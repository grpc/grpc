/*
 *
 * Copyright 2014, Google Inc.
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
#include <grpc/support/string.h>
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
  return gpr_time_add(gpr_now(), gpr_time_from_micros(GPR_US_PER_SEC * n));
}

static gpr_timespec five_seconds_time() { return n_seconds_time(5); }

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

static void test_no_op(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  end_test(&f);
  config.tear_down_data(&f);
}

static void simple_request_body(grpc_end2end_test_fixture f) {
  grpc_call *c;
  grpc_call *s;
  grpc_status send_status = {GRPC_STATUS_UNIMPLEMENTED, "xyz"};
  gpr_timespec deadline = five_seconds_time();
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c, tag(4)));
  cq_expect_finish_accepted(v_client, tag(4), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));
  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_accept(s, f.server_cq, tag(102), 0));
  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_status(s, send_status, tag(5)));
  cq_expect_finished_with_status(v_client, tag(3), send_status, NULL);
  cq_verify(v_client);

  cq_expect_finish_accepted(v_server, tag(5), GRPC_OP_OK);
  cq_verify(v_server);
  cq_expect_finished(v_server, tag(102), NULL);
  cq_verify(v_server);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);
}

/* an alternative ordering of the simple request body */
static void simple_request_body2(grpc_end2end_test_fixture f) {
  grpc_call *c;
  grpc_call *s;
  grpc_status send_status = {GRPC_STATUS_UNIMPLEMENTED, "xyz"};
  gpr_timespec deadline = five_seconds_time();
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c, tag(4)));
  cq_expect_finish_accepted(v_client, tag(4), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));
  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_accept(s, f.server_cq, tag(102), 0));

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_status(s, send_status, tag(5)));
  cq_expect_finish_accepted(v_server, tag(5), GRPC_OP_OK);
  cq_verify(v_server);

  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  cq_expect_finished_with_status(v_client, tag(3), send_status, NULL);
  cq_verify(v_client);

  cq_expect_finished(v_server, tag(102), NULL);
  cq_verify(v_server);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);
}

static void test_invoke_simple_request(
    grpc_end2end_test_config config, const char *name,
    void (*body)(grpc_end2end_test_fixture f)) {
  char *fullname;
  grpc_end2end_test_fixture f;

  gpr_asprintf(&fullname, "%s/%s", __FUNCTION__, name);
  f = begin_test(config, fullname, NULL, NULL);
  body(f);
  end_test(&f);
  config.tear_down_data(&f);
  gpr_free(fullname);
}

static void test_invoke_10_simple_requests(grpc_end2end_test_config config) {
  int i;
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  for (i = 0; i < 10; i++) {
    simple_request_body(f);
    gpr_log(GPR_INFO, "Passed simple request %d", i);
  }
  end_test(&f);
  config.tear_down_data(&f);
}

static void simple_delayed_request_body(grpc_end2end_test_config config,
                                        grpc_end2end_test_fixture *f,
                                        grpc_channel_args *client_args,
                                        grpc_channel_args *server_args,
                                        long delay_us) {
  grpc_call *c;
  grpc_call *s;
  grpc_status send_status = {GRPC_STATUS_UNIMPLEMENTED, "xyz"};
  gpr_timespec deadline = five_seconds_time();
  cq_verifier *v_client = cq_verifier_create(f->client_cq);
  cq_verifier *v_server = cq_verifier_create(f->server_cq);

  config.init_client(f, client_args);

  c = grpc_channel_create_call(f->client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_invoke(c, f->client_cq, tag(1),
                                                    tag(2), tag(3), 0));
  gpr_sleep_until(gpr_time_add(gpr_now(), gpr_time_from_micros(delay_us)));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);

  config.init_server(f, server_args);

  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c, tag(4)));
  cq_expect_finish_accepted(v_client, tag(4), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f->server, tag(100)));
  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_accept(s, f->server_cq, tag(102), 0));
  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_status(s, send_status, tag(5)));
  cq_expect_finished_with_status(v_client, tag(3), send_status, NULL);
  cq_verify(v_client);

  cq_expect_finish_accepted(v_server, tag(5), GRPC_OP_OK);
  cq_expect_finished(v_server, tag(102), NULL);
  cq_verify(v_server);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);
}

