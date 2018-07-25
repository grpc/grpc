/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
/* This is for _exit() below, which is temporary. */
#include <unistd.h>
#endif

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/profiling/timers.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/cmdline.h"
#include "test/core/util/grpc_profiler.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static grpc_completion_queue* cq;
static grpc_server* server;
static grpc_call* call;
static grpc_call_details call_details;
static grpc_metadata_array request_metadata_recv;
static grpc_metadata_array initial_metadata_send;
static grpc_byte_buffer* payload_buffer = nullptr;
/* Used to drain the terminal read in unary calls. */
static grpc_byte_buffer* terminal_buffer = nullptr;

static grpc_op read_op;
static grpc_op metadata_send_op;
static grpc_op write_op;
static grpc_op status_op[2];
static int was_cancelled = 2;
static grpc_op unary_ops[6];
static int got_sigint = 0;

static void* tag(intptr_t t) { return (void*)t; }

typedef enum {
  FLING_SERVER_NEW_REQUEST = 1,
  FLING_SERVER_READ_FOR_UNARY,
  FLING_SERVER_BATCH_OPS_FOR_UNARY,
  FLING_SERVER_SEND_INIT_METADATA_FOR_STREAMING,
  FLING_SERVER_READ_FOR_STREAMING,
  FLING_SERVER_WRITE_FOR_STREAMING,
  FLING_SERVER_SEND_STATUS_FOR_STREAMING
} fling_server_tags;

typedef struct {
  gpr_refcount pending_ops;
  uint32_t flags;
} call_state;

static void request_call(void) {
  grpc_metadata_array_init(&request_metadata_recv);
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_server_request_call(server, &call, &call_details,
                                      &request_metadata_recv, cq, cq,
                                      tag(FLING_SERVER_NEW_REQUEST)));
}

