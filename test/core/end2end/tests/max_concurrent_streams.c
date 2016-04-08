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

enum { TIMEOUT = 200000 };

static void *tag(intptr_t t) { return (void *)t; }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char *test_name,
                                            grpc_channel_args *client_args,
                                            grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "%s/%s", test_name, config.name);
  f = config.create_fixture(client_args, server_args);
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

static void simple_request_body(grpc_end2end_test_fixture f) {
  grpc_call *c;
  grpc_call *s;
  gpr_timespec deadline = five_seconds_time();
  cq_verifier *cqv = cq_verifier_create(f.cq);
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  char *details = NULL;
  size_t details_capacity = 0;
  int was_cancelled = 2;

  c = grpc_channel_create_call(f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               "/foo", "foo.test.google.fr:1234", deadline,
                               NULL);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

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
  cq_expect_completion(cqv, tag(101), 1);
  cq_verify(cqv);

  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  op->data.send_status_from_server.status_details = "xyz";
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cq_expect_completion(cqv, tag(102), 1);
  cq_expect_completion(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == strcmp(details, "xyz"));
  GPR_ASSERT(0 == strcmp(call_details.method, "/foo"));
  GPR_ASSERT(0 == strcmp(call_details.host, "foo.test.google.fr:1234"));
  GPR_ASSERT(was_cancelled == 1);

  gpr_free(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  cq_verifier_destroy(cqv);
}

static void test_max_concurrent_streams(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f;
  grpc_arg server_arg;
  grpc_channel_args server_args;
  grpc_call *c1;
  grpc_call *c2;
  grpc_call *s1;
  grpc_call *s2;
  int live_call;
  gpr_timespec deadline;
  cq_verifier *cqv;
  grpc_event ev;
  grpc_call_details call_details;
  grpc_metadata_array request_metadata_recv;
  grpc_metadata_array initial_metadata_recv1;
  grpc_metadata_array trailing_metadata_recv1;
  grpc_metadata_array initial_metadata_recv2;
  grpc_metadata_array trailing_metadata_recv2;
  grpc_status_code status1;
  grpc_call_error error;
  char *details1 = NULL;
  size_t details_capacity1 = 0;
  grpc_status_code status2;
  char *details2 = NULL;
  size_t details_capacity2 = 0;
  grpc_op ops[6];
  grpc_op *op;
  int was_cancelled;
  int got_client_start;
  int got_server_start;

  server_arg.key = GRPC_ARG_MAX_CONCURRENT_STREAMS;
  server_arg.type = GRPC_ARG_INTEGER;
  server_arg.value.integer = 1;

  server_args.num_args = 1;
  server_args.args = &server_arg;

  f = begin_test(config, "test_max_concurrent_streams", NULL, &server_args);
  cqv = cq_verifier_create(f.cq);

  grpc_metadata_array_init(&request_metadata_recv);
  grpc_metadata_array_init(&initial_metadata_recv1);
  grpc_metadata_array_init(&trailing_metadata_recv1);
  grpc_metadata_array_init(&initial_metadata_recv2);
  grpc_metadata_array_init(&trailing_metadata_recv2);
  grpc_call_details_init(&call_details);

  /* perform a ping-pong to ensure that settings have had a chance to round
     trip */
  simple_request_body(f);
  /* perform another one to make sure that the one stream case still works */
  simple_request_body(f);

  /* start two requests - ensuring that the second is not accepted until
     the first completes */
  deadline = n_seconds_time(1000);
  c1 = grpc_channel_create_call(f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
                                "/alpha", "foo.test.google.fr:1234", deadline,
                                NULL);
  GPR_ASSERT(c1);
  c2 = grpc_channel_create_call(f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
                                "/beta", "foo.test.google.fr:1234", deadline,
                                NULL);
  GPR_ASSERT(c2);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                 f.server, &s1, &call_details,
                                 &request_metadata_recv, f.cq, f.cq, tag(101)));

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
  error = grpc_call_start_batch(c1, ops, (size_t)(op - ops), tag(301), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv1;
  op->data.recv_status_on_client.status = &status1;
  op->data.recv_status_on_client.status_details = &details1;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity1;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata = &initial_metadata_recv1;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c1, ops, (size_t)(op - ops), tag(302), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

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
  error = grpc_call_start_batch(c2, ops, (size_t)(op - ops), tag(401), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv2;
  op->data.recv_status_on_client.status = &status2;
  op->data.recv_status_on_client.status_details = &details2;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity2;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata = &initial_metadata_recv1;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c2, ops, (size_t)(op - ops), tag(402), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  got_client_start = 0;
  got_server_start = 0;
  live_call = -1;
  while (!got_client_start || !got_server_start) {
    ev = grpc_completion_queue_next(f.cq, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(3),
                                    NULL);
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
      live_call = ((int)(intptr_t)ev.tag) - 1;
      got_client_start = 1;
    }
  }
  GPR_ASSERT(live_call == 300 || live_call == 400);

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
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  op->data.send_status_from_server.status_details = "xyz";
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s1, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cq_expect_completion(cqv, tag(102), 1);
  cq_expect_completion(cqv, tag(live_call + 2), 1);
  /* first request is finished, we should be able to start the second */
  live_call = (live_call == 300) ? 400 : 300;
  cq_expect_completion(cqv, tag(live_call + 1), 1);
  cq_verify(cqv);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                 f.server, &s2, &call_details,
                                 &request_metadata_recv, f.cq, f.cq, tag(201)));
  cq_expect_completion(cqv, tag(201), 1);
  cq_verify(cqv);

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
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  op->data.send_status_from_server.status_details = "xyz";
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s2, ops, (size_t)(op - ops), tag(202), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cq_expect_completion(cqv, tag(live_call + 2), 1);
  cq_expect_completion(cqv, tag(202), 1);
  cq_verify(cqv);

  cq_verifier_destroy(cqv);

  grpc_call_destroy(c1);
  grpc_call_destroy(s1);
  grpc_call_destroy(c2);
  grpc_call_destroy(s2);

  gpr_free(details1);
  gpr_free(details2);
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
  test_max_concurrent_streams(config);
}

void max_concurrent_streams_pre_init(void) {}
