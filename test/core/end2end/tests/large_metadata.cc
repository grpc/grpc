//
//
// Copyright 2015 gRPC authors.
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
//
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <functional>
#include <memory>

#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

static std::unique_ptr<CoreTestFixture> begin_test(
    const CoreTestConfiguration& config, const char* test_name,
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
  auto f = config.create_fixture(grpc_core::ChannelArgs::FromC(client_args),
                                 grpc_core::ChannelArgs::FromC(server_args));
  f->InitServer(grpc_core::ChannelArgs::FromC(server_args));
  f->InitClient(grpc_core::ChannelArgs::FromC(client_args));
  return f;
}

static grpc_status_code send_metadata(CoreTestFixture* f,
                                      const size_t metadata_size,
                                      grpc_slice* client_details) {
  grpc_core::CqVerifier cqv(f->cq());
  grpc_call* c;
  grpc_call* s;
  grpc_metadata meta;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  int was_cancelled = 2;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  c = grpc_channel_create_call(f->client(), nullptr, GRPC_PROPAGATE_DEFAULTS,
                               f->cq(), grpc_slice_from_static_string("/foo"),
                               nullptr, deadline, nullptr);
  GPR_ASSERT(c);

  // Add metadata of size `metadata_size`.
  meta.key = grpc_slice_from_static_string("key");
  meta.value = grpc_slice_malloc(metadata_size);
  memset(GRPC_SLICE_START_PTR(meta.value), 'a', metadata_size);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  // Client: wait on initial metadata from server.
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
  op->data.recv_status_on_client.status_details = client_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error = grpc_server_request_call(f->server(), &s, &call_details,
                                   &request_metadata_recv, f->cq(), f->cq(),
                                   grpc_core::CqVerifier::tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(grpc_core::CqVerifier::tag(101), true);
  cqv.Verify();

  memset(ops, 0, sizeof(ops));
  // Server: send metadata of size `metadata_size`.
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 1;
  op->data.send_initial_metadata.metadata = &meta;
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
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(102), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(grpc_core::CqVerifier::tag(102), true);
  cqv.Expect(grpc_core::CqVerifier::tag(1), true);
  cqv.Verify();

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  grpc_slice_unref(meta.value);

  return status;
}

// Server responds with metadata under soft limit of what client accepts. No
// requests should be rejected.
static void test_request_with_large_metadata_under_soft_limit(
    const CoreTestConfiguration& config) {
  const size_t soft_limit = 32 * 1024;
  const size_t hard_limit = 45 * 1024;
  const size_t metadata_size = soft_limit;
  grpc_arg arg[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_MAX_METADATA_SIZE), soft_limit + 1024),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE),
          hard_limit + 1024)};
  grpc_channel_args args = {GPR_ARRAY_SIZE(arg), arg};
  auto f =
      begin_test(config, "test_request_with_large_metadata_under_soft_limit",
                 &args, &args);
  for (int i = 0; i < 100; i++) {
    grpc_slice client_details;
    auto status = send_metadata(f.get(), metadata_size, &client_details);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    GPR_ASSERT(0 == grpc_slice_str_cmp(client_details, "xyz"));
    grpc_slice_unref(client_details);
  }
}

// Server responds with metadata between soft and hard limits of what client
// accepts. Some requests should be rejected.
static void test_request_with_large_metadata_between_soft_and_hard_limits(
    const CoreTestConfiguration& config) {
  const size_t soft_limit = 32 * 1024;
  const size_t hard_limit = 45 * 1024;
  const size_t metadata_size = (soft_limit + hard_limit) / 2;
  grpc_arg arg[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_MAX_METADATA_SIZE), soft_limit + 1024),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE),
          hard_limit + 1024)};
  grpc_channel_args args = {GPR_ARRAY_SIZE(arg), arg};
  auto f = begin_test(
      config, "test_request_with_large_metadata_between_soft_and_hard_limits",
      &args, &args);

  int num_requests_rejected = 0;
  for (int i = 0; i < 100; i++) {
    grpc_slice client_details;
    auto status = send_metadata(f.get(), metadata_size, &client_details);
    if (status == GRPC_STATUS_RESOURCE_EXHAUSTED) {
      num_requests_rejected++;
      const char* expected_error =
          "received initial metadata size exceeds soft limit";
      grpc_slice actual_error =
          grpc_slice_split_head(&client_details, strlen(expected_error));
      GPR_ASSERT(0 == grpc_slice_str_cmp(actual_error, expected_error));
      grpc_slice_unref(actual_error);
    } else {
      GPR_ASSERT(status == GRPC_STATUS_OK);
      GPR_ASSERT(0 == grpc_slice_str_cmp(client_details, "xyz"));
    }
    grpc_slice_unref(client_details);
  }

  // Check that some requests were rejected.
  GPR_ASSERT(abs(num_requests_rejected - 50) <= 45);
}