static void handle_unary_method(void) {
  grpc_op* op;
  grpc_call_error error;

  grpc_metadata_array_init(&initial_metadata_send);

  op = unary_ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &terminal_buffer;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  if (payload_buffer == nullptr) {
    gpr_log(GPR_INFO, "NULL payload buffer !!!");
  }
  op->data.send_message.send_message = payload_buffer;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status_details = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;

  error = grpc_call_start_batch(call, unary_ops,
                                static_cast<size_t>(op - unary_ops),
                                tag(FLING_SERVER_BATCH_OPS_FOR_UNARY), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
}

static void send_initial_metadata(void) {
  grpc_call_error error;
  void* tagarg = tag(FLING_SERVER_SEND_INIT_METADATA_FOR_STREAMING);
  grpc_metadata_array_init(&initial_metadata_send);
  metadata_send_op.op = GRPC_OP_SEND_INITIAL_METADATA;
  metadata_send_op.data.send_initial_metadata.count = 0;
  error = grpc_call_start_batch(call, &metadata_send_op, 1, tagarg, nullptr);

  GPR_ASSERT(GRPC_CALL_OK == error);
}

static void start_read_op(int t) {
  grpc_call_error error;
  /* Starting read at server */
  read_op.op = GRPC_OP_RECV_MESSAGE;
  read_op.data.recv_message.recv_message = &payload_buffer;
  error = grpc_call_start_batch(call, &read_op, 1, tag(t), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
}

static void start_write_op(void) {
  grpc_call_error error;
  void* tagarg = tag(FLING_SERVER_WRITE_FOR_STREAMING);
  /* Starting write at server */
  write_op.op = GRPC_OP_SEND_MESSAGE;
  if (payload_buffer == nullptr) {
    gpr_log(GPR_INFO, "NULL payload buffer !!!");
  }
  write_op.data.send_message.send_message = payload_buffer;
  error = grpc_call_start_batch(call, &write_op, 1, tagarg, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
}

static void start_send_status(void) {
  grpc_call_error error;
  void* tagarg = tag(FLING_SERVER_SEND_STATUS_FOR_STREAMING);
  status_op[0].op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  status_op[0].data.send_status_from_server.status = GRPC_STATUS_OK;
  status_op[0].data.send_status_from_server.trailing_metadata_count = 0;
  status_op[0].data.send_status_from_server.status_details = nullptr;
  status_op[1].op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  status_op[1].data.recv_close_on_server.cancelled = &was_cancelled;

  error = grpc_call_start_batch(call, status_op, 2, tagarg, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
}

/* We have some sort of deadlock, so let's not exit gracefully for now.
   When that is resolved, please remove the #include <unistd.h> above. */
static void sigint_handler(int x) { _exit(0); }

int main(int argc, char** argv) {
  grpc_event ev;
  call_state* s;
  char* addr_buf = nullptr;
  gpr_cmdline* cl;
  grpc_completion_queue* shutdown_cq;
  int shutdown_started = 0;
  int shutdown_finished = 0;

  int secure = 0;
  const char* addr = nullptr;

  char* fake_argv[1];

  gpr_timers_set_log_filename("latency_trace.fling_server.txt");

  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc_test_init(1, fake_argv);

  grpc_init();
  srand(static_cast<unsigned>(clock()));

  cl = gpr_cmdline_create("fling server");
  gpr_cmdline_add_string(cl, "bind", "Bind host:port", &addr);
  gpr_cmdline_add_flag(cl, "secure", "Run with security?", &secure);
  gpr_cmdline_parse(cl, argc, argv);
  gpr_cmdline_destroy(cl);

  if (addr == nullptr) {
    gpr_join_host_port(&addr_buf, "::", grpc_pick_unused_port_or_die());
    addr = addr_buf;
  }
  gpr_log(GPR_INFO, "creating server on: %s", addr);

  cq = grpc_completion_queue_create_for_next(nullptr);
  if (secure) {
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {test_server1_key,
                                                    test_server1_cert};
    grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
        nullptr, &pem_key_cert_pair, 1, 0, nullptr);
    server = grpc_server_create(nullptr, nullptr);
    GPR_ASSERT(grpc_server_add_secure_http2_port(server, addr, ssl_creds));
    grpc_server_credentials_release(ssl_creds);
  } else {
    server = grpc_server_create(nullptr, nullptr);
    GPR_ASSERT(grpc_server_add_insecure_http2_port(server, addr));
  }
  grpc_server_register_completion_queue(server, cq, nullptr);
  grpc_server_start(server);

  gpr_free(addr_buf);
  addr = addr_buf = nullptr;

  grpc_call_details_init(&call_details);

  request_call();

  grpc_profiler_start("server.prof");
  signal(SIGINT, sigint_handler);
  while (!shutdown_finished) {
    if (got_sigint && !shutdown_started) {
      gpr_log(GPR_INFO, "Shutting down due to SIGINT");

      shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
      grpc_server_shutdown_and_notify(server, shutdown_cq, tag(1000));

      GPR_ASSERT(grpc_completion_queue_pluck(
                     shutdown_cq, tag(1000),
                     grpc_timeout_seconds_to_deadline(5), nullptr)
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
    s = static_cast<call_state*>(ev.tag);
    switch (ev.type) {
      case GRPC_OP_COMPLETE:
        switch ((intptr_t)s) {
          case FLING_SERVER_NEW_REQUEST:
            if (call != nullptr) {
              if (0 == grpc_slice_str_cmp(call_details.method,
                                          "/Reflector/reflectStream")) {
                /* Received streaming call. Send metadata here. */
                start_read_op(FLING_SERVER_READ_FOR_STREAMING);
                send_initial_metadata();
              } else {
                /* Received unary call. Can do all ops in one batch. */
                start_read_op(FLING_SERVER_READ_FOR_UNARY);
              }
            } else {
              GPR_ASSERT(shutdown_started);
            }
            /*      request_call();
             */
            break;
          case FLING_SERVER_READ_FOR_STREAMING:
            if (payload_buffer != nullptr) {
              /* Received payload from client. */
              start_write_op();
            } else {
              /* Received end of stream from client. */
              start_send_status();
            }
            break;
          case FLING_SERVER_WRITE_FOR_STREAMING:
            /* Write completed at server  */
            grpc_byte_buffer_destroy(payload_buffer);
            payload_buffer = nullptr;
            start_read_op(FLING_SERVER_READ_FOR_STREAMING);
            break;
          case FLING_SERVER_SEND_INIT_METADATA_FOR_STREAMING:
            /* Metadata send completed at server */
            break;
          case FLING_SERVER_SEND_STATUS_FOR_STREAMING:
            /* Send status and close completed at server */
            grpc_call_unref(call);
            if (!shutdown_started) request_call();
            break;
          case FLING_SERVER_READ_FOR_UNARY:
            /* Finished payload read for unary. Start all reamaining
             *  unary ops in a batch.
             */
            handle_unary_method();
            break;
          case FLING_SERVER_BATCH_OPS_FOR_UNARY:
            /* Finished unary call. */
            grpc_byte_buffer_destroy(payload_buffer);
            payload_buffer = nullptr;
            grpc_call_unref(call);
            if (!shutdown_started) request_call();
            break;
        }
        break;
      case GRPC_QUEUE_SHUTDOWN:
        GPR_ASSERT(shutdown_started);
        shutdown_finished = 1;
        break;
      case GRPC_QUEUE_TIMEOUT:
        break;
    }
  }
  grpc_profiler_stop();
  grpc_call_details_destroy(&call_details);

  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
  grpc_shutdown();
  return 0;
}
