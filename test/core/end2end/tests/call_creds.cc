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
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/end2end/cq_verifier.h"

static const char iam_token[] = "token";
static const char iam_selector[] = "selector";
static const char overridden_iam_token[] = "overridden_token";
static const char overridden_iam_selector[] = "overridden_selector";

typedef enum { NONE, OVERRIDE, DESTROY, FAIL } override_mode;

static void* tag(intptr_t t) { return (void*)t; }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char* test_name,
                                            int fail_server_auth_check) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
  f = config.create_fixture(nullptr, nullptr);
  config.init_client(&f, nullptr);
  if (fail_server_auth_check) {
    grpc_arg fail_auth_arg = {
        GRPC_ARG_STRING,
        const_cast<char*>(FAIL_AUTH_CHECK_SERVER_ARG_NAME),
        {nullptr}};
    grpc_channel_args args;
    args.num_args = 1;
    args.args = &fail_auth_arg;
    config.init_server(&f, &args);
  } else {
    config.init_server(&f, nullptr);
  }
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

static void print_auth_context(int is_client, const grpc_auth_context* ctx) {
  const grpc_auth_property* p;
  grpc_auth_property_iterator it;
  gpr_log(GPR_INFO, "%s peer:", is_client ? "client" : "server");
  gpr_log(GPR_INFO, "\tauthenticated: %s",
          grpc_auth_context_peer_is_authenticated(ctx) ? "YES" : "NO");
  it = grpc_auth_context_peer_identity(ctx);
  while ((p = grpc_auth_property_iterator_next(&it)) != nullptr) {
    gpr_log(GPR_INFO, "\t\t%s: %s", p->name, p->value);
  }
  gpr_log(GPR_INFO, "\tall properties:");
  it = grpc_auth_context_property_iterator(ctx);
  while ((p = grpc_auth_property_iterator_next(&it)) != nullptr) {
    gpr_log(GPR_INFO, "\t\t%s: %s", p->name, p->value);
  }
}

static void request_response_with_payload_and_call_creds(
    const char* test_name, grpc_end2end_test_config config,
    override_mode mode) {
  grpc_call* c;
  grpc_call* s;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_slice response_payload_slice =
      grpc_slice_from_copied_string("hello you");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer* response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  grpc_end2end_test_fixture f;
  cq_verifier* cqv;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer* request_payload_recv = nullptr;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  grpc_call_credentials* creds = nullptr;
  grpc_auth_context* s_auth_context = nullptr;
  grpc_auth_context* c_auth_context = nullptr;

  f = begin_test(config, test_name, 0);
  cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);
  creds = grpc_google_iam_credentials_create(iam_token, iam_selector, nullptr);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(grpc_call_set_credentials(c, creds) == GRPC_CALL_OK);
  switch (mode) {
    case NONE:
      break;
    case OVERRIDE:
      grpc_call_credentials_release(creds);
      creds = grpc_google_iam_credentials_create(
          overridden_iam_token, overridden_iam_selector, nullptr);
      GPR_ASSERT(creds != nullptr);
      GPR_ASSERT(grpc_call_set_credentials(c, creds) == GRPC_CALL_OK);
      break;
    case DESTROY:
    case FAIL:
      GPR_ASSERT(grpc_call_set_credentials(c, nullptr) == GRPC_CALL_OK);
      break;
  }
  grpc_call_credentials_release(creds);

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
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
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
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
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
  s_auth_context = grpc_call_auth_context(s);
  GPR_ASSERT(s_auth_context != nullptr);
  print_auth_context(0, s_auth_context);
  grpc_auth_context_release(s_auth_context);

  c_auth_context = grpc_call_auth_context(c);
  GPR_ASSERT(c_auth_context != nullptr);
  print_auth_context(1, c_auth_context);
  grpc_auth_context_release(c_auth_context);

  /* Cannot set creds on the server call object. */
  GPR_ASSERT(grpc_call_set_credentials(s, nullptr) != GRPC_CALL_OK);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(103),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(103), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_OK);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
  GPR_ASSERT(was_cancelled == 0);
  GPR_ASSERT(byte_buffer_eq_string(request_payload_recv, "hello world"));
  GPR_ASSERT(byte_buffer_eq_string(response_payload_recv, "hello you"));

  switch (mode) {
    case NONE:
      GPR_ASSERT(contains_metadata(&request_metadata_recv,
                                   GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
                                   iam_token));
      GPR_ASSERT(contains_metadata(&request_metadata_recv,
                                   GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
                                   iam_selector));
      break;
    case OVERRIDE:
      GPR_ASSERT(contains_metadata(&request_metadata_recv,
                                   GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
                                   overridden_iam_token));
      GPR_ASSERT(contains_metadata(&request_metadata_recv,
                                   GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
                                   overridden_iam_selector));
      break;
    case DESTROY:
    case FAIL:
      GPR_ASSERT(!contains_metadata(&request_metadata_recv,
                                    GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
                                    iam_token));
      GPR_ASSERT(!contains_metadata(&request_metadata_recv,
                                    GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
                                    iam_selector));
      GPR_ASSERT(!contains_metadata(&request_metadata_recv,
                                    GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY,
                                    overridden_iam_token));
      GPR_ASSERT(!contains_metadata(&request_metadata_recv,
                                    GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY,
                                    overridden_iam_selector));
      break;
  }

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(request_payload_recv);
  grpc_byte_buffer_destroy(response_payload_recv);

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_request_response_with_payload_and_call_creds(
    grpc_end2end_test_config config) {
  request_response_with_payload_and_call_creds(
      "test_request_response_with_payload_and_call_creds", config, NONE);
}

