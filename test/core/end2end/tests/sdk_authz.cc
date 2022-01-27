// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/security/authorization/grpc_authorization_policy_provider.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/tls_utils.h"

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

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

static void test_allow_authorized_request(grpc_end2end_test_fixture f) {
  grpc_call* c;
  grpc_call* s;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  const char* error_string = nullptr;
  grpc_call_error error;
  grpc_slice details = grpc_empty_slice();
  int was_cancelled = 2;

  cq_verifier* cqv = cq_verifier_create(f.cq);

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
  op->data.recv_status_on_client.error_string = &error_string;
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
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
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
  GPR_ASSERT(GRPC_STATUS_OK == status);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));

  grpc_slice_unref(details);
  gpr_free(const_cast<char*>(error_string));
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);
  cq_verifier_destroy(cqv);
}

static void test_deny_unauthorized_request(grpc_end2end_test_fixture f) {
  grpc_call* c;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_status_code status;
  const char* error_string = nullptr;
  grpc_call_error error;
  grpc_slice details = grpc_empty_slice();

  cq_verifier* cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);

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
  op->data.recv_status_on_client.error_string = &error_string;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(GRPC_STATUS_PERMISSION_DENIED == status);
  GPR_ASSERT(0 ==
             grpc_slice_str_cmp(details, "Unauthorized RPC request rejected."));

  grpc_slice_unref(details);
  gpr_free(const_cast<char*>(error_string));
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);

  grpc_call_unref(c);
  cq_verifier_destroy(cqv);
}

static void test_static_init_allow_authorized_request(
    grpc_end2end_test_config config) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_status_code code = GRPC_STATUS_OK;
  const char* error_details;
  grpc_authorization_policy_provider* provider =
      grpc_authorization_policy_provider_static_data_create(authz_policy, &code,
                                                            &error_details);
  GPR_ASSERT(GRPC_STATUS_OK == code);
  grpc_arg args[] = {
      grpc_channel_arg_pointer_create(
          const_cast<char*>(GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER), provider,
          grpc_authorization_policy_provider_arg_vtable()),
  };
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(args), args};

  grpc_end2end_test_fixture f =
      begin_test(config, "test_static_init_allow_authorized_request", nullptr,
                 &server_args);
  grpc_authorization_policy_provider_release(provider);
  test_allow_authorized_request(f);

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_static_init_deny_unauthorized_request(
    grpc_end2end_test_config config) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_bar\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/bar\""
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_status_code code = GRPC_STATUS_OK;
  const char* error_details;
  grpc_authorization_policy_provider* provider =
      grpc_authorization_policy_provider_static_data_create(authz_policy, &code,
                                                            &error_details);
  GPR_ASSERT(GRPC_STATUS_OK == code);
  grpc_arg args[] = {
      grpc_channel_arg_pointer_create(
          const_cast<char*>(GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER), provider,
          grpc_authorization_policy_provider_arg_vtable()),
  };
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(args), args};

  grpc_end2end_test_fixture f =
      begin_test(config, "test_static_init_deny_unauthorized_request", nullptr,
                 &server_args);
  grpc_authorization_policy_provider_release(provider);
  test_deny_unauthorized_request(f);

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_static_init_deny_request_no_match_in_policy(
    grpc_end2end_test_config config) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_bar\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/bar\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_status_code code = GRPC_STATUS_OK;
  const char* error_details;
  grpc_authorization_policy_provider* provider =
      grpc_authorization_policy_provider_static_data_create(authz_policy, &code,
                                                            &error_details);
  GPR_ASSERT(GRPC_STATUS_OK == code);
  grpc_arg args[] = {
      grpc_channel_arg_pointer_create(
          const_cast<char*>(GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER), provider,
          grpc_authorization_policy_provider_arg_vtable()),
  };
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(args), args};

  grpc_end2end_test_fixture f =
      begin_test(config, "test_static_init_deny_request_no_match_in_policy",
                 nullptr, &server_args);
  grpc_authorization_policy_provider_release(provider);
  test_deny_unauthorized_request(f);

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_file_watcher_init_allow_authorized_request(
    grpc_end2end_test_config config) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(authz_policy);
  grpc_status_code code = GRPC_STATUS_OK;
  const char* error_details;
  grpc_authorization_policy_provider* provider =
      grpc_authorization_policy_provider_file_watcher_create(
          tmp_policy.name().c_str(), /*refresh_interval_sec=*/1, &code,
          &error_details);
  GPR_ASSERT(GRPC_STATUS_OK == code);
  grpc_arg args[] = {
      grpc_channel_arg_pointer_create(
          const_cast<char*>(GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER), provider,
          grpc_authorization_policy_provider_arg_vtable()),
  };
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(args), args};

  grpc_end2end_test_fixture f =
      begin_test(config, "test_file_watcher_init_allow_authorized_request",
                 nullptr, &server_args);
  grpc_authorization_policy_provider_release(provider);
  test_allow_authorized_request(f);

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_file_watcher_init_deny_unauthorized_request(
    grpc_end2end_test_config config) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_bar\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/bar\""
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(authz_policy);
  grpc_status_code code = GRPC_STATUS_OK;
  const char* error_details;
  grpc_authorization_policy_provider* provider =
      grpc_authorization_policy_provider_file_watcher_create(
          tmp_policy.name().c_str(), /*refresh_interval_sec=*/1, &code,
          &error_details);
  GPR_ASSERT(GRPC_STATUS_OK == code);
  grpc_arg args[] = {
      grpc_channel_arg_pointer_create(
          const_cast<char*>(GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER), provider,
          grpc_authorization_policy_provider_arg_vtable()),
  };
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(args), args};

  grpc_end2end_test_fixture f =
      begin_test(config, "test_file_watcher_init_deny_unauthorized_request",
                 nullptr, &server_args);
  grpc_authorization_policy_provider_release(provider);
  test_deny_unauthorized_request(f);

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_file_watcher_init_deny_request_no_match_in_policy(
    grpc_end2end_test_config config) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_bar\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/bar\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(authz_policy);
  grpc_status_code code = GRPC_STATUS_OK;
  const char* error_details;
  grpc_authorization_policy_provider* provider =
      grpc_authorization_policy_provider_file_watcher_create(
          tmp_policy.name().c_str(), /*refresh_interval_sec=*/1, &code,
          &error_details);
  GPR_ASSERT(GRPC_STATUS_OK == code);
  grpc_arg args[] = {
      grpc_channel_arg_pointer_create(
          const_cast<char*>(GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER), provider,
          grpc_authorization_policy_provider_arg_vtable()),
  };
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(args), args};

  grpc_end2end_test_fixture f = begin_test(
      config, "test_file_watcher_init_deny_request_no_match_in_policy", nullptr,
      &server_args);
  grpc_authorization_policy_provider_release(provider);
  test_deny_unauthorized_request(f);

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_file_watcher_valid_policy_reload(
    grpc_end2end_test_config config) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(authz_policy);
  grpc_status_code code = GRPC_STATUS_OK;
  const char* error_details;
  grpc_authorization_policy_provider* provider =
      grpc_authorization_policy_provider_file_watcher_create(
          tmp_policy.name().c_str(), /*refresh_interval_sec=*/1, &code,
          &error_details);
  GPR_ASSERT(GRPC_STATUS_OK == code);
  grpc_arg args[] = {
      grpc_channel_arg_pointer_create(
          const_cast<char*>(GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER), provider,
          grpc_authorization_policy_provider_arg_vtable()),
  };
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(args), args};

  grpc_end2end_test_fixture f = begin_test(
      config, "test_file_watcher_valid_policy_reload", nullptr, &server_args);
  grpc_authorization_policy_provider_release(provider);
  test_allow_authorized_request(f);
  // Replace existing policy in file with a different authorization policy.
  authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_bar\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/bar\""
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  tmp_policy.RewriteFile(authz_policy);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  test_deny_unauthorized_request(f);

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_file_watcher_invalid_policy_skip_reload(
    grpc_end2end_test_config config) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(authz_policy);
  grpc_status_code code = GRPC_STATUS_OK;
  const char* error_details;
  grpc_authorization_policy_provider* provider =
      grpc_authorization_policy_provider_file_watcher_create(
          tmp_policy.name().c_str(), /*refresh_interval_sec=*/1, &code,
          &error_details);
  GPR_ASSERT(GRPC_STATUS_OK == code);
  grpc_arg args[] = {
      grpc_channel_arg_pointer_create(
          const_cast<char*>(GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER), provider,
          grpc_authorization_policy_provider_arg_vtable()),
  };
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(args), args};

  grpc_end2end_test_fixture f =
      begin_test(config, "test_file_watcher_invalid_policy_skip_reload",
                 nullptr, &server_args);
  grpc_authorization_policy_provider_release(provider);
  test_allow_authorized_request(f);
  // Replace exisiting policy in file with an invalid policy.
  authz_policy = "{}";
  tmp_policy.RewriteFile(authz_policy);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  test_allow_authorized_request(f);

  end_test(&f);
  config.tear_down_data(&f);
}