static void test_simple_delayed_request_short(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f;

  gpr_log(GPR_INFO, "%s/%s", __FUNCTION__, config.name);
  f = config.create_fixture(NULL, NULL);
  simple_delayed_request_body(config, &f, NULL, NULL, 100000);
  end_test(&f);
  config.tear_down_data(&f);
}

static void test_simple_delayed_request_long(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f;

  gpr_log(GPR_INFO, "%s/%s", __FUNCTION__, config.name);
  f = config.create_fixture(NULL, NULL);
  /* This timeout should be longer than a single retry */
  simple_delayed_request_body(config, &f, NULL, NULL, 1500000);
  end_test(&f);
  config.tear_down_data(&f);
}

/* Client sends a request with payload, server reads then returns status. */
static void test_invoke_request_with_payload(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_status send_status = {GRPC_STATUS_UNIMPLEMENTED, "xyz"};
  gpr_slice payload_slice = gpr_slice_from_copied_string("hello world");
  grpc_byte_buffer *payload = grpc_byte_buffer_create(&payload_slice, 1);
  gpr_timespec deadline = five_seconds_time();
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);

  /* byte buffer holds the slice, we can unref it already */
  gpr_slice_unref(payload_slice);

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_write(c, payload, tag(4), 0));
  /* destroy byte buffer early to ensure async code keeps track of its contents
     correctly */
  grpc_byte_buffer_destroy(payload);
  cq_expect_write_accepted(v_client, tag(4), GRPC_OP_OK);
  cq_verify(v_client);

  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);

  grpc_call_accept(s, f.server_cq, tag(102), 0);
  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read(s, tag(4)));
  cq_expect_read(v_server, tag(4), gpr_slice_from_copied_string("hello world"));

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c, tag(5)));
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_status(s, send_status, tag(6)));
  cq_expect_finish_accepted(v_client, tag(5), GRPC_OP_OK);
  cq_expect_finished_with_status(v_client, tag(3), send_status, NULL);
  cq_verify(v_client);

  cq_expect_finish_accepted(v_server, tag(6), GRPC_OP_OK);
  cq_expect_finished(v_server, tag(102), NULL);
  cq_verify(v_server);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  end_test(&f);
  config.tear_down_data(&f);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);
}

/* test the case when there is a pending message at the client side,
   writes_done should not return a status without a start_read.
   Note: this test will last for 3s. Do not run in a loop. */
static void test_writes_done_hangs_with_pending_read(
    grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_status send_status = {GRPC_STATUS_UNIMPLEMENTED, "xyz"};
  gpr_slice request_payload_slice = gpr_slice_from_copied_string("hello world");
  gpr_slice response_payload_slice = gpr_slice_from_copied_string("hello you");
  grpc_byte_buffer *request_payload =
      grpc_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_byte_buffer_create(&response_payload_slice, 1);
  gpr_timespec deadline = five_seconds_time();
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);

  /* byte buffer holds the slice, we can unref it already */
  gpr_slice_unref(request_payload_slice);
  gpr_slice_unref(response_payload_slice);

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write(c, request_payload, tag(4), 0));
  /* destroy byte buffer early to ensure async code keeps track of its contents
     correctly */
  grpc_byte_buffer_destroy(request_payload);
  cq_expect_write_accepted(v_client, tag(4), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));
  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);

  grpc_call_accept(s, f.server_cq, tag(102), 0);
  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read(s, tag(5)));
  cq_expect_read(v_server, tag(5), gpr_slice_from_copied_string("hello world"));
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write(s, response_payload, tag(6), 0));
  /* destroy byte buffer early to ensure async code keeps track of its contents
     correctly */
  grpc_byte_buffer_destroy(response_payload);
  cq_expect_write_accepted(v_server, tag(6), GRPC_OP_OK);
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c, tag(6)));
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_status(s, send_status, tag(7)));

  cq_expect_finish_accepted(v_client, tag(6), GRPC_OP_OK);
  cq_verify(v_client);

  /* does not return status because there is a pending message to be read */
  cq_verify_empty(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read(c, tag(8)));
  cq_expect_read(v_client, tag(8), gpr_slice_from_copied_string("hello you"));
  cq_verify(v_client);

  cq_expect_finished_with_status(v_client, tag(3), send_status, NULL);
  cq_verify(v_client);

  cq_expect_finish_accepted(v_server, tag(7), GRPC_OP_OK);
  cq_expect_finished(v_server, tag(102), NULL);
  cq_verify(v_server);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  end_test(&f);
  config.tear_down_data(&f);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);
}

