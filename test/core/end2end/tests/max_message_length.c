/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "test/core/end2end/cq_verifier.h"

static void *tag(intptr_t t) { return (void *)t; }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char *test_name,
                                            grpc_channel_args *client_args,
                                            grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "%s/%s", test_name, config.name);
  // We intentionally do not pass the client and server args to
  // create_fixture(), since we don't want the limit enforced on the
  // proxy, only on the backend server.
  f = config.create_fixture(NULL, NULL);
  config.init_server(&f, server_args);
  config.init_client(&f, client_args);
  return f;
}

static gpr_timespec n_seconds_time(int n) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(n);
}

static gpr_timespec five_seconds_time(void) { return n_seconds_time(5); }

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_time(), NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture *f) {
  if (!f->server) return;
  grpc_server_shutdown_and_notify(f->server, f->cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(
                 f->cq, tag(1000), GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5), NULL)
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
}

// Test with request larger than the limit.
// If send_limit is true, applies send limit on client; otherwise, applies
// recv limit on server.
static void test_max_message_length_on_request(grpc_end2end_test_config config,
                                               bool send_limit) {
  gpr_log(GPR_INFO, "testing request with send_limit=%d", send_limit);

  grpc_end2end_test_fixture f;
  grpc_arg channel_arg;
  grpc_channel_args channel_args;
  grpc_call *c = NULL;
  grpc_call *s = NULL;
  cq_verifier *cqv;
  grpc_op ops[6];
  grpc_op *op;
  gpr_slice request_payload_slice = gpr_slice_from_copied_string("hello world");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *recv_payload = NULL;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  char *details = NULL;
  size_t details_capacity = 0;
  int was_cancelled = 2;

  channel_arg.key = send_limit ? GRPC_ARG_MAX_SEND_MESSAGE_LENGTH
                               : GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH;
  channel_arg.type = GRPC_ARG_INTEGER;
  channel_arg.value.integer = 5;

  channel_args.num_args = 1;
  channel_args.args = &channel_arg;

  f = begin_test(config, "test_max_message_length",
                 send_limit ? &channel_args : NULL,
                 send_limit ? NULL : &channel_args);
  cqv = cq_verifier_create(f.cq);

  c = grpc_channel_create_call(f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               "/foo", "foo.test.google.fr:1234",
                               gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message = request_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  if (send_limit) {
    CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
    cq_verify(cqv);
    goto done;
  }

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message = &recv_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(0 == strcmp(call_details.method, "/foo"));
  GPR_ASSERT(0 == strcmp(call_details.host, "foo.test.google.fr:1234"));
  GPR_ASSERT(was_cancelled == 1);

done:
  GPR_ASSERT(status == GRPC_STATUS_INVALID_ARGUMENT);
  GPR_ASSERT(strcmp(details,
                    send_limit
                        ? "Sent message larger than max (11 vs. 5)"
                        : "Received message larger than max (11 vs. 5)") == 0);

  gpr_free(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(recv_payload);

  grpc_call_destroy(c);
  if (s != NULL) grpc_call_destroy(s);

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

// Test with response larger than the limit.
// If send_limit is true, applies send limit on server; otherwise, applies
// recv limit on client.
static void test_max_message_length_on_response(grpc_end2end_test_config config,
                                                bool send_limit) {
  gpr_log(GPR_INFO, "testing response with send_limit=%d", send_limit);

  grpc_end2end_test_fixture f;
  grpc_arg channel_arg;
  grpc_channel_args channel_args;
  grpc_call *c = NULL;
  grpc_call *s = NULL;
  cq_verifier *cqv;
  grpc_op ops[6];
  grpc_op *op;
  gpr_slice response_payload_slice =
      gpr_slice_from_copied_string("hello world");
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *recv_payload = NULL;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  char *details = NULL;
  size_t details_capacity = 0;
  int was_cancelled = 2;

  channel_arg.key = send_limit ? GRPC_ARG_MAX_SEND_MESSAGE_LENGTH
                               : GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH;
  channel_arg.type = GRPC_ARG_INTEGER;
  channel_arg.value.integer = 5;

  channel_args.num_args = 1;
  channel_args.args = &channel_arg;

  f = begin_test(config, "test_max_message_length",
                 send_limit ? NULL : &channel_args,
                 send_limit ? &channel_args : NULL);
  cqv = cq_verifier_create(f.cq);

  c = grpc_channel_create_call(f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               "/foo", "foo.test.google.fr:1234",
                               gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message = &recv_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message = response_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->data.send_status_from_server.status_details = "xyz";
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(0 == strcmp(call_details.method, "/foo"));
  GPR_ASSERT(0 == strcmp(call_details.host, "foo.test.google.fr:1234"));
  GPR_ASSERT(was_cancelled == 0);

  GPR_ASSERT(status == GRPC_STATUS_INVALID_ARGUMENT);
  GPR_ASSERT(strcmp(details,
                    send_limit
                        ? "Sent message larger than max (11 vs. 5)"
                        : "Received message larger than max (11 vs. 5)") == 0);

  gpr_free(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(recv_payload);

  grpc_call_destroy(c);
  if (s != NULL) grpc_call_destroy(s);

  cq_verifier_destroy(cqv);

  end_test(&f);
  config.tear_down_data(&f);
}

void max_message_length(grpc_end2end_test_config config) {
  test_max_message_length_on_request(config, false /* send_limit */);
  test_max_message_length_on_request(config, true /* send_limit */);
  test_max_message_length_on_response(config, false /* send_limit */);
  test_max_message_length_on_response(config, true /* send_limit */);
}

void max_message_length_pre_init(void) {}