static void test_file_watcher_recovers_from_failure(
    grpc_end2end_test_config config) {
  const char* authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(authz_policy);
  grpc_status_code code = GRPC_STATUS_OK;
  const char* error_details;
  grpc_authorization_policy_provider* provider =
      grpc_authorization_policy_provider_file_watcher_create(
          tmp_policy.name().c_str(), /*refresh_interval_sec=*/1, &code,
          &error_details);
  GPR_ASSERT(GRPC_STATUS_OK == code);
  grpc_arg args[] = {
      grpc_channel_arg_pointer_create(
          const_cast<char*>(GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER), provider,
          grpc_authorization_policy_provider_arg_vtable()),
  };
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(args), args};

  grpc_end2end_test_fixture f = begin_test(
      config, "test_file_watcher_valid_policy_reload", nullptr, &server_args);
  grpc_authorization_policy_provider_release(provider);
  test_allow_authorized_request(f);
  // Replace exisiting policy in file with an invalid policy.
  authz_policy = "{}";
  tmp_policy.RewriteFile(authz_policy);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  test_allow_authorized_request(f);
  // Recover from reload errors, by replacing invalid policy in file with a
  // valid policy.
  authz_policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_bar\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/bar\""
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  tmp_policy.RewriteFile(authz_policy);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  test_deny_unauthorized_request(f);

  end_test(&f);
  config.tear_down_data(&f);
}

void sdk_authz(grpc_end2end_test_config config) {
  test_static_init_allow_authorized_request(config);
  test_static_init_deny_unauthorized_request(config);
  test_static_init_deny_request_no_match_in_policy(config);
  test_file_watcher_init_allow_authorized_request(config);
  test_file_watcher_init_deny_unauthorized_request(config);
  test_file_watcher_init_deny_request_no_match_in_policy(config);
  test_file_watcher_valid_policy_reload(config);
  test_file_watcher_invalid_policy_skip_reload(config);
  test_file_watcher_recovers_from_failure(config);
}

void sdk_authz_pre_init(void) {}