static void request_response_with_payload(grpc_end2end_test_fixture f) {
  grpc_call *c;
  grpc_call *s;
  grpc_status send_status = {GRPC_STATUS_UNIMPLEMENTED, "xyz"};
  gpr_slice request_payload_slice = gpr_slice_from_copied_string("hello world");
  gpr_slice response_payload_slice = gpr_slice_from_copied_string("hello you");
  grpc_byte_buffer *request_payload =
      grpc_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *response_payload =
      grpc_byte_buffer_create(&response_payload_slice, 1);
  gpr_timespec deadline = five_seconds_time();
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);

  /* byte buffer holds the slice, we can unref it already */
  gpr_slice_unref(request_payload_slice);
  gpr_slice_unref(response_payload_slice);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write(c, request_payload, tag(4), 0));
  /* destroy byte buffer early to ensure async code keeps track of its contents
     correctly */
  grpc_byte_buffer_destroy(request_payload);
  cq_expect_write_accepted(v_client, tag(4), GRPC_OP_OK);
  cq_verify(v_client);

  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);

  grpc_call_accept(s, f.server_cq, tag(102), 0);
  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read(s, tag(5)));
  cq_expect_read(v_server, tag(5), gpr_slice_from_copied_string("hello world"));
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write(s, response_payload, tag(6), 0));
  /* destroy byte buffer early to ensure async code keeps track of its contents
     correctly */
  grpc_byte_buffer_destroy(response_payload);
  cq_expect_write_accepted(v_server, tag(6), GRPC_OP_OK);
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read(c, tag(7)));
  cq_expect_read(v_client, tag(7), gpr_slice_from_copied_string("hello you"));

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c, tag(8)));
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_status(s, send_status, tag(9)));

  cq_expect_finish_accepted(v_client, tag(8), GRPC_OP_OK);
  cq_expect_finished_with_status(v_client, tag(3), send_status, NULL);
  cq_verify(v_client);

  cq_expect_finish_accepted(v_server, tag(9), GRPC_OP_OK);
  cq_expect_finished(v_server, tag(102), NULL);
  cq_verify(v_server);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);
}

/* Client sends a request with payload, server reads then returns a response
   payload and status. */
static void test_invoke_request_response_with_payload(
    grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  request_response_with_payload(f);
  end_test(&f);
  config.tear_down_data(&f);
}

static void test_invoke_10_request_response_with_payload(
    grpc_end2end_test_config config) {
  int i;
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  for (i = 0; i < 10; i++) {
    request_response_with_payload(f);
  }
  end_test(&f);
  config.tear_down_data(&f);
}

/* allow cancellation by either grpc_call_cancel, or by wait_for_deadline (which
 * does nothing) */
typedef grpc_call_error (*canceller)(grpc_call *call);

static grpc_call_error wait_for_deadline(grpc_call *call) {
  return GRPC_CALL_OK;
}

/* Cancel and do nothing */
static void test_cancel_in_a_vacuum(grpc_end2end_test_config config,
                                    canceller call_cancel) {
  grpc_call *c;
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  gpr_timespec deadline = five_seconds_time();
  cq_verifier *v_client = cq_verifier_create(f.client_cq);

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK == call_cancel(c));

  grpc_call_destroy(c);

  cq_verifier_destroy(v_client);
  end_test(&f);
  config.tear_down_data(&f);
}

/* Cancel before invoke */
static void test_cancel_before_invoke(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  gpr_timespec deadline = five_seconds_time();
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  grpc_status chk_status = {GRPC_STATUS_CANCELLED, NULL};

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_cancel(c));

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_ERROR);
  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_expect_finished_with_status(v_client, tag(3), chk_status, NULL);
  cq_verify(v_client);

  grpc_call_destroy(c);

  cq_verifier_destroy(v_client);
  end_test(&f);
  config.tear_down_data(&f);
}

