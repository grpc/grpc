/*
 *
 * Copyright 2016 gRPC authors.
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

/** \file Verify that status ordering rules are obeyed.
    \ref doc/status_ordering.md */

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "test/core/end2end/cq_verifier.h"

static void* tag(intptr_t t) { return (void*)t; }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char* test_name,
                                            grpc_channel_args* client_args,
                                            grpc_channel_args* server_args,
                                            bool request_status_early) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "Running test: %s/%s/request_status_early=%s", test_name,
          config.name, request_status_early ? "true" : "false");
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
  grpc_server_shutdown_and_notify(f->server, f->shutdown_cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(f->shutdown_cq, tag(1000),
                                         grpc_timeout_seconds_to_deadline(5),
                                         nullptr)
                 .type == GRPC_OP_COMPLETE);
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
  grpc_completion_queue_destroy(f->shutdown_cq);
}

/* Client sends a request with payload, server reads then returns status. */
static void test(grpc_end2end_test_config config, bool request_status_early) {
  grpc_call* c;
  grpc_call* s;
  grpc_slice response_payload1_slice = grpc_slice_from_copied_string("hello");
  grpc_byte_buffer* response_payload1 =
      grpc_raw_byte_buffer_create(&response_payload1_slice, 1);
  grpc_slice response_payload2_slice = grpc_slice_from_copied_string("world");
  grpc_byte_buffer* response_payload2 =
      grpc_raw_byte_buffer_create(&response_payload2_slice, 1);
  grpc_end2end_test_fixture f =
      begin_test(config, "streaming_error_response", nullptr, nullptr,
                 request_status_early);
  cq_verifier* cqv = cq_verifier_create(f.cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer* response_payload1_recv = nullptr;
  grpc_byte_buffer* response_payload2_recv = nullptr;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(
      f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/foo"),
      get_host_override_slice("foo.test.google.fr:1234", config), deadline,
      nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload1_recv;
  op++;
  if (request_status_early) {
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
    op->data.recv_status_on_client.status = &status;
    op->data.recv_status_on_client.status_details = &details;
    op++;
  }
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                 f.server, &s, &call_details,
                                 &request_metadata_recv, f.cq, f.cq, tag(101)));
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload1;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  if (!request_status_early) {
    CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  }
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload2;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(103), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(103), 1);

  if (!request_status_early) {
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload2_recv;
    op++;
    error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(2), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);

    CQ_EXPECT_COMPLETION(cqv, tag(2), 1);
    cq_verify(cqv);
  }

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_FAILED_PRECONDITION;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(104), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(104), 1);
  if (request_status_early) {
    CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  }
  cq_verify(cqv);

  if (!request_status_early) {
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
    op->data.recv_status_on_client.status = &status;
    op->data.recv_status_on_client.status_details = &details;
    op++;
    error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(3), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);

    CQ_EXPECT_COMPLETION(cqv, tag(3), 1);
    cq_verify(cqv);

    GPR_ASSERT(response_payload1_recv != nullptr);
    GPR_ASSERT(response_payload2_recv != nullptr);
  }

  GPR_ASSERT(status == GRPC_STATUS_FAILED_PRECONDITION);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(was_cancelled == 1);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);

  grpc_byte_buffer_destroy(response_payload1);
  grpc_byte_buffer_destroy(response_payload2);
  grpc_byte_buffer_destroy(response_payload1_recv);
  grpc_byte_buffer_destroy(response_payload2_recv);

  end_test(&f);
  config.tear_down_data(&f);
}

void streaming_error_response(grpc_end2end_test_config config) {
  test(config, false);
  test(config, true);
}

void streaming_error_response_pre_init(void) {}
