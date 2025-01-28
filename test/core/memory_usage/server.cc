//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifndef _WIN32
// This is for _exit() below, which is temporary.
#include <unistd.h>
#endif

#include <grpc/byte_buffer.h>
#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>

#include <algorithm>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "src/core/ext/transport/chaotic_good/server/chaotic_good_server.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/host_port.h"
#include "src/core/xds/grpc/xds_enabled_server.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/memory_usage/memstats.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

ABSL_FLAG(std::string, bind, "", "Bind host:port");
ABSL_FLAG(bool, secure, false, "Use security");
ABSL_FLAG(bool, minstack, false, "Use minimal stack");
ABSL_FLAG(bool, use_xds, false, "Use xDS");
ABSL_FLAG(bool, chaotic_good, false, "Use chaotic good");

static grpc_completion_queue* cq;
static grpc_server* server;
static grpc_op metadata_ops[2];
static grpc_op snapshot_ops[5];
static grpc_op status_op;
static int got_sigint = 0;
static grpc_byte_buffer* payload_buffer = nullptr;
static int was_cancelled = 2;

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

typedef enum {
  FLING_SERVER_NEW_REQUEST = 1,
  FLING_SERVER_SEND_INIT_METADATA,
  FLING_SERVER_WAIT_FOR_DESTROY,
  FLING_SERVER_SEND_STATUS_FLING_CALL,
  FLING_SERVER_SEND_STATUS_SNAPSHOT,
  FLING_SERVER_BATCH_SEND_STATUS_FLING_CALL
} fling_server_tags;

typedef struct {
  fling_server_tags state;
  grpc_call* call;
  grpc_call_details call_details;
  grpc_metadata_array request_metadata_recv;
  grpc_metadata_array initial_metadata_send;
} fling_call;

// hold up to 100000 calls and 6 snaphost calls
static fling_call calls[1000006];

static void request_call_unary(int call_idx) {
  if (call_idx == static_cast<int>(sizeof(calls) / sizeof(fling_call))) {
    LOG(INFO) << "Used all call slots (10000) on server. Server exit.";
    _exit(0);
  }
  grpc_metadata_array_init(&calls[call_idx].request_metadata_recv);
  grpc_server_request_call(
      server, &calls[call_idx].call, &calls[call_idx].call_details,
      &calls[call_idx].request_metadata_recv, cq, cq, &calls[call_idx]);
}

static void send_initial_metadata_unary(void* tag) {
  grpc_metadata_array_init(
      &(*static_cast<fling_call*>(tag)).initial_metadata_send);
  metadata_ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  metadata_ops[0].data.send_initial_metadata.count = 0;

  CHECK(GRPC_CALL_OK == grpc_call_start_batch((*(fling_call*)tag).call,
                                              metadata_ops, 1, tag, nullptr));
}

static void send_status(void* tag) {
  status_op.op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  status_op.data.send_status_from_server.status = GRPC_STATUS_OK;
  status_op.data.send_status_from_server.trailing_metadata_count = 0;
  grpc_slice details = grpc_slice_from_static_string("");
  status_op.data.send_status_from_server.status_details = &details;

  CHECK(GRPC_CALL_OK == grpc_call_start_batch((*(fling_call*)tag).call,
                                              &status_op, 1, tag, nullptr));
}

static void send_snapshot(void* tag, MemStats* snapshot) {
  grpc_op* op;

  grpc_slice snapshot_slice =
      grpc_slice_new(snapshot, sizeof(*snapshot), gpr_free);
  payload_buffer = grpc_raw_byte_buffer_create(&snapshot_slice, 1);
  grpc_metadata_array_init(
      &(*static_cast<fling_call*>(tag)).initial_metadata_send);

  op = snapshot_ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  if (payload_buffer == nullptr) {
    LOG(INFO) << "NULL payload buffer !!!";
  }
  op->data.send_message.send_message = payload_buffer;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  grpc_slice details = grpc_slice_from_static_string("");
  op->data.send_status_from_server.status_details = &details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;

  CHECK(GRPC_CALL_OK ==
        grpc_call_start_batch((*(fling_call*)tag).call, snapshot_ops,
                              (size_t)(op - snapshot_ops), tag, nullptr));
}
// We have some sort of deadlock, so let's not exit gracefully for now.
// When that is resolved, please remove the #include <unistd.h> above.
static void sigint_handler(int /*x*/) { _exit(0); }