/* Cancel after invoke, no payload */
static void test_cancel_after_invoke(grpc_end2end_test_config config,
                                     canceller call_cancel) {
  grpc_call *c;
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  gpr_timespec deadline = five_seconds_time();
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  grpc_status chk_status = {GRPC_STATUS_CANCELLED, NULL};

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == call_cancel(c));

  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_expect_finished_with_status(v_client, tag(3), chk_status, NULL);
  cq_verify(v_client);

  grpc_call_destroy(c);

  cq_verifier_destroy(v_client);
  end_test(&f);
  config.tear_down_data(&f);
}

/* Cancel after accept, no payload */
static void test_cancel_after_accept(grpc_end2end_test_config config,
                                     canceller call_cancel) {
  grpc_call *c;
  grpc_call *s;
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  gpr_timespec deadline = five_seconds_time();
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);
  grpc_status chk_status = {GRPC_STATUS_CANCELLED, NULL};

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));
  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_accept(s, f.server_cq, tag(102), 0));
  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == call_cancel(c));

  cq_expect_finished_with_status(v_client, tag(3), chk_status, NULL);
  cq_verify(v_client);

  cq_expect_finished_with_status(v_server, tag(102), chk_status, NULL);
  cq_verify(v_server);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);
  end_test(&f);
  config.tear_down_data(&f);
}

/* Cancel after accept with a writes closed, no payload */
static void test_cancel_after_accept_and_writes_closed(
    grpc_end2end_test_config config, canceller call_cancel) {
  grpc_call *c;
  grpc_call *s;
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  gpr_timespec deadline = five_seconds_time();
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);
  grpc_status chk_status = {GRPC_STATUS_CANCELLED, NULL};

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));
  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_accept(s, f.server_cq, tag(102), 0));
  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c, tag(4)));
  cq_expect_finish_accepted(v_client, tag(4), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read(s, tag(101)));
  cq_expect_empty_read(v_server, tag(101));
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK == call_cancel(c));

  cq_expect_finished_with_status(v_client, tag(3), chk_status, NULL);
  cq_verify(v_client);

  cq_expect_finished_with_status(v_server, tag(102), chk_status, NULL);
  cq_verify(v_server);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);
  end_test(&f);
  config.tear_down_data(&f);
}

/* Request/response with metadata and payload.*/
static void test_request_response_with_metadata_and_payload(
    grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_status send_status = {GRPC_STATUS_UNIMPLEMENTED, "xyz"};
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

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));

  /* byte buffer holds the slice, we can unref it already */
  gpr_slice_unref(request_payload_slice);
  gpr_slice_unref(response_payload_slice);

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  /* add multiple metadata */
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_add_metadata(c, &meta1, 0));
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_add_metadata(c, &meta2, 0));

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write(c, request_payload, tag(4), 0));
  /* destroy byte buffer early to ensure async code keeps track of its contents
     correctly */
  grpc_byte_buffer_destroy(request_payload);
  cq_expect_write_accepted(v_client, tag(4), GRPC_OP_OK);
  cq_verify(v_client);

  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, "key1", "val1", "key2", "val2", NULL);
  cq_verify(v_server);

  /* add multiple metadata */
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_add_metadata(s, &meta3, 0));
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_add_metadata(s, &meta4, 0));

  grpc_call_accept(s, f.server_cq, tag(102), 0);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read(s, tag(5)));
  cq_expect_read(v_server, tag(5), gpr_slice_from_copied_string("hello world"));
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write(s, response_payload, tag(6), 0));
  /* destroy byte buffer early to ensure async code keeps track of its contents
     correctly */
  grpc_byte_buffer_destroy(response_payload);
  cq_expect_write_accepted(v_server, tag(6), GRPC_OP_OK);
  cq_verify(v_server);

  /* fetch metadata.. */
  cq_expect_client_metadata_read(v_client, tag(2), "key3", "val3", "key4",
                                 "val4", NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read(c, tag(7)));
  cq_expect_read(v_client, tag(7), gpr_slice_from_copied_string("hello you"));
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c, tag(8)));
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_status(s, send_status, tag(9)));

  cq_expect_finish_accepted(v_client, tag(8), GRPC_OP_OK);
  cq_expect_finished_with_status(v_client, tag(3), send_status, NULL);
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

