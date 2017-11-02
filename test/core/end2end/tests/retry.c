/*
 *
 * Copyright 2017 gRPC authors.
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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/static_metadata.h"

#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/tests/cancel_test_helpers.h"

static void *tag(intptr_t t) { return (void *)t; }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char *test_name,
                                            grpc_channel_args *client_args,
                                            grpc_channel_args *server_args) {
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

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_from_now(), NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture *f) {
  if (!f->server) return;
  grpc_server_shutdown_and_notify(f->server, f->shutdown_cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(f->shutdown_cq, tag(1000),
                                         grpc_timeout_seconds_to_deadline(5),
                                         NULL)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(f->server);
  f->server = NULL;
}

static void shutdown_client(grpc_end2end_test_fixture *f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = NULL;
}

static void end_test(grpc_end2end_test_fixture *f) {
  shutdown_server(f);
  shutdown_client(f);

  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);
  grpc_completion_queue_destroy(f->shutdown_cq);
}

// Tests a basic retry scenario:
// - 2 retry attempts allowed for ABORTED status
// - first attempt gets ABORTED
// - second attempt gets OK
static void test_retry_basic(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg arg = {
      .key = GRPC_ARG_SERVICE_CONFIG,
      .type = GRPC_ARG_STRING,
      .value.string =
          "{\n"
          "  \"methodConfig\": [ {\n"
          "    \"name\": [\n"
          "      { \"service\": \"service\", \"method\": \"method\" }\n"
          "    ],\n"
          "    \"retryPolicy\": {\n"
          "      \"maxAttempts\": 3,\n"
          "      \"initialBackoff\": \"1s\",\n"
          "      \"maxBackoff\": \"120s\",\n"
          "      \"backoffMultiplier\": 1.6,\n"
          "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
          "    }\n"
          "  } ]\n"
          "}"};
  grpc_channel_args client_args = {1, &arg};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_basic", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  // Make sure the "grpc-previous-rpc-attempts" header was not sent in the
  // initial attempt.
  for (size_t i = 0; i < request_metadata_recv.count; ++i) {
    GPR_ASSERT(!grpc_slice_eq(request_metadata_recv.metadata[i].key,
                              GRPC_MDSTR_GRPC_PREVIOUS_RPC_ATTEMPTS));
  }

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  cq_verify(cqv);

  grpc_call_unref(s);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_details_init(&call_details);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(201));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(201), true);
  cq_verify(cqv);

  // Make sure the "grpc-previous-rpc-attempts" header was sent in the retry.
  bool found_retry_header = false;
  for (size_t i = 0; i < request_metadata_recv.count; ++i) {
    if (grpc_slice_eq(request_metadata_recv.metadata[i].key,
                      GRPC_MDSTR_GRPC_PREVIOUS_RPC_ATTEMPTS)) {
      GPR_ASSERT(grpc_slice_str_cmp(request_metadata_recv.metadata[i].value,
                                    "1") == 0);
      found_retry_header = true;
      break;
    }
  }
  GPR_ASSERT(found_retry_header);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(202), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(202), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_OK);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
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

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests retrying a streaming RPC.  This is the same as
// test_retry_basic(), except that the client sends two messages on the
// call before the initial attempt fails.
// FIXME: We should also test the case where the retry is committed after
// replaying 1 of 2 previously-completed send_message ops.  However,
// there's no way to trigger that from an end2end test, because the
// replayed ops happen under the hood -- they are not surfaced to the
// C-core API, and therefore we have no way to inject the commit at the
// right point.
static void test_retry_streaming(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice request2_payload_slice = grpc_slice_from_static_string("bar");
  grpc_slice request3_payload_slice = grpc_slice_from_static_string("baz");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("quux");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *request2_payload =
      grpc_raw_byte_buffer_create(&request2_payload_slice, 1);
  grpc_byte_buffer *request3_payload =
      grpc_raw_byte_buffer_create(&request3_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *request2_payload_recv = NULL;
  grpc_byte_buffer *request3_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg arg = {
      .key = GRPC_ARG_SERVICE_CONFIG,
      .type = GRPC_ARG_STRING,
      .value.string =
          "{\n"
          "  \"methodConfig\": [ {\n"
          "    \"name\": [\n"
          "      { \"service\": \"service\", \"method\": \"method\" }\n"
          "    ],\n"
          "    \"retryPolicy\": {\n"
          "      \"maxAttempts\": 3,\n"
          "      \"initialBackoff\": \"1s\",\n"
          "      \"maxBackoff\": \"120s\",\n"
          "      \"backoffMultiplier\": 1.6,\n"
          "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
          "    }\n"
          "  } ]\n"
          "}"};
  grpc_channel_args client_args = {1, &arg};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_streaming", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer_before_call=%s", peer);
  gpr_free(peer);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);
  grpc_slice status_details = grpc_slice_from_static_string("xyz");

  // Client starts a batch for receiving initial metadata, a message,
  // and trailing metadata.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Client sends initial metadata and a message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(2), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(2), true);
  cq_verify(cqv);

  // Server gets a call with received initial metadata.
  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  // Server receives a message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  cq_verify(cqv);

  // Client sends a second message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request2_payload;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(3), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(3), true);
  cq_verify(cqv);

  // Server receives the second message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request2_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(103), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(103), true);
  cq_verify(cqv);

  // Server sends both initial and trailing metadata.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(104), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(104), true);
  cq_verify(cqv);

  // Clean up from first attempt.
  grpc_call_unref(s);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_details_init(&call_details);
  GPR_ASSERT(byte_buffer_eq_slice(request_payload_recv, request_payload_slice));
  grpc_byte_buffer_destroy(request_payload_recv);
  request_payload_recv = NULL;
  GPR_ASSERT(
      byte_buffer_eq_slice(request2_payload_recv, request2_payload_slice));
  grpc_byte_buffer_destroy(request2_payload_recv);
  request2_payload_recv = NULL;

  // Server gets a second call (the retry).
  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(201));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(201), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  // Server receives a message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(202), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(202), true);
  cq_verify(cqv);

  // Server receives a second message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request2_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(203), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(203), true);
  cq_verify(cqv);

  // Client sends a third message and a close.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request3_payload;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(4), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(4), true);
  cq_verify(cqv);

  // Server receives a third message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request3_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(204), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(204), true);
  cq_verify(cqv);

  // Server receives a close and sends initial metadata, a message, and
  // trailing metadata.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  // Returning a retriable code, but because we are also sending a
  // message, the client will commit instead of retrying again.
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(205), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(205), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(request2_payload);
  grpc_byte_buffer_destroy(request3_payload);
  grpc_byte_buffer_destroy(response_payload);
  GPR_ASSERT(byte_buffer_eq_slice(request_payload_recv, request_payload_slice));
  grpc_byte_buffer_destroy(request_payload_recv);
  GPR_ASSERT(
      byte_buffer_eq_slice(request2_payload_recv, request2_payload_slice));
  grpc_byte_buffer_destroy(request2_payload_recv);
  GPR_ASSERT(
      byte_buffer_eq_slice(request3_payload_recv, request3_payload_slice));
  grpc_byte_buffer_destroy(request3_payload_recv);
  grpc_byte_buffer_destroy(response_payload_recv);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests that we correctly clean up if the second attempt finishes
// before we have finished replaying all of the send ops.
static void test_retry_streaming_succeeds_before_replay_finished(
    grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice request2_payload_slice = grpc_slice_from_static_string("bar");
  grpc_slice request3_payload_slice = grpc_slice_from_static_string("baz");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("quux");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *request2_payload =
      grpc_raw_byte_buffer_create(&request2_payload_slice, 1);
  grpc_byte_buffer *request3_payload =
      grpc_raw_byte_buffer_create(&request3_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *request2_payload_recv = NULL;
  grpc_byte_buffer *request3_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg arg = {
      .key = GRPC_ARG_SERVICE_CONFIG,
      .type = GRPC_ARG_STRING,
      .value.string =
          "{\n"
          "  \"methodConfig\": [ {\n"
          "    \"name\": [\n"
          "      { \"service\": \"service\", \"method\": \"method\" }\n"
          "    ],\n"
          "    \"retryPolicy\": {\n"
          "      \"maxAttempts\": 3,\n"
          "      \"initialBackoff\": \"1s\",\n"
          "      \"maxBackoff\": \"120s\",\n"
          "      \"backoffMultiplier\": 1.6,\n"
          "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
          "    }\n"
          "  } ]\n"
          "}"};
  grpc_channel_args client_args = {1, &arg};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_streaming", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer_before_call=%s", peer);
  gpr_free(peer);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);
  grpc_slice status_details = grpc_slice_from_static_string("xyz");

  // Client starts a batch for receiving initial metadata, a message,
  // and trailing metadata.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Client sends initial metadata and a message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(2), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(2), true);
  cq_verify(cqv);

  // Server gets a call with received initial metadata.
  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  // Server receives a message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  cq_verify(cqv);

  // Client sends a second message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request2_payload;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(3), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(3), true);
  cq_verify(cqv);

  // Server receives the second message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request2_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(103), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(103), true);
  cq_verify(cqv);

  // Client sends a third message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request3_payload;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(4), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(4), true);
  cq_verify(cqv);

  // Server receives the third message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request3_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(104), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(104), true);
  cq_verify(cqv);

  // Server sends both initial and trailing metadata.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(105), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(105), true);
  cq_verify(cqv);

  // Clean up from first attempt.
  grpc_call_unref(s);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_details_init(&call_details);
  GPR_ASSERT(byte_buffer_eq_slice(request_payload_recv, request_payload_slice));
  grpc_byte_buffer_destroy(request_payload_recv);
  request_payload_recv = NULL;
  GPR_ASSERT(
      byte_buffer_eq_slice(request2_payload_recv, request2_payload_slice));
  grpc_byte_buffer_destroy(request2_payload_recv);
  request2_payload_recv = NULL;
  GPR_ASSERT(
      byte_buffer_eq_slice(request3_payload_recv, request3_payload_slice));
  grpc_byte_buffer_destroy(request3_payload_recv);
  request3_payload_recv = NULL;

  // Server gets a second call (the retry).
  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(201));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(201), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  // Server receives the first message (and does not receive any others).
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(202), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(202), true);
  cq_verify(cqv);

  // Server sends initial metadata, a message, and trailing metadata.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  // Returning a retriable code, but because we are also sending a
  // message, the client will commit instead of retrying again.
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(205), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(205), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(request2_payload);
  grpc_byte_buffer_destroy(request3_payload);
  grpc_byte_buffer_destroy(response_payload);
  GPR_ASSERT(byte_buffer_eq_slice(request_payload_recv, request_payload_slice));
  grpc_byte_buffer_destroy(request_payload_recv);
  grpc_byte_buffer_destroy(response_payload_recv);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests that we can continue to send/recv messages on a streaming call
// after retries are committed.
static void test_retry_streaming_after_commit(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice request2_payload_slice = grpc_slice_from_static_string("bar");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("baz");
  grpc_slice response2_payload_slice = grpc_slice_from_static_string("quux");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *request2_payload =
      grpc_raw_byte_buffer_create(&request2_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *response2_payload =
      grpc_raw_byte_buffer_create(&response2_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *request2_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_byte_buffer *response2_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg arg = {
      .key = GRPC_ARG_SERVICE_CONFIG,
      .type = GRPC_ARG_STRING,
      .value.string =
          "{\n"
          "  \"methodConfig\": [ {\n"
          "    \"name\": [\n"
          "      { \"service\": \"service\", \"method\": \"method\" }\n"
          "    ],\n"
          "    \"retryPolicy\": {\n"
          "      \"maxAttempts\": 3,\n"
          "      \"initialBackoff\": \"1s\",\n"
          "      \"maxBackoff\": \"120s\",\n"
          "      \"backoffMultiplier\": 1.6,\n"
          "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
          "    }\n"
          "  } ]\n"
          "}"};
  grpc_channel_args client_args = {1, &arg};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_streaming_after_commit", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer_before_call=%s", peer);
  gpr_free(peer);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);
  grpc_slice status_details = grpc_slice_from_static_string("xyz");

  // Client starts a batch for receiving trailing metadata.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Client starts a batch for receiving initial metadata and a message.
  // This will commit retries.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(2), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Client sends initial metadata and a message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(3), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(3), true);
  cq_verify(cqv);

  // Server gets a call with received initial metadata.
  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  // Server receives a message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  cq_verify(cqv);

  // Server sends initial metadata and a message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(103), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(103), true);
  cq_verify(cqv);

  // Client receives initial metadata and a message.
  CQ_EXPECT_COMPLETION(cqv, tag(2), true);
  cq_verify(cqv);

  // Client sends a second message and a close.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request2_payload;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(4), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(4), true);
  cq_verify(cqv);

  // Server receives a second message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request2_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(104), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(104), true);
  cq_verify(cqv);

  // Server receives a close, sends a second message, and sends status.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response2_payload;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  // Returning a retriable code, but because retries are already
  // committed, the client will not retry.
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(105), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(105), true);
  cq_verify(cqv);

  // Client receives a second message.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response2_payload_recv;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(5), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(5), true);
  cq_verify(cqv);

  // Client receives status.
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(request2_payload);
  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(response2_payload);
  GPR_ASSERT(byte_buffer_eq_slice(request_payload_recv, request_payload_slice));
  grpc_byte_buffer_destroy(request_payload_recv);
  GPR_ASSERT(
      byte_buffer_eq_slice(request2_payload_recv, request2_payload_slice));
  grpc_byte_buffer_destroy(request2_payload_recv);
  GPR_ASSERT(
      byte_buffer_eq_slice(response_payload_recv, response_payload_slice));
  grpc_byte_buffer_destroy(response_payload_recv);
  GPR_ASSERT(
      byte_buffer_eq_slice(response2_payload_recv, response2_payload_slice));
  grpc_byte_buffer_destroy(response2_payload_recv);
  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests that we stop retrying after the configured number of attempts.
// - 1 retry attempt allowed for ABORTED status
// - first attempt gets ABORTED
// - second attempt gets ABORTED but does not retry
static void test_retry_too_many_attempts(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg arg = {
      .key = GRPC_ARG_SERVICE_CONFIG,
      .type = GRPC_ARG_STRING,
      .value.string =
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
          "}"};
  grpc_channel_args client_args = {1, &arg};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_too_many_attempts", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  cq_verify(cqv);

  grpc_call_unref(s);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_details_init(&call_details);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(201));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(201), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(202), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(202), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

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

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests that we don't retry for non-retryable status codes.
// - 1 retry attempt allowed for ABORTED status
// - first attempt gets INVALID_ARGUMENT, so no retry is done
static void test_retry_non_retriable_status(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg arg = {
      .key = GRPC_ARG_SERVICE_CONFIG,
      .type = GRPC_ARG_STRING,
      .value.string =
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
          "}"};
  grpc_channel_args client_args = {1, &arg};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_non_retriable_status", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_INVALID_ARGUMENT;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_INVALID_ARGUMENT);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

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

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests that we don't make any further attempts after we exceed the
// max buffer size.
// - 1 retry attempt allowed for ABORTED status
// - buffer size set to 2 bytes
// - client sends a 3-byte message
// - first attempt gets ABORTED but is not retried
static void test_retry_exceeds_buffer_size_in_initial_batch(
    grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg args[] = {
      {.key = GRPC_ARG_SERVICE_CONFIG,
       .type = GRPC_ARG_STRING,
       .value.string =
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
           "}"},
      {.key = GRPC_ARG_PER_RPC_RETRY_BUFFER_SIZE,
       .type = GRPC_ARG_INTEGER,
       .value.integer = 2}};
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(args), args};
  grpc_end2end_test_fixture f = begin_test(
      config, "retry_exceeds_buffer_size_in_initial_batch", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

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

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Similar to test_retry_exceeds_buffer_size_in_initial_batch(), but we don't
// exceed the buffer size until the second batch.
// - 1 retry attempt allowed for ABORTED status
// - buffer size set to 100 KiB (larger than initial metadata)
// - client sends a 100 KiB message
// - first attempt gets ABORTED but is not retried
static void test_retry_exceeds_buffer_size_in_subsequent_batch(
    grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  char buf[102401];
  memset(buf, 'a', sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  grpc_slice request_payload_slice = grpc_slice_from_static_string(buf);
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg args[] = {
      {.key = GRPC_ARG_SERVICE_CONFIG,
       .type = GRPC_ARG_STRING,
       .value.string =
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
           "}"},
      {
          .key = GRPC_ARG_PER_RPC_RETRY_BUFFER_SIZE,
          .type = GRPC_ARG_INTEGER,
          .value.integer = 102400,
      }};
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(args), args};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_exceeds_buffer_size_in_subsequent_batch",
                 &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(2), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  CQ_EXPECT_COMPLETION(cqv, tag(2), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

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

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests that receiving initial metadata commits the call.
// - 1 retry attempt allowed for ABORTED status
// - first attempt receives initial metadata before trailing metadata,
//   so no retry is done even though status was ABORTED
static void test_retry_recv_initial_metadata(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg arg = {
      .key = GRPC_ARG_SERVICE_CONFIG,
      .type = GRPC_ARG_STRING,
      .value.string =
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
          "}"};
  grpc_channel_args client_args = {1, &arg};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_recv_initial_metadata", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  // Server sends initial metadata in its own batch, before sending
  // trailing metadata.
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  cq_verify(cqv);

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
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(103), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(103), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

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

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests that receiving a message commits the call.
// - 1 retry attempt allowed for ABORTED status
// - first attempt receives a message and therefore does not retry even
//   though the final status is ABORTED
static void test_retry_recv_message(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg arg = {
      .key = GRPC_ARG_SERVICE_CONFIG,
      .type = GRPC_ARG_STRING,
      .value.string =
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
          "}"};
  grpc_channel_args client_args = {1, &arg};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_recv_message", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(103), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(103), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

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

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests that we don't retry when retries are disabled.
// - 1 retry attempt allowed for ABORTED status
// - first attempt gets ABORTED but does not retry
static void test_retry_disabled(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg args[] = {
      {.key = GRPC_ARG_SERVICE_CONFIG,
       .type = GRPC_ARG_STRING,
       .value.string =
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
           "}"},
      {.key = GRPC_ARG_ENABLE_RETRIES,
       .type = GRPC_ARG_INTEGER,
       .value.integer = 0}};
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(args), args};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_disabled", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

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

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests that we don't retry when throttled.
// - 1 retry attempt allowed for ABORTED status
// - first attempt gets ABORTED but is over limit, so no retry is done
static void test_retry_throttled(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg arg = {
      .key = GRPC_ARG_SERVICE_CONFIG,
      .type = GRPC_ARG_STRING,
      .value.string =
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
          "  } ],\n"
          // A single failure will cause us to be throttled.
          // (This is not a very realistic config, but it works for the
          // purposes of this test.)
          "  \"retryThrottling\": {\n"
          "    \"maxTokens\": 2,\n"
          "    \"tokenRatio\": 1.0,\n"
          "  }\n"
          "}"};
  grpc_channel_args client_args = {1, &arg};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_throttled", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

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

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests that we honor server push-back delay.
// - 2 retry attempt allowed for ABORTED status
// - first attempt gets ABORTED with a long delay
// - second attempt succeeds
static void test_retry_server_pushback_delay(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_metadata pushback_md;
  memset(&pushback_md, 0, sizeof(pushback_md));
  pushback_md.key = GRPC_MDSTR_GRPC_RETRY_PUSHBACK_MS;
  pushback_md.value = grpc_slice_from_static_string("2000");

  grpc_arg arg = {
      .key = GRPC_ARG_SERVICE_CONFIG,
      .type = GRPC_ARG_STRING,
      .value.string =
          "{\n"
          "  \"methodConfig\": [ {\n"
          "    \"name\": [\n"
          "      { \"service\": \"service\", \"method\": \"method\" }\n"
          "    ],\n"
          "    \"retryPolicy\": {\n"
          "      \"maxAttempts\": 3,\n"
          "      \"initialBackoff\": \"1s\",\n"
          "      \"maxBackoff\": \"120s\",\n"
          "      \"backoffMultiplier\": 1.6,\n"
          "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
          "    }\n"
          "  } ]\n"
          "}"};
  grpc_channel_args client_args = {1, &arg};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_server_pushback_disabled", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 1;
  op->data.send_status_from_server.trailing_metadata = &pushback_md;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  cq_verify(cqv);

  gpr_timespec before_retry = gpr_now(GPR_CLOCK_MONOTONIC);

  grpc_call_unref(s);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_details_init(&call_details);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(201));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(201), true);
  cq_verify(cqv);

  gpr_timespec after_retry = gpr_now(GPR_CLOCK_MONOTONIC);
  gpr_timespec retry_delay = gpr_time_sub(after_retry, before_retry);
  // Configured back-off was 1 second, server push-back said 2 seconds.
  // To avoid flakiness, we allow some fudge factor here.
  gpr_log(GPR_INFO, "retry delay was {.tv_sec=%" PRIdPTR ", .tv_nsec=%d}",
          retry_delay.tv_sec, retry_delay.tv_nsec);
  GPR_ASSERT(retry_delay.tv_sec >= 1);
  if (retry_delay.tv_sec == 1) {
    GPR_ASSERT(retry_delay.tv_nsec >= 999000000);
  }

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(202), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(202), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_OK);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
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

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests that we don't retry when disabled by server push-back.
// - 2 retry attempt allowed for ABORTED status
// - first attempt gets ABORTED
// - second attempt gets ABORTED but server push back disables retrying
static void test_retry_server_pushback_disabled(
    grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_metadata pushback_md;
  memset(&pushback_md, 0, sizeof(pushback_md));
  pushback_md.key = GRPC_MDSTR_GRPC_RETRY_PUSHBACK_MS;
  pushback_md.value = grpc_slice_from_static_string("-1");

  grpc_arg arg = {
      .key = GRPC_ARG_SERVICE_CONFIG,
      .type = GRPC_ARG_STRING,
      .value.string =
          "{\n"
          "  \"methodConfig\": [ {\n"
          "    \"name\": [\n"
          "      { \"service\": \"service\", \"method\": \"method\" }\n"
          "    ],\n"
          "    \"retryPolicy\": {\n"
          "      \"maxAttempts\": 3,\n"
          "      \"initialBackoff\": \"1s\",\n"
          "      \"maxBackoff\": \"120s\",\n"
          "      \"backoffMultiplier\": 1.6,\n"
          "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
          "    }\n"
          "  } ]\n"
          "}"};
  grpc_channel_args client_args = {1, &arg};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_server_pushback_disabled", &client_args, NULL);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  cq_verify(cqv);

  grpc_call_unref(s);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_details_init(&call_details);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(201));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(201), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 1;
  op->data.send_status_from_server.trailing_metadata = &pushback_md;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(202), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(202), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

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

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Tests retry cancellation.
static void test_retry_cancellation(grpc_end2end_test_config config,
                                    cancellation_mode mode) {
  grpc_call *c;
  grpc_call *s;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *request_payload_recv = NULL;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  char *peer;

  grpc_arg arg = {
      .key = GRPC_ARG_SERVICE_CONFIG,
      .type = GRPC_ARG_STRING,
      .value.string =
          "{\n"
          "  \"methodConfig\": [ {\n"
          "    \"name\": [\n"
          "      { \"service\": \"service\", \"method\": \"method\" }\n"
          "    ],\n"
          "    \"retryPolicy\": {\n"
          "      \"maxAttempts\": 3,\n"
          "      \"initialBackoff\": \"1s\",\n"
          "      \"maxBackoff\": \"120s\",\n"
          "      \"backoffMultiplier\": 1.6,\n"
          "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
          "    },\n"
          "    \"timeout\": \"5s\"\n"
          "  } ]\n"
          "}"};
  grpc_channel_args client_args = {1, &arg};
  char *name;
  gpr_asprintf(&name, "retry_cancellation/%s", mode.name);
  grpc_end2end_test_fixture f = begin_test(config, name, &client_args, NULL);
  gpr_free(name);

  cq_verifier *cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
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
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_ABORTED;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  cq_verify(cqv);

  grpc_call_unref(s);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_details_init(&call_details);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(201));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(201), true);
  cq_verify(cqv);

  GPR_ASSERT(GRPC_CALL_OK == mode.initiate_cancel(c, NULL));

  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  GPR_ASSERT(status == mode.expect_status);
  GPR_ASSERT(was_cancelled == 1);

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

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

void retry(grpc_end2end_test_config config) {
  GPR_ASSERT(config.feature_mask & FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL);
  test_retry_basic(config);
  test_retry_streaming(config);
  test_retry_streaming_succeeds_before_replay_finished(config);
  test_retry_streaming_after_commit(config);
  test_retry_too_many_attempts(config);
  test_retry_non_retriable_status(config);
  test_retry_exceeds_buffer_size_in_initial_batch(config);
  test_retry_exceeds_buffer_size_in_subsequent_batch(config);
  test_retry_recv_initial_metadata(config);
  test_retry_recv_message(config);
  test_retry_disabled(config);
  test_retry_throttled(config);
  test_retry_server_pushback_delay(config);
  test_retry_server_pushback_disabled(config);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(cancellation_modes); ++i) {
    test_retry_cancellation(config, cancellation_modes[i]);
  }
}

void retry_pre_init(void) {}
