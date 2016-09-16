/*
 *
 * Copyright 2016, Google Inc.
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
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/support/string.h"
#include "test/core/end2end/cq_verifier.h"

enum { TIMEOUT = 200000 };

static void *tag(intptr_t t) { return (void *)t; }

extern void gpr_default_log(gpr_log_func_args *args);

static void test_no_log(gpr_log_func_args *args) {
  char *message = NULL;
  gpr_asprintf(&message, "Unwanted log: %s", args->message);
  args->message = message;
  gpr_default_log(args);
  gpr_free(message);
  abort();
}

static void test_no_error_log(gpr_log_func_args *args) {
  if (args->severity == GPR_LOG_SEVERITY_ERROR) {
    test_no_log(args);
  }
}

static gpr_atm g_log_func = (gpr_atm)gpr_default_log;

static void log_dispatcher_func(gpr_log_func_args *args) {
  gpr_log_func log_func = (gpr_log_func)gpr_atm_no_barrier_load(&g_log_func);
  log_func(args);
}

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
  char *peer;

  c = grpc_channel_create_call(f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               "/foo", "foo.test.google.fr:1234", deadline,
                               NULL);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_free(peer);

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

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != NULL);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != NULL);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
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

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == strcmp(details, "xyz"));
  GPR_ASSERT(0 == strcmp(call_details.method, "/foo"));
  GPR_ASSERT(0 == strcmp(call_details.host, "foo.test.google.fr:1234"));
  GPR_ASSERT(0 == call_details.flags);
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

static void test_invoke_simple_request(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f;

  f = begin_test(config, "test_invoke_simple_request_with_no_error_logging",
                 NULL, NULL);
  simple_request_body(f);
  end_test(&f);
  config.tear_down_data(&f);
}

static void test_invoke_10_simple_requests(grpc_end2end_test_config config) {
  int i;
  grpc_end2end_test_fixture f =
      begin_test(config, "test_invoke_10_simple_requests_with_no_error_logging",
                 NULL, NULL);
  for (i = 0; i < 10; i++) {
    simple_request_body(f);
    gpr_log(GPR_INFO, "Passed simple request %d", i);
  }
  simple_request_body(f);
  end_test(&f);
  config.tear_down_data(&f);
}

static void test_no_error_logging_in_entire_process(
    grpc_end2end_test_config config) {
  int i;
  gpr_atm_no_barrier_store(&g_log_func, (gpr_atm)test_no_error_log);
  for (i = 0; i < 10; i++) {
    test_invoke_simple_request(config);
  }
  test_invoke_10_simple_requests(config);
  gpr_atm_no_barrier_store(&g_log_func, (gpr_atm)gpr_default_log);
}

static void test_no_logging_in_one_request(grpc_end2end_test_config config) {
  int i;
  grpc_end2end_test_fixture f =
      begin_test(config, "test_no_logging_in_last_request", NULL, NULL);
  for (i = 0; i < 10; i++) {
    simple_request_body(f);
  }
  gpr_atm_no_barrier_store(&g_log_func, (gpr_atm)test_no_log);
  simple_request_body(f);
  gpr_atm_no_barrier_store(&g_log_func, (gpr_atm)gpr_default_log);
  end_test(&f);
  config.tear_down_data(&f);
}

void no_logging(grpc_end2end_test_config config) {
  gpr_set_log_function(log_dispatcher_func);
  test_no_logging_in_one_request(config);
  test_no_error_logging_in_entire_process(config);
  gpr_set_log_function(gpr_default_log);
}

void no_logging_pre_init(void) {}