/* Request with a large amount of metadata.*/
static void test_request_with_large_metadata(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_status send_status = {GRPC_STATUS_OK, NULL};
  gpr_timespec deadline = five_seconds_time();
  grpc_metadata meta;
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);
  const int large_size = 64 * 1024;

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));

  meta.key = "key";
  meta.value = gpr_malloc(large_size + 1);
  memset(meta.value, 'a', large_size);
  meta.value[large_size] = 0;
  meta.value_length = large_size;

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  /* add the metadata */
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_add_metadata(c, &meta, 0));

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);
  cq_verify(v_client);

  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, "key", meta.value, NULL);
  cq_verify(v_server);

  grpc_call_accept(s, f.server_cq, tag(102), 0);

  /* fetch metadata.. */
  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c, tag(8)));
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_status(s, send_status, tag(9)));

  cq_expect_finish_accepted(v_client, tag(8), GRPC_OP_OK);
  cq_expect_finished_with_status(v_client, tag(3), send_status, NULL);
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

  gpr_free(meta.value);
}

/* Client pings and server pongs. Repeat messages rounds before finishing. */
static void test_pingpong_streaming(grpc_end2end_test_config config,
                                    int messages) {
  int i;
  grpc_call *c;
  grpc_call *s = NULL;
  grpc_status send_status = {GRPC_STATUS_UNIMPLEMENTED, "xyz"};
  gpr_slice request_payload_slice = gpr_slice_from_copied_string("hello world");
  gpr_slice response_payload_slice = gpr_slice_from_copied_string("hello you");
  grpc_byte_buffer *request_payload = NULL;
  grpc_byte_buffer *response_payload = NULL;
  gpr_timespec deadline = n_seconds_time(messages * 5);
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);

  gpr_log(GPR_INFO, "testing with %d message pairs.", messages);
  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));

  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);
  grpc_call_accept(s, f.server_cq, tag(102), 0);

  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  for (i = 0; i < messages; i++) {
    request_payload = grpc_byte_buffer_create(&request_payload_slice, 1);
    GPR_ASSERT(GRPC_CALL_OK ==
               grpc_call_start_write(c, request_payload, tag(2), 0));
    /* destroy byte buffer early to ensure async code keeps track of its
       contents
       correctly */
    grpc_byte_buffer_destroy(request_payload);
    cq_expect_write_accepted(v_client, tag(2), GRPC_OP_OK);
    cq_verify(v_client);

    GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read(s, tag(3)));
    cq_expect_read(v_server, tag(3),
                   gpr_slice_from_copied_string("hello world"));
    cq_verify(v_server);

    response_payload = grpc_byte_buffer_create(&response_payload_slice, 1);
    GPR_ASSERT(GRPC_CALL_OK ==
               grpc_call_start_write(s, response_payload, tag(4), 0));
    /* destroy byte buffer early to ensure async code keeps track of its
       contents
       correctly */
    grpc_byte_buffer_destroy(response_payload);
    cq_expect_write_accepted(v_server, tag(4), GRPC_OP_OK);
    cq_verify(v_server);

    GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read(c, tag(5)));
    cq_expect_read(v_client, tag(5), gpr_slice_from_copied_string("hello you"));
    cq_verify(v_client);
  }

  gpr_slice_unref(request_payload_slice);
  gpr_slice_unref(response_payload_slice);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c, tag(6)));
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_status(s, send_status, tag(7)));

  cq_expect_finish_accepted(v_client, tag(6), GRPC_OP_OK);
  cq_expect_finished_with_status(v_client, tag(3), send_status, NULL);
  cq_verify(v_client);

  cq_expect_finish_accepted(v_server, tag(7), GRPC_OP_OK);
  cq_expect_finished(v_server, tag(102), NULL);
  cq_verify(v_server);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  end_test(&f);
  config.tear_down_data(&f);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);
}

static void test_early_server_shutdown_finishes_tags(
    grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);
  grpc_call *s = (void *)1;

  /* upon shutdown, the server should finish all requested calls indicating
     no new call */
  grpc_server_request_call(f.server, tag(1000));
  grpc_server_shutdown(f.server);
  cq_expect_server_rpc_new(v_server, &s, tag(1000), NULL, NULL, gpr_inf_past,
                           NULL);
  cq_verify(v_server);
  GPR_ASSERT(s == NULL);

  end_test(&f);
  config.tear_down_data(&f);
  cq_verifier_destroy(v_server);
}

