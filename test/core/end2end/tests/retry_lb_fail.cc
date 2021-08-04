//
// Copyright 2017 gRPC authors.
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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/static_metadata.h"

#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/tests/cancel_test_helpers.h"

namespace grpc_core {
namespace {

const char* kFailPolicyName = "fail_lb";

Atomic<int> g_num_lb_picks;

class FailPolicy : public LoadBalancingPolicy {
 public:
  explicit FailPolicy(Args args) : LoadBalancingPolicy(std::move(args)) {}

  const char* name() const override { return kFailPolicyName; }

  void UpdateLocked(UpdateArgs) override {
    absl::Status status = absl::AbortedError("LB pick failed");
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        absl::make_unique<FailPicker>(status));
  }

  void ResetBackoffLocked() override {}
  void ShutdownLocked() override {}

 private:
  class FailPicker : public SubchannelPicker {
   public:
    explicit FailPicker(absl::Status status) : status_(status) {}

    PickResult Pick(PickArgs /*args*/) override {
      g_num_lb_picks.FetchAdd(1);
      return PickResult::Fail(status_);
    }

   private:
    absl::Status status_;
  };
};

class FailLbConfig : public LoadBalancingPolicy::Config {
 public:
  const char* name() const override { return kFailPolicyName; }
};

class FailPolicyFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<FailPolicy>(std::move(args));
  }

  const char* name() const override { return kFailPolicyName; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& /*json*/, grpc_error_handle* /*error*/) const override {
    return MakeRefCounted<FailLbConfig>();
  }
};

void RegisterFailPolicy() {
  LoadBalancingPolicyRegistry::Builder::RegisterLoadBalancingPolicyFactory(
      absl::make_unique<FailPolicyFactory>());
}

}  // namespace
}  // namespace grpc_core

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

// Tests that we retry properly when the LB policy fails the call before
// it ever gets to the transport, even if recv_trailing_metadata isn't
// started by the application until after the LB pick fails.
// - 1 retry allowed for ABORTED status
// - on first attempt, LB policy fails with ABORTED before application
//   starts recv_trailing_metadata op
static void test_retry_lb_fail(grpc_end2end_test_config config) {
  grpc_call* c;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;

  grpc_core::g_num_lb_picks.Store(0, grpc_core::MemoryOrder::RELAXED);

  grpc_arg args[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ENABLE_RETRIES), 1),
      grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_ARG_SERVICE_CONFIG),
          const_cast<char*>(
              "{\n"
              "  \"loadBalancingConfig\": [ {\n"
              "    \"fail_lb\": {}\n"
              "  } ],\n"
              "  \"methodConfig\": [ {\n"
              "    \"name\": [\n"
              "      { \"service\": \"service\", \"method\": \"method\" }\n"
              "    ],\n"
              "    \"retryPolicy\": {\n"
              "      \"maxAttempts\": 2,\n"
              "      \"initialBackoff\": \"1s\",\n"
              "      \"maxBackoff\": \"120s\",\n"
              "      \"backoffMultiplier\": 1.6,\n"
              "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
              "    }\n"
              "  } ]\n"
              "}")),
  };
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(args), args};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_lb_fail", &client_args, nullptr);

  cq_verifier* cqv = cq_verifier_create(f.cq);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/service/method"),
                               nullptr, deadline, nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(1), false);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(2),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(2), true);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_ABORTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "LB pick failed"));

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload_recv);

  grpc_call_unref(c);

  cq_verifier_destroy(cqv);

  int num_picks =
      grpc_core::g_num_lb_picks.Load(grpc_core::MemoryOrder::RELAXED);
  gpr_log(GPR_INFO, "NUM LB PICKS: %d", num_picks);
  GPR_ASSERT(num_picks == 2);

  end_test(&f);
  config.tear_down_data(&f);
}

void retry_lb_fail(grpc_end2end_test_config config) {
  GPR_ASSERT(config.feature_mask & FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL);
  test_retry_lb_fail(config);
}

void retry_lb_fail_pre_init(void) { grpc_core::RegisterFailPolicy(); }