// Server responds with metadata above hard limit of what the client accepts.
// All requests should be rejected.
static void test_request_with_large_metadata_above_hard_limit(
    const CoreTestConfiguration& config) {
  const size_t soft_limit = 32 * 1024;
  const size_t hard_limit = 45 * 1024;
  const size_t metadata_size = hard_limit * 1.5;
  grpc_arg arg[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_MAX_METADATA_SIZE), soft_limit + 1024),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE),
          hard_limit + 1024)};
  grpc_channel_args args = {GPR_ARRAY_SIZE(arg), arg};
  auto f =
      begin_test(config, "test_request_with_large_metadata_above_hard_limit",
                 &args, &args);

  for (int i = 0; i < 100; i++) {
    grpc_slice client_details;
    auto status = send_metadata(f.get(), metadata_size, &client_details);
    GPR_ASSERT(status == GRPC_STATUS_RESOURCE_EXHAUSTED);
    const char* expected_error =
        "received initial metadata size exceeds hard limit";
    grpc_slice actual_error =
        grpc_slice_split_head(&client_details, strlen(expected_error));
    GPR_ASSERT(0 == grpc_slice_str_cmp(actual_error, expected_error));
    grpc_slice_unref(actual_error);
    grpc_slice_unref(client_details);
  }
}

// Set soft limit higher than hard limit. All requests above hard limit should
// be rejected, all requests below hard limit should be accepted (soft limit
// should not be respected).
static void test_request_with_large_metadata_soft_limit_above_hard_limit(
    const CoreTestConfiguration& config) {
  const size_t soft_limit = 64 * 1024;
  const size_t hard_limit = 32 * 1024;
  const size_t metadata_size_below_hard_limit = hard_limit;
  const size_t metadata_size_above_hard_limit = hard_limit * 2;
  grpc_arg arg[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_MAX_METADATA_SIZE), soft_limit + 1024),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE),
          hard_limit + 1024)};
  grpc_channel_args args = {GPR_ARRAY_SIZE(arg), arg};
  auto f = begin_test(
      config, "test_request_with_large_metadata_soft_limit_above_hard_limit",
      &args, &args);

  // Send 50 requests below hard limit. Should be accepted.
  for (int i = 0; i < 50; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_below_hard_limit, &client_details);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    GPR_ASSERT(0 == grpc_slice_str_cmp(client_details, "xyz"));
    grpc_slice_unref(client_details);
  }

  // Send 50 requests above hard limit. Should be rejected.
  for (int i = 0; i < 50; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_above_hard_limit, &client_details);
    GPR_ASSERT(status == GRPC_STATUS_RESOURCE_EXHAUSTED);
    const char* expected_error =
        "received initial metadata size exceeds hard limit";
    grpc_slice actual_error =
        grpc_slice_split_head(&client_details, strlen(expected_error));
    GPR_ASSERT(0 == grpc_slice_str_cmp(actual_error, expected_error));
    grpc_slice_unref(actual_error);
    grpc_slice_unref(client_details);
  }
}

// Set soft limit * 1.25 higher than default hard limit and do not set hard
// limit. Soft limit * 1.25 should be used as hard limit.
static void test_request_with_large_metadata_soft_limit_overrides_default_hard(
    const CoreTestConfiguration& config) {
  const size_t soft_limit = 64 * 1024;
  const size_t metadata_size_below_soft_limit = soft_limit;
  const size_t metadata_size_above_hard_limit = soft_limit * 1.5;
  const size_t metadata_size_between_limits =
      (soft_limit + soft_limit * 1.25) / 2;
  grpc_arg arg[] = {grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_MAX_METADATA_SIZE), soft_limit + 1024)};
  grpc_channel_args args = {GPR_ARRAY_SIZE(arg), arg};
  auto f = begin_test(
      config,
      "test_request_with_large_metadata_soft_limit_overrides_default_hard",
      &args, &args);

  // Send 50 requests below soft limit. Should be accepted.
  for (int i = 0; i < 50; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_below_soft_limit, &client_details);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    GPR_ASSERT(0 == grpc_slice_str_cmp(client_details, "xyz"));
    grpc_slice_unref(client_details);
  }

  // Send 100 requests between soft and hard limits. Some should be rejected.
  int num_requests_rejected = 0;
  for (int i = 0; i < 100; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_between_limits, &client_details);
    if (status == GRPC_STATUS_RESOURCE_EXHAUSTED) {
      num_requests_rejected++;
      const char* expected_error =
          "received initial metadata size exceeds soft limit";
      grpc_slice actual_error =
          grpc_slice_split_head(&client_details, strlen(expected_error));
      GPR_ASSERT(0 == grpc_slice_str_cmp(actual_error, expected_error));
      grpc_slice_unref(actual_error);
    } else {
      GPR_ASSERT(status == GRPC_STATUS_OK);
      GPR_ASSERT(0 == grpc_slice_str_cmp(client_details, "xyz"));
    }
    grpc_slice_unref(client_details);
  }
  // Check that some requests were rejected.
  GPR_ASSERT(abs(num_requests_rejected - 50) <= 45);

  // Send 50 requests above hard limit. Should be rejected.
  for (int i = 0; i < 50; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_above_hard_limit, &client_details);
    GPR_ASSERT(status == GRPC_STATUS_RESOURCE_EXHAUSTED);
    const char* expected_error =
        "received initial metadata size exceeds hard limit";
    grpc_slice actual_error =
        grpc_slice_split_head(&client_details, strlen(expected_error));
    GPR_ASSERT(0 == grpc_slice_str_cmp(actual_error, expected_error));
    grpc_slice_unref(actual_error);
    grpc_slice_unref(client_details);
  }
}

