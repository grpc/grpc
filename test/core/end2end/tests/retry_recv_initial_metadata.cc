//
//
// Copyright 2017 gRPC authors.
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

#include <stdint.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char* test_name,
                                            grpc_channel_args* client_args,
                                            grpc_channel_args* server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
  f = config.create_fixture(client_args, server_args);
  config.init_server(&f, server_args);
  config.init_client(&f, client_args);
  return f;
}

static gpr_timespec n_seconds_from_now(int n) {
  return grpc_timeout_seconds_to_deadline(n);
}

static gpr_timespec five_seconds_from_now(void) {
  return n_seconds_from_now(5);
}

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_from_now(), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture* f) {
  if (!f->server) return;
  grpc_server_shutdown_and_notify(f->server, f->cq, tag(1000));
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(f->cq, grpc_timeout_seconds_to_deadline(5),
                                    nullptr);
  } while (ev.type != GRPC_OP_COMPLETE || ev.tag != tag(1000));
  grpc_server_destroy(f->server);
  f->server = nullptr;
}

static void shutdown_client(grpc_end2end_test_fixture* f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = nullptr;
}

static void end_test(grpc_end2end_test_fixture* f) {
  shutdown_server(f);
  shutdown_client(f);

  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);
}

// Tests that receiving initial metadata commits the call.
// - 1 retry allowed for ABORTED status
// - first attempt receives initial metadata before trailing metadata,
//   so no retry is done even though status was ABORTED
static void test_retry_recv_initial_metadata(grpc_end2end_test_config config) {
  grpc_call* c;
  grpc_call* s;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer* response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer* request_payload_recv = nullptr;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char* peer;
  grpc_metadata initial_metadata_from_server = {
      grpc_slice_from_static_string("key1"),
      grpc_slice_from_static_string("val1"),
      {{nullptr, nullptr, nullptr, nullptr}}};

  grpc_arg args[] = {
      grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_ARG_SERVICE_CONFIG),
          const_cast<char*>(
              "{\n"
              "  \"methodConfig\": [ {\n"
              "    \"name\": [\n"
              "      { \"service\": \"service\", \"method\": \"method\" }\n"
              "    ],\n"
              "    \"retryPolicy\": {\n"
              "      \"maxAttempts\": 2,\n"
              "      \"initialBackoff\": \"1s\",\n"
              "      \"maxBackoff\": \"120s\",\n"
              "      \"backoffMultiplier\": 1.6,\n"
              "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
              "    }\n"
              "  } ]\n"
              "}")),
  };
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(args), args};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_recv_initial_metadata", &client_args, nullptr);

  grpc_core::CqVerifier cqv(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/service/method"),
                               nullptr, deadline, nullptr);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != nullptr);
  gpr_log(GPR_DEBUG, "client_peer_before_call=%s", peer);
  gpr_free(peer);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);
  grpc_slice status_details = grpc_slice_from_static_string("xyz");

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(tag(101), true);
  cqv.Verify();

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != nullptr);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != nullptr);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  // Server sends initial metadata in its own batch, before sending
  // trailing metadata.
  // Ideally, this would not require actually sending any metadata
  // entries, but we do so to avoid sporadic failures in the proxy
  // tests, where the proxy may wind up combining the batches, depending
  // on timing.  Sending a metadata entry ensures that the transport
  // won't send a Trailers-Only response, even if the batches are combined.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 1;
  op->data.send_initial_metadata.metadata = &initial_metadata_from_server;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(tag(102), true);
  cqv.Verify();

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(103),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(tag(103), true);
  cqv.Expect(tag(1), true);
  cqv.Verify();

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  GPR_ASSERT(was_cancelled == 0);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(request_payload_recv);
  grpc_byte_buffer_destroy(response_payload_recv);

  grpc_call_unref(c);
  grpc_call_unref(s);

  end_test(&f);
  config.tear_down_data(&f);
}

void retry_recv_initial_metadata(grpc_end2end_test_config config) {
  GPR_ASSERT(config.feature_mask & FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL);
  test_retry_recv_initial_metadata(config);
}

void retry_recv_initial_metadata_pre_init(void) {}
