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

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"
#include "test/core/util/test_lb_policies.h"

namespace grpc_core {
namespace {

constexpr absl::string_view kDropPolicyName = "drop_lb";

class DropPolicy : public LoadBalancingPolicy {
 public:
  explicit DropPolicy(Args args) : LoadBalancingPolicy(std::move(args)) {}

  absl::string_view name() const override { return kDropPolicyName; }

  absl::Status UpdateLocked(UpdateArgs) override {
    channel_control_helper()->UpdateState(GRPC_CHANNEL_READY, absl::Status(),
                                          MakeRefCounted<DropPicker>());
    return absl::OkStatus();
  }

  void ResetBackoffLocked() override {}
  void ShutdownLocked() override {}

 private:
  class DropPicker : public SubchannelPicker {
   public:
    PickResult Pick(PickArgs /*args*/) override {
      return PickResult::Drop(
          absl::UnavailableError("Call dropped by drop LB policy"));
    }
  };
};

class DropLbConfig : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override { return kDropPolicyName; }
};

class DropPolicyFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<DropPolicy>(std::move(args));
  }

  absl::string_view name() const override { return kDropPolicyName; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return MakeRefCounted<DropLbConfig>();
  }
};

std::vector<PickArgsSeen>* g_pick_args_vector = nullptr;

void RegisterDropPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<DropPolicyFactory>());
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
  grpc_server_shutdown_and_notify(f->server, f->cq, tag(1000));
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(f->cq, grpc_timeout_seconds_to_deadline(5),
                                    nullptr);
  } while (ev.type != GRPC_OP_COMPLETE || ev.tag != tag(1000));
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
}

// Tests that we don't retry when the LB policy drops a call,
// even when there is retry configuration in the service config.
// - 1 retry allowed for UNAVAILABLE status
// - first attempt returns UNAVAILABLE due to LB drop but does not retry
static void test_retry_lb_drop(grpc_end2end_test_config config) {
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

  std::vector<grpc_core::PickArgsSeen> pick_args_seen;
  grpc_core::g_pick_args_vector = &pick_args_seen;

  grpc_arg args[] = {
      grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_ARG_SERVICE_CONFIG),
          const_cast<char*>(
              "{\n"
              "  \"loadBalancingConfig\": [ {\n"
              "    \"test_pick_args_lb\": {}\n"
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
              "      \"retryableStatusCodes\": [ \"UNAVAILABLE\" ]\n"
              "    }\n"
              "  } ]\n"
              "}")),
  };
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(args), args};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_lb_drop", &client_args, nullptr);

  grpc_core::CqVerifier cqv(f.cq);

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
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(tag(1), true);
  cqv.Verify();

  GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);
  GPR_ASSERT(0 ==
             grpc_slice_str_cmp(details, "Call dropped by drop LB policy"));

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload_recv);

  grpc_call_unref(c);

  gpr_log(GPR_INFO, "NUMBER OF LB PICKS: %" PRIuPTR, pick_args_seen.size());
  GPR_ASSERT(pick_args_seen.size() == 1);

  grpc_core::g_pick_args_vector = nullptr;

  end_test(&f);
  config.tear_down_data(&f);
}

void retry_lb_drop(grpc_end2end_test_config config) {
  GPR_ASSERT(config.feature_mask & FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL);
  test_retry_lb_drop(config);
}

void retry_lb_drop_pre_init(void) {
  grpc_core::CoreConfiguration::RegisterBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        grpc_core::RegisterTestPickArgsLoadBalancingPolicy(
            builder,
            [](const grpc_core::PickArgsSeen& pick_args) {
              GPR_ASSERT(grpc_core::g_pick_args_vector != nullptr);
              grpc_core::g_pick_args_vector->push_back(pick_args);
            },
            grpc_core::kDropPolicyName);
      });
  grpc_core::CoreConfiguration::RegisterBuilder(grpc_core::RegisterDropPolicy);
}