// Set hard limit * 0.8 higher than default soft limit and do not set soft
// limit. Hard limit * 0.8 should be used as soft limit.
static void test_request_with_large_metadata_hard_limit_overrides_default_soft(
    const CoreTestConfiguration& config) {
  const size_t hard_limit = 45 * 1024;
  const size_t metadata_size_below_soft_limit = hard_limit * 0.5;
  const size_t metadata_size_above_hard_limit = hard_limit * 1.5;
  const size_t metadata_size_between_limits =
      (hard_limit * 0.8 + hard_limit) / 2;
  grpc_arg arg[] = {grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE),
      hard_limit + 1024)};
  grpc_channel_args args = {GPR_ARRAY_SIZE(arg), arg};
  auto f = begin_test(
      config,
      "test_request_with_large_metadata_hard_limit_overrides_default_soft",
      &args, &args);

  // Send 50 requests below soft limit. Should be accepted.
  for (int i = 0; i < 50; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_below_soft_limit, &client_details);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    GPR_ASSERT(0 == grpc_slice_str_cmp(client_details, "xyz"));
    grpc_slice_unref(client_details);
  }

  // Send 100 requests between soft and hard limits. Some should be rejected.
  int num_requests_rejected = 0;
  for (int i = 0; i < 100; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_between_limits, &client_details);
    if (status == GRPC_STATUS_RESOURCE_EXHAUSTED) {
      num_requests_rejected++;
      const char* expected_error =
          "received initial metadata size exceeds soft limit";
      grpc_slice actual_error =
          grpc_slice_split_head(&client_details, strlen(expected_error));
      GPR_ASSERT(0 == grpc_slice_str_cmp(actual_error, expected_error));
      grpc_slice_unref(actual_error);
    } else {
      GPR_ASSERT(status == GRPC_STATUS_OK);
      GPR_ASSERT(0 == grpc_slice_str_cmp(client_details, "xyz"));
    }
    grpc_slice_unref(client_details);
  }
  // Check that some requests were rejected.
  GPR_ASSERT(abs(num_requests_rejected - 50) <= 45);

  // Send 50 requests above hard limit. Should be rejected.
  for (int i = 0; i < 50; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_above_hard_limit, &client_details);
    GPR_ASSERT(status == GRPC_STATUS_RESOURCE_EXHAUSTED);
    const char* expected_error =
        "received initial metadata size exceeds hard limit";
    grpc_slice actual_error =
        grpc_slice_split_head(&client_details, strlen(expected_error));
    GPR_ASSERT(0 == grpc_slice_str_cmp(actual_error, expected_error));
    grpc_slice_unref(actual_error);
    grpc_slice_unref(client_details);
  }
}

