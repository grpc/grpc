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

#include <limits.h>
#include <string.h>

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#include "test/core/end2end/cq_verifier.h"

#define MAX_CONNECTION_AGE_MS 500
#define MAX_CONNECTION_AGE_GRACE_MS 1000
#define MAX_CONNECTION_IDLE_MS 9999

#define MAX_CONNECTION_AGE_JITTER_MULTIPLIER 1.1
#define CALL_DEADLINE_S 10
/* The amount of time we wait for the connection to time out, but after it the
   connection should not use up its grace period. It should be a number between
   MAX_CONNECTION_AGE_MS and MAX_CONNECTION_AGE_MS +
   MAX_CONNECTION_AGE_GRACE_MS */
#define CQ_MAX_CONNECTION_AGE_WAIT_TIME_S 1
/* The amount of time we wait after the connection reaches its max age, it
   should be shorter than CALL_DEADLINE_S - CQ_MAX_CONNECTION_AGE_WAIT_TIME_S */
#define CQ_MAX_CONNECTION_AGE_GRACE_WAIT_TIME_S 2
/* The grace period for the test to observe the channel shutdown process */
#define IMMEDIATE_SHUTDOWN_GRACE_TIME_MS 3000

static void* tag(intptr_t t) { return (void*)t; }

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, grpc_timeout_seconds_to_deadline(5),
                                    nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture* f) {
  if (!f->server) return;
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

static void test_max_age_forcibly_close(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = config.create_fixture(nullptr, nullptr);
  cq_verifier* cqv = cq_verifier_create(f.cq);
  grpc_arg server_a[3];
  server_a[0].type = GRPC_ARG_INTEGER;
  server_a[0].key = const_cast<char*>(GRPC_ARG_MAX_CONNECTION_AGE_MS);
  server_a[0].value.integer = MAX_CONNECTION_AGE_MS;
  server_a[1].type = GRPC_ARG_INTEGER;
  server_a[1].key = const_cast<char*>(GRPC_ARG_MAX_CONNECTION_AGE_GRACE_MS);
  server_a[1].value.integer = MAX_CONNECTION_AGE_GRACE_MS;
  server_a[2].type = GRPC_ARG_INTEGER;
  server_a[2].key = const_cast<char*>(GRPC_ARG_MAX_CONNECTION_IDLE_MS);
  server_a[2].value.integer = MAX_CONNECTION_IDLE_MS;
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(server_a), server_a};

  config.init_client(&f, nullptr);
  config.init_server(&f, &server_args);

  grpc_call* c;
  grpc_call* s;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(CALL_DEADLINE_S);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

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
  op->data.send_initial_metadata.metadata = nullptr;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  gpr_timespec expect_shutdown_time = grpc_timeout_milliseconds_to_deadline(
      (int)(MAX_CONNECTION_AGE_MS * MAX_CONNECTION_AGE_JITTER_MULTIPLIER) +
      MAX_CONNECTION_AGE_GRACE_MS + IMMEDIATE_SHUTDOWN_GRACE_TIME_MS);

  /* Wait for the channel to reach its max age */
  cq_verify_empty_timeout(cqv, CQ_MAX_CONNECTION_AGE_WAIT_TIME_S);

  /* After the channel reaches its max age, we still do nothing here. And wait
     for it to use up its max age grace period. */
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  gpr_timespec channel_shutdown_time = gpr_now(GPR_CLOCK_MONOTONIC);
  GPR_ASSERT(gpr_time_cmp(channel_shutdown_time, expect_shutdown_time) < 0);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  cq_verify(cqv);

  grpc_server_shutdown_and_notify(f.server, f.cq, tag(0xdead));
  CQ_EXPECT_COMPLETION(cqv, tag(0xdead), true);
  cq_verify(cqv);

  grpc_call_unref(s);

  /* The connection should be closed immediately after the max age grace period,
     the in-progress RPC should fail. */
  GPR_ASSERT(status == GRPC_STATUS_INTERNAL);
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
  cq_verifier_destroy(cqv);
  end_test(&f);
  config.tear_down_data(&f);
}

static void test_max_age_gracefully_close(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = config.create_fixture(nullptr, nullptr);
  cq_verifier* cqv = cq_verifier_create(f.cq);
  grpc_arg server_a[3];
  server_a[0].type = GRPC_ARG_INTEGER;
  server_a[0].key = const_cast<char*>(GRPC_ARG_MAX_CONNECTION_AGE_MS);
  server_a[0].value.integer = MAX_CONNECTION_AGE_MS;
  server_a[1].type = GRPC_ARG_INTEGER;
  server_a[1].key = const_cast<char*>(GRPC_ARG_MAX_CONNECTION_AGE_GRACE_MS);
  server_a[1].value.integer = INT_MAX;
  server_a[2].type = GRPC_ARG_INTEGER;
  server_a[2].key = const_cast<char*>(GRPC_ARG_MAX_CONNECTION_IDLE_MS);
  server_a[2].value.integer = MAX_CONNECTION_IDLE_MS;
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(server_a), server_a};

  config.init_client(&f, nullptr);
  config.init_server(&f, &server_args);

  grpc_call* c;
  grpc_call* s;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(CALL_DEADLINE_S);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

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
  op->data.send_initial_metadata.metadata = nullptr;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), true);
  cq_verify(cqv);

  /* Wait for the channel to reach its max age */
  cq_verify_empty_timeout(cqv, CQ_MAX_CONNECTION_AGE_WAIT_TIME_S);

  /* The connection is shutting down gracefully. In-progress rpc should not be
     closed, hence the completion queue should see nothing here. */
  cq_verify_empty_timeout(cqv, CQ_MAX_CONNECTION_AGE_GRACE_WAIT_TIME_S);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), true);
  CQ_EXPECT_COMPLETION(cqv, tag(1), true);
  cq_verify(cqv);

  grpc_server_shutdown_and_notify(f.server, f.cq, tag(0xdead));
  CQ_EXPECT_COMPLETION(cqv, tag(0xdead), true);
  cq_verify(cqv);

  grpc_call_unref(s);

  /* The connection is closed gracefully with goaway, the rpc should still be
     completed. */
  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
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
  cq_verifier_destroy(cqv);
  end_test(&f);
  config.tear_down_data(&f);
}

void max_connection_age(grpc_end2end_test_config config) {
  test_max_age_forcibly_close(config);
  test_max_age_gracefully_close(config);
}

void max_connection_age_pre_init(void) {}
