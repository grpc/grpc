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

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/service_config.h"

#include "test/core/end2end/cq_verifier.h"

static void *tag(intptr_t t) { return (void *)t; }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char *test_name,
                                            grpc_channel_args *client_args,
                                            grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
  // We intentionally do not pass the client and server args to
  // create_fixture(), since we don't want the limit enforced on the
  // proxy, only on the backend server.
  f = config.create_fixture(NULL, NULL);
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

// Test with request larger than the limit.
// If send_limit is true, applies send limit on client; otherwise, applies
// recv limit on server.
static void test_max_message_length_on_request(grpc_end2end_test_config config,
                                               bool send_limit,
                                               bool use_service_config,
                                               bool use_string_json_value) {
  gpr_log(GPR_INFO,
          "testing request with send_limit=%d use_service_config=%d "
          "use_string_json_value=%d",
          send_limit, use_service_config, use_string_json_value);

  grpc_end2end_test_fixture f;
  grpc_call *c = NULL;
  grpc_call *s = NULL;
  cq_verifier *cqv;
  grpc_op ops[6];
  grpc_op *op;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer *recv_payload = NULL;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

  grpc_channel_args *client_args = NULL;
  grpc_channel_args *server_args = NULL;
  if (use_service_config) {
    // We don't currently support service configs on the server side.
    GPR_ASSERT(send_limit);
    grpc_arg arg;
    arg.type = GRPC_ARG_STRING;
    arg.key = GRPC_ARG_SERVICE_CONFIG;
    arg.value.string =
        use_string_json_value
            ? "{\n"
              "  \"methodConfig\": [ {\n"
              "    \"name\": [\n"
              "      { \"service\": \"service\", \"method\": \"method\" }\n"
              "    ],\n"
              "    \"maxRequestMessageBytes\": \"5\"\n"
              "  } ]\n"
              "}"
            : "{\n"
              "  \"methodConfig\": [ {\n"
              "    \"name\": [\n"
              "      { \"service\": \"service\", \"method\": \"method\" }\n"
              "    ],\n"
              "    \"maxRequestMessageBytes\": 5\n"
              "  } ]\n"
              "}";
    client_args = grpc_channel_args_copy_and_add(NULL, &arg, 1);
  } else {
    // Set limit via channel args.
    grpc_arg arg;
    arg.key = send_limit ? GRPC_ARG_MAX_SEND_MESSAGE_LENGTH
                         : GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH;
    arg.type = GRPC_ARG_INTEGER;
    arg.value.integer = 5;
    grpc_channel_args *args = grpc_channel_args_copy_and_add(NULL, &arg, 1);
    if (send_limit) {
      client_args = args;
    } else {
      server_args = args;
    }
  }

  f = begin_test(config, "test_max_request_message_length", client_args,
                 server_args);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    if (client_args != NULL) grpc_channel_args_destroy(&exec_ctx, client_args);
    if (server_args != NULL) grpc_channel_args_destroy(&exec_ctx, server_args);
    grpc_exec_ctx_finish(&exec_ctx);
  }

  cqv = cq_verifier_create(f.cq);

  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config),
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
  op->data.send_message.send_message = request_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
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
  op->data.recv_message.recv_message = &recv_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  validate_host_override_string("foo.test.google.fr:1234", call_details.host,
                                config);
  GPR_ASSERT(was_cancelled == 1);