// Set hard limit lower than default hard limit and ensure new limit is
// respected. Default soft limit is not respected since hard limit is lower than
// soft limit.
static void test_request_with_large_metadata_hard_limit_below_default_hard(
    const CoreTestConfiguration& config) {
  const size_t hard_limit = 4 * 1024;
  const size_t metadata_size_below_hard_limit = hard_limit;
  const size_t metadata_size_above_hard_limit = hard_limit * 2;
  grpc_arg arg[] = {grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE),
      hard_limit + 1024)};
  grpc_channel_args args = {GPR_ARRAY_SIZE(arg), arg};
  auto f = begin_test(
      config, "test_request_with_large_metadata_hard_limit_below_default_hard",
      &args, &args);

  // Send 50 requests below hard limit. Should be accepted.
  for (int i = 0; i < 50; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_below_hard_limit, &client_details);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    GPR_ASSERT(0 == grpc_slice_str_cmp(client_details, "xyz"));
    grpc_slice_unref(client_details);
  }

  // Send 50 requests above hard limit. Should be rejected.
  for (int i = 0; i < 50; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_above_hard_limit, &client_details);
    GPR_ASSERT(status == GRPC_STATUS_RESOURCE_EXHAUSTED);
    const char* expected_error =
        "received initial metadata size exceeds hard limit";
    grpc_slice actual_error =
        grpc_slice_split_head(&client_details, strlen(expected_error));
    GPR_ASSERT(0 == grpc_slice_str_cmp(actual_error, expected_error));
    grpc_slice_unref(actual_error);
    grpc_slice_unref(client_details);
  }
}

// Set soft limit lower than default soft limit and ensure new limit is
// respected. Hard limit should be default hard since this is greater than 2 *
// soft limit.
static void test_request_with_large_metadata_soft_limit_below_default_soft(
    const CoreTestConfiguration& config) {
  const size_t soft_limit = 1 * 1024;
  const size_t metadata_size_below_soft_limit = soft_limit;
  // greater than 2 * soft, less than default hard
  const size_t metadata_size_between_limits = 10 * 1024;
  const size_t metadata_size_above_hard_limit = 75 * 1024;
  grpc_arg arg[] = {grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_MAX_METADATA_SIZE), soft_limit + 1024)};
  grpc_channel_args args = {GPR_ARRAY_SIZE(arg), arg};
  auto f = begin_test(
      config, "test_request_with_large_metadata_soft_limit_below_default_soft",
      &args, &args);

  // Send 50 requests below soft limit. Should be accepted.
  for (int i = 0; i < 50; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_below_soft_limit, &client_details);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    GPR_ASSERT(0 == grpc_slice_str_cmp(client_details, "xyz"));
    grpc_slice_unref(client_details);
  }

  // Send 100 requests between soft and hard limits. Some should be rejected.
  int num_requests_rejected = 0;
  for (int i = 0; i < 100; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_between_limits, &client_details);
    if (status == GRPC_STATUS_RESOURCE_EXHAUSTED) {
      num_requests_rejected++;
      const char* expected_error =
          "received initial metadata size exceeds soft limit";
      grpc_slice actual_error =
          grpc_slice_split_head(&client_details, strlen(expected_error));
      GPR_ASSERT(0 == grpc_slice_str_cmp(actual_error, expected_error));
      grpc_slice_unref(actual_error);
    } else {
      GPR_ASSERT(status == GRPC_STATUS_OK);
      GPR_ASSERT(0 == grpc_slice_str_cmp(client_details, "xyz"));
    }
    grpc_slice_unref(client_details);
  }
  // Check that some requests were rejected.
  GPR_ASSERT((abs(num_requests_rejected - 50) <= 49));

  // Send 50 requests above hard limit. Should be rejected.
  for (int i = 0; i < 50; i++) {
    grpc_slice client_details;
    auto status =
        send_metadata(f.get(), metadata_size_above_hard_limit, &client_details);
    GPR_ASSERT(status == GRPC_STATUS_RESOURCE_EXHAUSTED);
    const char* expected_error =
        "received initial metadata size exceeds hard limit";
    grpc_slice actual_error =
        grpc_slice_split_head(&client_details, strlen(expected_error));
    GPR_ASSERT(0 == grpc_slice_str_cmp(actual_error, expected_error));
    grpc_slice_unref(actual_error);
    grpc_slice_unref(client_details);
  }
}

void large_metadata(const CoreTestConfiguration& config) {
  test_request_with_large_metadata_under_soft_limit(config);
  // TODO(yashykt): Maybe add checks for metadata size in inproc transport too.
  if (strcmp(config.name, "inproc") != 0) {
    test_request_with_large_metadata_between_soft_and_hard_limits(config);
    test_request_with_large_metadata_above_hard_limit(config);
    test_request_with_large_metadata_soft_limit_above_hard_limit(config);
    test_request_with_large_metadata_soft_limit_overrides_default_hard(config);
    test_request_with_large_metadata_hard_limit_overrides_default_soft(config);
    test_request_with_large_metadata_hard_limit_below_default_hard(config);
    test_request_with_large_metadata_soft_limit_below_default_soft(config);
  }
}

void large_metadata_pre_init(void) {}