static void test_request_response_with_payload_and_overridden_call_creds(
    grpc_end2end_test_config config) {
  request_response_with_payload_and_call_creds(
      "test_request_response_with_payload_and_overridden_call_creds", config,
      OVERRIDE);
}

static void test_request_response_with_payload_and_deleted_call_creds(
    grpc_end2end_test_config config) {
  request_response_with_payload_and_call_creds(
      "test_request_response_with_payload_and_deleted_call_creds", config,
      DESTROY);
}

static void test_request_response_with_payload_fail_to_send_call_creds(
    grpc_end2end_test_config config) {
  request_response_with_payload_and_call_creds(
      "test_request_response_with_payload_fail_to_send_call_creds", config,
      FAIL);
}

static void test_request_with_server_rejecting_client_creds(
    grpc_end2end_test_config config) {
  grpc_op ops[6];
  grpc_op* op;
  grpc_call* c;
  grpc_end2end_test_fixture f;
  gpr_timespec deadline = five_seconds_from_now();
  cq_verifier* cqv;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_call_credentials* creds;

  f = begin_test(config, "test_request_with_server_rejecting_client_creds", 1);
  cqv = cq_verifier_create(f.cq);

  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);

  creds = grpc_google_iam_credentials_create(iam_token, iam_selector, nullptr);
  GPR_ASSERT(creds != nullptr);
  GPR_ASSERT(grpc_call_set_credentials(c, creds) == GRPC_CALL_OK);
  grpc_call_credentials_release(creds);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
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
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(error == GRPC_CALL_OK);

  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_UNAUTHENTICATED);

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload_recv);
  grpc_slice_unref(details);

  grpc_call_unref(c);

  cq_verifier_destroy(cqv);
  end_test(&f);
  config.tear_down_data(&f);
}

void call_creds(grpc_end2end_test_config config) {
  if (config.feature_mask & FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS) {
    test_request_response_with_payload_and_call_creds(config);
    test_request_response_with_payload_and_overridden_call_creds(config);
    test_request_response_with_payload_and_deleted_call_creds(config);
    test_request_with_server_rejecting_client_creds(config);
  }
  if (config.feature_mask &
      FEATURE_MASK_DOES_NOT_SUPPORT_SEND_CALL_CREDENTIALS) {
    test_request_response_with_payload_fail_to_send_call_creds(config);
  }
}

void call_creds_pre_init(void) {}