static void test_early_server_shutdown_finishes_inflight_calls(
    grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  grpc_call *c;
  grpc_call *s;
  grpc_status expect_status = {GRPC_STATUS_UNAVAILABLE, NULL};
  gpr_timespec deadline = five_seconds_time();
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c, tag(4)));
  cq_expect_finish_accepted(v_client, tag(4), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));
  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_accept(s, f.server_cq, tag(102), 0));
  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  /* shutdown and destroy the server */
  shutdown_server(&f);

  cq_expect_finished(v_server, tag(102), NULL);
  cq_verify(v_server);

  grpc_call_destroy(s);

  cq_expect_finished_with_status(v_client, tag(3), expect_status, NULL);
  cq_verify(v_client);

  grpc_call_destroy(c);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_max_concurrent_streams(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f;
  grpc_arg server_arg;
  grpc_channel_args server_args;
  grpc_call *c1;
  grpc_call *c2;
  grpc_call *s1;
  grpc_call *s2;
  gpr_timespec deadline;
  grpc_status send_status = {GRPC_STATUS_UNIMPLEMENTED, "xyz"};
  cq_verifier *v_client;
  cq_verifier *v_server;

  server_arg.key = GRPC_ARG_MAX_CONCURRENT_STREAMS;
  server_arg.type = GRPC_ARG_INTEGER;
  server_arg.value.integer = 1;

  server_args.num_args = 1;
  server_args.args = &server_arg;

  f = begin_test(config, __FUNCTION__, NULL, &server_args);
  v_client = cq_verifier_create(f.client_cq);
  v_server = cq_verifier_create(f.server_cq);

  /* perform a ping-pong to ensure that settings have had a chance to round
     trip */
  simple_request_body(f);
  /* perform another one to make sure that the one stream case still works */
  simple_request_body(f);

  /* start two requests - ensuring that the second is not accepted until
     the first completes */
  deadline = five_seconds_time();
  c1 = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c1);
  c2 = grpc_channel_create_call(f.client, "/bar", "test.google.com", deadline);
  GPR_ASSERT(c1);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_invoke(c1, f.client_cq, tag(300),
                                                    tag(301), tag(302), 0));
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_invoke(c2, f.client_cq, tag(400),
                                                    tag(401), tag(402), 0));
  cq_expect_invoke_accepted(v_client, tag(300), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c1, tag(303)));
  cq_expect_finish_accepted(v_client, tag(303), GRPC_OP_OK);
  cq_verify(v_client);

  cq_expect_server_rpc_new(v_server, &s1, tag(100), "/foo", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_accept(s1, f.server_cq, tag(102), 0));
  cq_expect_client_metadata_read(v_client, tag(301), NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_status(s1, send_status, tag(103)));
  cq_expect_finish_accepted(v_server, tag(103), GRPC_OP_OK);
  cq_expect_finished(v_server, tag(102), NULL);
  cq_verify(v_server);

  /* first request is finished, we should be able to start the second */
  cq_expect_finished_with_status(v_client, tag(302), send_status, NULL);
  cq_expect_invoke_accepted(v_client, tag(400), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c2, tag(403)));
  cq_expect_finish_accepted(v_client, tag(403), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(200)));
  cq_expect_server_rpc_new(v_server, &s2, tag(200), "/bar", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_accept(s2, f.server_cq, tag(202), 0));
  cq_expect_client_metadata_read(v_client, tag(401), NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_status(s2, send_status, tag(203)));
  cq_expect_finish_accepted(v_server, tag(203), GRPC_OP_OK);
  cq_expect_finished(v_server, tag(202), NULL);
  cq_verify(v_server);

  cq_expect_finished_with_status(v_client, tag(402), send_status, NULL);
  cq_verify(v_client);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);

  grpc_call_destroy(c1);
  grpc_call_destroy(s1);
  grpc_call_destroy(c2);
  grpc_call_destroy(s2);

  end_test(&f);
  config.tear_down_data(&f);
}

static gpr_slice large_slice() {
  gpr_slice slice = gpr_slice_malloc(1000000);
  memset(GPR_SLICE_START_PTR(slice), 0xab, GPR_SLICE_LENGTH(slice));
  return slice;
}

