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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "test/core/end2end/cq_verifier.h"

static void* tag(intptr_t t) { return (void*)t; }

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

static void simple_request_body(grpc_end2end_test_config config,
                                grpc_end2end_test_fixture f) {
  grpc_call* c;
  grpc_call* s;
  cq_verifier* cqv = cq_verifier_create(f.cq);
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

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
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
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
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
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
  GPR_ASSERT(was_cancelled == 1);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);
}

static void test_max_concurrent_streams(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f;
  grpc_arg server_arg;
  grpc_channel_args server_args;
  grpc_call* c1;
  grpc_call* c2;
  grpc_call* s1;
  grpc_call* s2;
  int live_call;
  gpr_timespec deadline;
  cq_verifier* cqv;
  grpc_event ev;
  grpc_call_details call_details;
  grpc_metadata_array request_metadata_recv;
  grpc_metadata_array initial_metadata_recv1;
  grpc_metadata_array trailing_metadata_recv1;
  grpc_metadata_array initial_metadata_recv2;
  grpc_metadata_array trailing_metadata_recv2;
  grpc_status_code status1;
  grpc_call_error error;
  grpc_slice details1;
  grpc_status_code status2;
  grpc_slice details2;
  grpc_op ops[6];
  grpc_op* op;
  int was_cancelled;
  int got_client_start;
  int got_server_start;

  server_arg.key = const_cast<char*>(GRPC_ARG_MAX_CONCURRENT_STREAMS);
  server_arg.type = GRPC_ARG_INTEGER;
  server_arg.value.integer = 1;

  server_args.num_args = 1;
  server_args.args = &server_arg;

  f = begin_test(config, "test_max_concurrent_streams", nullptr, &server_args);
  cqv = cq_verifier_create(f.cq);

  grpc_metadata_array_init(&request_metadata_recv);
  grpc_metadata_array_init(&initial_metadata_recv1);
  grpc_metadata_array_init(&trailing_metadata_recv1);
  grpc_metadata_array_init(&initial_metadata_recv2);
  grpc_metadata_array_init(&trailing_metadata_recv2);
  grpc_call_details_init(&call_details);

  /* perform a ping-pong to ensure that settings have had a chance to round
     trip */
  simple_request_body(config, f);
  /* perform another one to make sure that the one stream case still works */
  simple_request_body(config, f);

  /* start two requests - ensuring that the second is not accepted until
     the first completes */
  deadline = n_seconds_from_now(1000);
  c1 = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS,
                                f.cq, grpc_slice_from_static_string("/alpha"),
                                nullptr, deadline, nullptr);
  GPR_ASSERT(c1);
  c2 = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS,
                                f.cq, grpc_slice_from_static_string("/beta"),
                                nullptr, deadline, nullptr);
  GPR_ASSERT(c2);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                 f.server, &s1, &call_details,
                                 &request_metadata_recv, f.cq, f.cq, tag(101)));

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c1, ops, static_cast<size_t>(op - ops),
                                tag(301), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv1;
  op->data.recv_status_on_client.status = &status1;
  op->data.recv_status_on_client.status_details = &details1;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv1;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c1, ops, static_cast<size_t>(op - ops),
                                tag(302), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c2, ops, static_cast<size_t>(op - ops),
                                tag(401), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv2;
  op->data.recv_status_on_client.status = &status2;
  op->data.recv_status_on_client.status_details = &details2;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv1;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c2, ops, static_cast<size_t>(op - ops),
                                tag(402), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  got_client_start = 0;
  got_server_start = 0;
  live_call = -1;
  while (!got_client_start || !got_server_start) {
    ev = grpc_completion_queue_next(f.cq, grpc_timeout_seconds_to_deadline(3),
                                    nullptr);
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(ev.success);
    if (ev.tag == tag(101)) {
      GPR_ASSERT(!got_server_start);
      got_server_start = 1;
    } else {
      GPR_ASSERT(!got_client_start);
      GPR_ASSERT(ev.tag == tag(301) || ev.tag == tag(401));
      /* The /alpha or /beta calls started above could be invoked (but NOT
       * both);
       * check this here */
      /* We'll get tag 303 or 403, we want 300, 400 */
      live_call = (static_cast<int>((intptr_t)ev.tag)) - 1;
      got_client_start = 1;
    }
  }
  GPR_ASSERT(live_call == 300 || live_call == 400);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
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
  error = grpc_call_start_batch(s1, ops, static_cast<size_t>(op - ops),
                                tag(102), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(live_call + 2), 1);
  /* first request is finished, we should be able to start the second */
  live_call = (live_call == 300) ? 400 : 300;
  CQ_EXPECT_COMPLETION(cqv, tag(live_call + 1), 1);
  cq_verify(cqv);

  grpc_call_details_destroy(&call_details);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                 f.server, &s2, &call_details,
                                 &request_metadata_recv, f.cq, f.cq, tag(201)));
  CQ_EXPECT_COMPLETION(cqv, tag(201), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s2, ops, static_cast<size_t>(op - ops),
                                tag(202), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(live_call + 2), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(202), 1);
  cq_verify(cqv);

  cq_verifier_destroy(cqv);

  grpc_call_unref(c1);
  grpc_call_unref(s1);
  grpc_call_unref(c2);
  grpc_call_unref(s2);

  grpc_slice_unref(details1);
  grpc_slice_unref(details2);
  grpc_metadata_array_destroy(&initial_metadata_recv1);
  grpc_metadata_array_destroy(&trailing_metadata_recv1);
  grpc_metadata_array_destroy(&initial_metadata_recv2);
  grpc_metadata_array_destroy(&trailing_metadata_recv2);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_max_concurrent_streams_with_timeout_on_first(
    grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f;
  grpc_arg server_arg;
  grpc_channel_args server_args;
  grpc_call* c1;
  grpc_call* c2;
  grpc_call* s1;
  grpc_call* s2;
  cq_verifier* cqv;
  grpc_call_details call_details;
  grpc_metadata_array request_metadata_recv;
  grpc_metadata_array initial_metadata_recv1;
  grpc_metadata_array trailing_metadata_recv1;
  grpc_metadata_array initial_metadata_recv2;
  grpc_metadata_array trailing_metadata_recv2;
  grpc_status_code status1;
  grpc_call_error error;
  grpc_slice details1 = grpc_empty_slice();
  grpc_status_code status2;
  grpc_slice details2 = grpc_empty_slice();
  grpc_op ops[6];
  grpc_op* op;
  int was_cancelled;

  server_arg.key = const_cast<char*>(GRPC_ARG_MAX_CONCURRENT_STREAMS);
  server_arg.type = GRPC_ARG_INTEGER;
  server_arg.value.integer = 1;

  server_args.num_args = 1;
  server_args.args = &server_arg;

  f = begin_test(config, "test_max_concurrent_streams_with_timeout_on_first",
                 nullptr, &server_args);
  cqv = cq_verifier_create(f.cq);

  grpc_metadata_array_init(&request_metadata_recv);
  grpc_metadata_array_init(&initial_metadata_recv1);
  grpc_metadata_array_init(&trailing_metadata_recv1);
  grpc_metadata_array_init(&initial_metadata_recv2);
  grpc_metadata_array_init(&trailing_metadata_recv2);
  grpc_call_details_init(&call_details);

  /* perform a ping-pong to ensure that settings have had a chance to round
     trip */
  simple_request_body(config, f);
  /* perform another one to make sure that the one stream case still works */
  simple_request_body(config, f);

  /* start two requests - ensuring that the second is not accepted until
     the first completes */
  c1 = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS,
                                f.cq, grpc_slice_from_static_string("/alpha"),
                                nullptr, n_seconds_from_now(3), nullptr);
  GPR_ASSERT(c1);
  c2 = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS,
                                f.cq, grpc_slice_from_static_string("/beta"),
                                nullptr, n_seconds_from_now(1000), nullptr);
  GPR_ASSERT(c2);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                 f.server, &s1, &call_details,
                                 &request_metadata_recv, f.cq, f.cq, tag(101)));

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c1, ops, static_cast<size_t>(op - ops),
                                tag(301), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv1;
  op->data.recv_status_on_client.status = &status1;
  op->data.recv_status_on_client.status_details = &details1;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv1;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c1, ops, static_cast<size_t>(op - ops),
                                tag(302), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(301), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c2, ops, static_cast<size_t>(op - ops),
                                tag(401), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv2;
  op->data.recv_status_on_client.status = &status2;
  op->data.recv_status_on_client.status_details = &details2;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv2;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c2, ops, static_cast<size_t>(op - ops),
                                tag(402), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  grpc_call_details_destroy(&call_details);
  grpc_call_details_init(&call_details);
  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                 f.server, &s2, &call_details,
                                 &request_metadata_recv, f.cq, f.cq, tag(201)));

  CQ_EXPECT_COMPLETION(cqv, tag(302), 1);
  /* first request is finished, we should be able to start the second */
  CQ_EXPECT_COMPLETION(cqv, tag(401), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(201), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
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
  error = grpc_call_start_batch(s2, ops, static_cast<size_t>(op - ops),
                                tag(202), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(402), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(202), 1);
  cq_verify(cqv);

  cq_verifier_destroy(cqv);

  grpc_call_unref(c1);
  grpc_call_unref(s1);
  grpc_call_unref(c2);
  grpc_call_unref(s2);

  grpc_slice_unref(details1);
  grpc_slice_unref(details2);
  grpc_metadata_array_destroy(&initial_metadata_recv1);
  grpc_metadata_array_destroy(&trailing_metadata_recv1);
  grpc_metadata_array_destroy(&initial_metadata_recv2);
  grpc_metadata_array_destroy(&trailing_metadata_recv2);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_max_concurrent_streams_with_timeout_on_second(
    grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f;
  grpc_arg server_arg;
  grpc_channel_args server_args;
  grpc_call* c1;
  grpc_call* c2;
  grpc_call* s1;
  cq_verifier* cqv;
  grpc_call_details call_details;
  grpc_metadata_array request_metadata_recv;
  grpc_metadata_array initial_metadata_recv1;
  grpc_metadata_array trailing_metadata_recv1;
  grpc_metadata_array initial_metadata_recv2;
  grpc_metadata_array trailing_metadata_recv2;
  grpc_status_code status1;
  grpc_call_error error;
  grpc_slice details1 = grpc_empty_slice();
  grpc_status_code status2;
  grpc_slice details2 = grpc_empty_slice();
  grpc_op ops[6];
  grpc_op* op;
  int was_cancelled;

  server_arg.key = const_cast<char*>(GRPC_ARG_MAX_CONCURRENT_STREAMS);
  server_arg.type = GRPC_ARG_INTEGER;
  server_arg.value.integer = 1;

  server_args.num_args = 1;
  server_args.args = &server_arg;

  f = begin_test(config, "test_max_concurrent_streams_with_timeout_on_second",
                 nullptr, &server_args);
  cqv = cq_verifier_create(f.cq);

  grpc_metadata_array_init(&request_metadata_recv);
  grpc_metadata_array_init(&initial_metadata_recv1);
  grpc_metadata_array_init(&trailing_metadata_recv1);
  grpc_metadata_array_init(&initial_metadata_recv2);
  grpc_metadata_array_init(&trailing_metadata_recv2);
  grpc_call_details_init(&call_details);

  /* perform a ping-pong to ensure that settings have had a chance to round
     trip */
  simple_request_body(config, f);
  /* perform another one to make sure that the one stream case still works */
  simple_request_body(config, f);

  /* start two requests - ensuring that the second is not accepted until
     the first completes , and the second request will timeout in the
     concurrent_list */
  c1 = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS,
                                f.cq, grpc_slice_from_static_string("/alpha"),
                                nullptr, n_seconds_from_now(1000), nullptr);
  GPR_ASSERT(c1);
  c2 = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS,
                                f.cq, grpc_slice_from_static_string("/beta"),
                                nullptr, n_seconds_from_now(3), nullptr);
  GPR_ASSERT(c2);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                 f.server, &s1, &call_details,
                                 &request_metadata_recv, f.cq, f.cq, tag(101)));

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c1, ops, static_cast<size_t>(op - ops),
                                tag(301), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv1;
  op->data.recv_status_on_client.status = &status1;
  op->data.recv_status_on_client.status_details = &details1;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv1;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c1, ops, static_cast<size_t>(op - ops),
                                tag(302), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(301), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c2, ops, static_cast<size_t>(op - ops),
                                tag(401), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv2;
  op->data.recv_status_on_client.status = &status2;
  op->data.recv_status_on_client.status_details = &details2;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv2;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c2, ops, static_cast<size_t>(op - ops),
                                tag(402), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  /* the second request is time out*/
  CQ_EXPECT_COMPLETION(cqv, tag(401), 0);
  CQ_EXPECT_COMPLETION(cqv, tag(402), 1);
  cq_verify(cqv);

  /* second request is finished because of time out, so destroy the second call
   */
  grpc_call_unref(c2);

  /* now reply the first call */
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
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
  error = grpc_call_start_batch(s1, ops, static_cast<size_t>(op - ops),
                                tag(102), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(302), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  cq_verify(cqv);

  cq_verifier_destroy(cqv);

  grpc_call_unref(c1);
  grpc_call_unref(s1);

  grpc_slice_unref(details1);
  grpc_slice_unref(details2);
  grpc_metadata_array_destroy(&initial_metadata_recv1);
  grpc_metadata_array_destroy(&trailing_metadata_recv1);
  grpc_metadata_array_destroy(&initial_metadata_recv2);
  grpc_metadata_array_destroy(&trailing_metadata_recv2);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  end_test(&f);
  config.tear_down_data(&f);
}

void max_concurrent_streams(grpc_end2end_test_config config) {
  test_max_concurrent_streams_with_timeout_on_first(config);
  test_max_concurrent_streams_with_timeout_on_second(config);
  test_max_concurrent_streams(config);
}

void max_concurrent_streams_pre_init(void) {}
