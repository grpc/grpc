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
#include <unistd.h>

#include "src/core/support/string.h"
#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "test/core/end2end/cq_verifier.h"

static gpr_timespec n_seconds_time(int n) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(n);
}

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char *test_name,
                                            grpc_channel_args *client_args,
                                            grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "%s/%s", test_name, config.name);
  f = config.create_fixture(client_args, server_args);
  config.init_client(&f, client_args);
  config.init_server(&f, server_args);
  return f;
}

static void shutdown_server(grpc_end2end_test_fixture *f) {
  if (!f->server) return;
  grpc_server_shutdown(f->server);
  grpc_server_destroy(f->server);
  f->server = NULL;
}

static void shutdown_client(grpc_end2end_test_fixture *f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = NULL;
}

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event *ev;
  grpc_completion_type type;
  do {
    ev = grpc_completion_queue_next(cq, n_seconds_time(5));
    GPR_ASSERT(ev);
    type = ev->type;
    grpc_event_finish(ev);
  } while (type != GRPC_QUEUE_SHUTDOWN);
}

static void end_test(grpc_end2end_test_fixture *f) {
  shutdown_server(f);
  shutdown_client(f);

  grpc_completion_queue_shutdown(f->server_cq);
  drain_cq(f->server_cq);
  grpc_completion_queue_destroy(f->server_cq);
  grpc_completion_queue_shutdown(f->client_cq);
  drain_cq(f->client_cq);
  grpc_completion_queue_destroy(f->client_cq);
}

static void *tag(gpr_intptr t) { return (void *)t; }

static void test_body(grpc_end2end_test_fixture f) {
  grpc_call *c;
  grpc_call *s;
  gpr_timespec deadline = n_seconds_time(10);
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);

  c = grpc_channel_create_call_old(f.client, "/foo", "foo.test.google.fr",
                                   deadline);
  GPR_ASSERT(c);
  tag(1);
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_invoke_old(c, f.client_cq, tag(2), tag(3), 0));

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done_old(c, tag(4)));
  cq_expect_finish_accepted(v_client, tag(4), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call_old(f.server, tag(100)));
  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "foo.test.google.fr",
                           deadline, NULL);
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_server_accept_old(s, f.server_cq, tag(102)));
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_server_end_initial_metadata_old(s, 0));
  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_write_status_old(
                                 s, GRPC_STATUS_UNIMPLEMENTED, "xyz", tag(5)));
  cq_expect_finished_with_status(v_client, tag(3), GRPC_STATUS_UNIMPLEMENTED,
                                 "xyz", NULL);
  cq_verify(v_client);

  cq_expect_finish_accepted(v_server, tag(5), GRPC_OP_OK);
  cq_expect_finished(v_server, tag(102), NULL);
  cq_verify(v_server);
  grpc_call_destroy(c);
  grpc_call_destroy(s);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);
}

static void test_invoke_request_with_census(
    grpc_end2end_test_config config, const char *name,
    void (*body)(grpc_end2end_test_fixture f)) {
  char *fullname;
  grpc_end2end_test_fixture f;
  grpc_arg client_arg, server_arg;
  grpc_channel_args client_args, server_args;

  client_arg.type = GRPC_ARG_INTEGER;
  client_arg.key = GRPC_ARG_ENABLE_CENSUS;
  client_arg.value.integer = 1;

  client_args.num_args = 1;
  client_args.args = &client_arg;

  server_arg.type = GRPC_ARG_INTEGER;
  server_arg.key = GRPC_ARG_ENABLE_CENSUS;
  server_arg.value.integer = 1;
  server_args.num_args = 1;
  server_args.args = &server_arg;

  gpr_asprintf(&fullname, "%s/%s", __FUNCTION__, name);
  f = begin_test(config, fullname, &client_args, &server_args);
  body(f);
  end_test(&f);
  config.tear_down_data(&f);
  gpr_free(fullname);
}

void grpc_end2end_tests(grpc_end2end_test_config config) {
  test_invoke_request_with_census(config, "census_simple_request", test_body);
}
