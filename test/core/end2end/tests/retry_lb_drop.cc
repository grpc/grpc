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
#include <functional>
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

// Tests that we don't retry when the LB policy drops a call,
// even when there is retry configuration in the service config.
// - 1 retry allowed for UNAVAILABLE status
// - first attempt returns UNAVAILABLE due to LB drop but does not retry
TEST_P(RetryTest, RetryLbDrop) {
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
  std::vector<grpc_core::PickArgsSeen> pick_args_seen;
  grpc_core::g_pick_args_vector = &pick_args_seen;
  InitServer(ChannelArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
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
      "}"));
  auto c =
      NewClientCall("/service/method").Timeout(Duration::Seconds(5)).Create();
  IncomingMessage server_message;
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("foo")
      .RecvMessage(server_message)
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNAVAILABLE);
  EXPECT_EQ(server_status.message(), "Call dropped by drop LB policy");
  EXPECT_EQ(pick_args_seen.size(), 1);
  grpc_core::g_pick_args_vector = nullptr;
}

}  // namespace
}  // namespace grpc_core