static void test_invoke_large_request(grpc_end2end_test_config config) {
  grpc_call *c;
  grpc_call *s;
  grpc_status send_status = {GRPC_STATUS_UNIMPLEMENTED, "xyz"};
  gpr_slice request_payload_slice = large_slice();
  grpc_byte_buffer *request_payload =
      grpc_byte_buffer_create(&request_payload_slice, 1);
  gpr_timespec deadline = five_seconds_time();
  grpc_end2end_test_fixture f = begin_test(config, __FUNCTION__, NULL, NULL);
  cq_verifier *v_client = cq_verifier_create(f.client_cq);
  cq_verifier *v_server = cq_verifier_create(f.server_cq);

  /* byte buffer holds the slice, we can unref it already */
  gpr_slice_unref(request_payload_slice);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(f.server, tag(100)));

  c = grpc_channel_create_call(f.client, "/foo", "test.google.com", deadline);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_invoke(c, f.client_cq, tag(1), tag(2), tag(3), 0));
  cq_expect_invoke_accepted(v_client, tag(1), GRPC_OP_OK);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write(c, request_payload, tag(4), 0));
  /* destroy byte buffer early to ensure async code keeps track of its contents
     correctly */
  grpc_byte_buffer_destroy(request_payload);
  /* write should not be accepted until the server is willing to read the
     request (as this request is very large) */
  cq_verify_empty(v_client);

  cq_expect_server_rpc_new(v_server, &s, tag(100), "/foo", "test.google.com",
                           deadline, NULL);
  cq_verify(v_server);

  grpc_call_accept(s, f.server_cq, tag(102), 0);
  cq_expect_client_metadata_read(v_client, tag(2), NULL);
  cq_verify(v_client);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_read(s, tag(5)));
  /* now the write can be accepted */
  cq_expect_write_accepted(v_client, tag(4), GRPC_OP_OK);
  cq_verify(v_client);
  cq_expect_read(v_server, tag(5), large_slice());
  cq_verify(v_server);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_writes_done(c, tag(8)));
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_write_status(s, send_status, tag(9)));

  cq_expect_finish_accepted(v_client, tag(8), GRPC_OP_OK);
  cq_expect_finished_with_status(v_client, tag(3), send_status, NULL);
  cq_verify(v_client);

  cq_expect_finish_accepted(v_server, tag(9), GRPC_OP_OK);
  cq_expect_finished(v_server, tag(102), NULL);
  cq_verify(v_server);

  grpc_call_destroy(c);
  grpc_call_destroy(s);

  cq_verifier_destroy(v_client);
  cq_verifier_destroy(v_server);

  end_test(&f);
  config.tear_down_data(&f);
}

void grpc_end2end_tests(grpc_end2end_test_config config) {
  int i;
  canceller cancellers[2] = {grpc_call_cancel, wait_for_deadline};

  test_no_op(config);
  test_invoke_simple_request(config, "simple_request_body",
                             simple_request_body);
  test_invoke_simple_request(config, "simple_request_body2",
                             simple_request_body2);
  test_invoke_10_simple_requests(config);
  if (config.feature_mask & FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION) {
    test_simple_delayed_request_short(config);
    test_simple_delayed_request_long(config);
  }
  test_invoke_request_with_payload(config);
  test_request_response_with_metadata_and_payload(config);
  test_request_with_large_metadata(config);
  test_writes_done_hangs_with_pending_read(config);
  test_invoke_request_response_with_payload(config);
  test_invoke_10_request_response_with_payload(config);
  test_early_server_shutdown_finishes_tags(config);
  test_early_server_shutdown_finishes_inflight_calls(config);
  test_max_concurrent_streams(config);
  test_invoke_large_request(config);
  for (i = 0; i < GPR_ARRAY_SIZE(cancellers); i++) {
    test_cancel_in_a_vacuum(config, cancellers[i]);
    test_cancel_after_invoke(config, cancellers[i]);
    test_cancel_after_accept(config, cancellers[i]);
    test_cancel_after_accept_and_writes_closed(config, cancellers[i]);
  }
  test_cancel_before_invoke(config);
  for (i = 1; i < 10; i++) {
    test_pingpong_streaming(config, i);
  }
}