done:
  GPR_ASSERT(status == GRPC_STATUS_RESOURCE_EXHAUSTED);
  GPR_ASSERT(
      grpc_slice_str_cmp(
          details, send_limit
                       ? "Sent message larger than max (11 vs. 5)"
                       : "Received message larger than max (11 vs. 5)") == 0);

  grpc_slice_unref(details);
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
                                                bool send_limit,
                                                bool use_service_config,
                                                bool use_string_json_value) {
  gpr_log(GPR_INFO,
          "testing response with send_limit=%d use_service_config=%d "
          "use_string_json_value=%d",
          send_limit, use_service_config, use_string_json_value);

  grpc_end2end_test_fixture f;
  grpc_call *c = NULL;
  grpc_call *s = NULL;
  cq_verifier *cqv;
  grpc_op ops[6];
  grpc_op *op;
  grpc_slice response_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer *response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_byte_buffer *recv_payload = NULL;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

  grpc_channel_args *client_args = NULL;
  grpc_channel_args *server_args = NULL;
  if (use_service_config) {
    // We don't currently support service configs on the server side.
    GPR_ASSERT(!send_limit);
    grpc_arg arg;
    arg.type = GRPC_ARG_STRING;
    arg.key = GRPC_ARG_SERVICE_CONFIG;
    arg.value.string =
        use_string_json_value
            ? "{\n"
              "  \"methodConfig\": [ {\n"
              "    \"name\": [\n"
              "      { \"service\": \"service\", \"method\": \"method\" }\n"
              "    ],\n"
              "    \"maxResponseMessageBytes\": \"5\"\n"
              "  } ]\n"
              "}"
            : "{\n"
              "  \"methodConfig\": [ {\n"
              "    \"name\": [\n"
              "      { \"service\": \"service\", \"method\": \"method\" }\n"
              "    ],\n"
              "    \"maxResponseMessageBytes\": 5\n"
              "  } ]\n"
              "}";
    client_args = grpc_channel_args_copy_and_add(NULL, &arg, 1);
  } else {
    // Set limit via channel args.
    grpc_arg arg;
    arg.key = send_limit ? GRPC_ARG_MAX_SEND_MESSAGE_LENGTH
                         : GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH;
    arg.type = GRPC_ARG_INTEGER;
    arg.value.integer = 5;
    grpc_channel_args *args = grpc_channel_args_copy_and_add(NULL, &arg, 1);
    if (send_limit) {
      server_args = args;
    } else {
      client_args = args;
    }
  }

  f = begin_test(config, "test_max_response_message_length", client_args,
                 server_args);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    if (client_args != NULL) grpc_channel_args_destroy(&exec_ctx, client_args);
    if (server_args != NULL) grpc_channel_args_destroy(&exec_ctx, server_args);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  cqv = cq_verifier_create(f.cq);

  c = grpc_channel_create_call(
      f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq,
      grpc_slice_from_static_string("/service/method"),
      get_host_override_slice("foo.test.google.fr:1234", config),
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
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
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
  op->data.send_message.send_message = response_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  GPR_ASSERT(0 ==
             grpc_slice_str_cmp(call_details.host, "foo.test.google.fr:1234"));

  GPR_ASSERT(status == GRPC_STATUS_RESOURCE_EXHAUSTED);
  GPR_ASSERT(
      grpc_slice_str_cmp(
          details, send_limit
                       ? "Sent message larger than max (11 vs. 5)"
                       : "Received message larger than max (11 vs. 5)") == 0);

  grpc_slice_unref(details);
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
  test_max_message_length_on_request(config, false /* send_limit */,
                                     false /* use_service_config */,
                                     false /* use_string_json_value */);
  test_max_message_length_on_request(config, true /* send_limit */,
                                     false /* use_service_config */,
                                     false /* use_string_json_value */);
  test_max_message_length_on_response(config, false /* send_limit */,
                                      false /* use_service_config */,
                                      false /* use_string_json_value */);
  test_max_message_length_on_response(config, true /* send_limit */,
                                      false /* use_service_config */,
                                      false /* use_string_json_value */);
  test_max_message_length_on_request(config, true /* send_limit */,
                                     true /* use_service_config */,
                                     false /* use_string_json_value */);
  test_max_message_length_on_request(config, true /* send_limit */,
                                     true /* use_service_config */,
                                     true /* use_string_json_value */);
  test_max_message_length_on_response(config, false /* send_limit */,
                                      true /* use_service_config */,
                                      false /* use_string_json_value */);
  test_max_message_length_on_response(config, false /* send_limit */,
                                      true /* use_service_config */,
                                      true /* use_string_json_value */);
}

void max_message_length_pre_init(void) {}
