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

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "test/core/end2end/cq_verifier.h"

enum { TIMEOUT = 200000 };

static void *tag(gpr_intptr t) { return (void *)t; }

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

static gpr_timespec n_seconds_time(int n) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(n);
}

static gpr_timespec five_seconds_time(void) { return n_seconds_time(5); }

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event *ev;
  grpc_completion_type type;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_time());
    GPR_ASSERT(ev);
    type = ev->type;
    grpc_event_finish(ev);
  } while (type != GRPC_QUEUE_SHUTDOWN);
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

/* Request/response with metadata and payload.*/
static void test_request_response_with_metadata_and_payload(
    grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  gpr_slice request_payload_slice = gpr_slice_from_copied_string("hello world");
  gpr_slice response_payload_slice = gpr_slice_from_copied_string("hello you");
  grpc_byte_buffer *request_payload =
      grpc_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_byte_buffer_create(&response_payload_slice, 1);
  gpr_timespec deadline = five_seconds_time();
  grpc_metadata meta1 = {"key1", "val1", 4};
  grpc_metadata meta2 = {"key2", "val2", 4};
  grpc_metadata meta3 = {"key3", "val3", 4};
  grpc_metadata meta4 = {"key4", "val4", 4};
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call_old(f.server, tag(100)));

  /* byte buffer holds the slice, we can unref it already */
  gpr_slice_unref(request_payload_slice);
  gpr_slice_unref(response_payload_slice);

  c = grpc_channel_create_call_old(f.client, "/foo", "foo.test.google.fr",
                                   deadline);
  GPR_ASSERT(c);

  /* add multiple metadata */
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_add_metadata_old(c, &meta1, 0));
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_add_metadata_old(c, &meta2, 0));

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_invoke_old(c, f.client_cq, tag(2), tag(3), 0));

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_old(c, request_payload, tag(4), 0));
  /* destroy byte buffer early to ensure async code keeps track of its contents
     correctly */
  grpc_byte_buffer_destroy(request_payload);
  cq_expect_write_accepted(v_client, tag(4), GRPC_OP_OK);
  cq_verify(v_client);

  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "foo.test.google.fr",
                           deadline, "key1", "val1", "key2", "val2", NULL);
  cq_verify(v_server);

  grpc_call_server_accept_old(s, f.server_cq, tag(102));

  /* add multiple metadata */
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_add_metadata_old(s, &meta3, 0));
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_add_metadata_old(s, &meta4, 0));

  grpc_call_server_end_initial_metadata_old(s, 0);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read_old(s, tag(5)));
  cq_expect_read(v_server, tag(5), gpr_slice_from_copied_string("hello world"));
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_old(s, response_payload, tag(6), 0));
  /* destroy byte buffer early to ensure async code keeps track of its contents
     correctly */
  grpc_byte_buffer_destroy(response_payload);
  cq_expect_write_accepted(v_server, tag(6), GRPC_OP_OK);
  cq_verify(v_server);

  /* fetch metadata.. */
  cq_expect_client_metadata_read(v_client, tag(2), "key3", "val3", "key4",
                                 "val4", NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read_old(c, tag(7)));
  cq_expect_read(v_client, tag(7), gpr_slice_from_copied_string("hello you"));
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done_old(c, tag(8)));
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_write_status_old(
                                 s, GRPC_STATUS_UNIMPLEMENTED, "xyz", tag(9)));

  cq_expect_finish_accepted(v_client, tag(8), GRPC_OP_OK);
  cq_expect_finished_with_status(v_client, tag(3), GRPC_STATUS_UNIMPLEMENTED,
                                 "xyz", NULL);
  cq_verify(v_client);

  cq_expect_finish_accepted(v_server, tag(9), GRPC_OP_OK);
  cq_expect_finished(v_server, tag(102), NULL);
  cq_verify(v_server);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  end_test(&f);
  config.tear_down_data(&f);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);
}

void grpc_end2end_tests(grpc_end2end_test_config config) {
  test_request_response_with_metadata_and_payload(config);
}