static void OnServingStatusUpdate(void* /*user_data*/, const char* uri,
                                  grpc_serving_status_update update) {
  absl::Status status(static_cast<absl::StatusCode>(update.code),
                      update.error_message);
  LOG(INFO) << "xDS serving status notification: uri=\"" << uri
            << "\", status=" << status;
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  grpc_event ev;
  grpc_completion_queue* shutdown_cq;
  int shutdown_started = 0;
  int shutdown_finished = 0;

  char* fake_argv[1];

  CHECK_GE(argc, 1);
  fake_argv[0] = argv[0];
  grpc::testing::TestEnvironment env(&argc, argv);

  grpc_init();
  srand(static_cast<unsigned>(clock()));

  std::string addr = absl::GetFlag(FLAGS_bind);
  if (addr.empty()) {
    addr = grpc_core::JoinHostPort("::", grpc_pick_unused_port_or_die());
  }
  LOG(INFO) << "creating server on: " << addr;

  cq = grpc_completion_queue_create_for_next(nullptr);

  std::vector<grpc_arg> args_vec;
  if (absl::GetFlag(FLAGS_minstack)) {
    args_vec.push_back(grpc_channel_arg_integer_create(
        const_cast<char*>(GRPC_ARG_MINIMAL_STACK), 1));
  }
  // TODO(roth): The xDS code here duplicates the functionality in
  // XdsServerBuilder, which is undesirable.  We should ideally convert
  // this to use the C++ API instead of the C-core API, so that we can
  // avoid this duplication.
  if (absl::GetFlag(FLAGS_use_xds)) {
    args_vec.push_back(grpc_channel_arg_integer_create(
        const_cast<char*>(GRPC_ARG_XDS_ENABLED_SERVER), 1));
  }

  grpc_channel_args args = {args_vec.size(), args_vec.data()};
  server = grpc_server_create(&args, nullptr);

  if (absl::GetFlag(FLAGS_use_xds)) {
    grpc_server_config_fetcher* config_fetcher =
        grpc_server_config_fetcher_xds_create({OnServingStatusUpdate, nullptr},
                                              &args);
    if (config_fetcher != nullptr) {
      grpc_server_set_config_fetcher(server, config_fetcher);
    }
  }

  MemStats before_server_create = MemStats::Snapshot();
  if (absl::GetFlag(FLAGS_chaotic_good)) {
    grpc_server_add_chaotic_good_port(server, addr.c_str());
  } else if (absl::GetFlag(FLAGS_secure)) {
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {test_server1_key,
                                                    test_server1_cert};
    grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
        nullptr, &pem_key_cert_pair, 1, 0, nullptr);
    CHECK(grpc_server_add_http2_port(server, addr.c_str(), ssl_creds));
    grpc_server_credentials_release(ssl_creds);
  } else {
    CHECK(grpc_server_add_http2_port(
        server, addr.c_str(), grpc_insecure_server_credentials_create()));
  }

  grpc_server_register_completion_queue(server, cq, nullptr);
  grpc_server_start(server);

  MemStats after_server_create = MemStats::Snapshot();

  // initialize call instances
  for (int i = 0; i < static_cast<int>(sizeof(calls) / sizeof(fling_call));
       i++) {
    grpc_call_details_init(&calls[i].call_details);
    calls[i].state = FLING_SERVER_NEW_REQUEST;
  }

  int next_call_idx = 0;
  MemStats current_snapshot;

  request_call_unary(next_call_idx);

  signal(SIGINT, sigint_handler);

  while (!shutdown_finished) {
    if (got_sigint && !shutdown_started) {
      LOG(INFO) << "Shutting down due to SIGINT";

      shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
      grpc_server_shutdown_and_notify(server, shutdown_cq, tag(1000));
      CHECK(grpc_completion_queue_pluck(shutdown_cq, tag(1000),
                                        grpc_timeout_seconds_to_deadline(5),
                                        nullptr)
                .type == GRPC_OP_COMPLETE);
      grpc_completion_queue_destroy(shutdown_cq);
      grpc_completion_queue_shutdown(cq);
      shutdown_started = 1;
    }
    ev = grpc_completion_queue_next(
        cq,
        gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                     gpr_time_from_micros(1000000, GPR_TIMESPAN)),
        nullptr);
    fling_call* s = static_cast<fling_call*>(ev.tag);
    switch (ev.type) {
      case GRPC_OP_COMPLETE:
        switch (s->state) {
          case FLING_SERVER_NEW_REQUEST:
            request_call_unary(++next_call_idx);
            if (0 == grpc_slice_str_cmp(s->call_details.method,
                                        "/Reflector/reflectUnary")) {
              s->state = FLING_SERVER_SEND_INIT_METADATA;
              send_initial_metadata_unary(s);
            } else if (0 ==
                       grpc_slice_str_cmp(s->call_details.method,
                                          "Reflector/GetBeforeSvrCreation")) {
              s->state = FLING_SERVER_SEND_STATUS_SNAPSHOT;
              send_snapshot(s, &before_server_create);
            } else if (0 ==
                       grpc_slice_str_cmp(s->call_details.method,
                                          "Reflector/GetAfterSvrCreation")) {
              s->state = FLING_SERVER_SEND_STATUS_SNAPSHOT;
              send_snapshot(s, &after_server_create);
            } else if (0 == grpc_slice_str_cmp(s->call_details.method,
                                               "Reflector/SimpleSnapshot")) {
              s->state = FLING_SERVER_SEND_STATUS_SNAPSHOT;
              current_snapshot = MemStats::Snapshot();
              send_snapshot(s, &current_snapshot);
            } else if (0 == grpc_slice_str_cmp(s->call_details.method,
                                               "Reflector/DestroyCalls")) {
              s->state = FLING_SERVER_BATCH_SEND_STATUS_FLING_CALL;
              current_snapshot = MemStats::Snapshot();
              send_snapshot(s, &current_snapshot);
            } else {
              LOG(ERROR) << "Wrong call method";
            }
            break;
          case FLING_SERVER_SEND_INIT_METADATA:
            s->state = FLING_SERVER_WAIT_FOR_DESTROY;
            break;
          case FLING_SERVER_WAIT_FOR_DESTROY:
            break;
          case FLING_SERVER_SEND_STATUS_FLING_CALL:
            grpc_call_unref(s->call);
            grpc_call_details_destroy(&s->call_details);
            grpc_metadata_array_destroy(&s->initial_metadata_send);
            grpc_metadata_array_destroy(&s->request_metadata_recv);
            break;
          case FLING_SERVER_BATCH_SEND_STATUS_FLING_CALL:
            for (int k = 0;
                 k < static_cast<int>(sizeof(calls) / sizeof(fling_call));
                 ++k) {
              if (calls[k].state == FLING_SERVER_WAIT_FOR_DESTROY) {
                calls[k].state = FLING_SERVER_SEND_STATUS_FLING_CALL;
                send_status(&calls[k]);
              }
            }
            [[fallthrough]];
          // no break here since we want to continue to case
          // FLING_SERVER_SEND_STATUS_SNAPSHOT to destroy the snapshot call
          case FLING_SERVER_SEND_STATUS_SNAPSHOT:
            grpc_byte_buffer_destroy(payload_buffer);
            grpc_call_unref(s->call);
            grpc_call_details_destroy(&s->call_details);
            grpc_metadata_array_destroy(&s->initial_metadata_send);
            grpc_metadata_array_destroy(&s->request_metadata_recv);
            payload_buffer = nullptr;
            break;
        }
        break;
      case GRPC_QUEUE_SHUTDOWN:
        CHECK(shutdown_started);
        shutdown_finished = 1;
        break;
      case GRPC_QUEUE_TIMEOUT:
        break;
    }
  }

  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
  grpc_shutdown_blocking();
  return 0;
}
