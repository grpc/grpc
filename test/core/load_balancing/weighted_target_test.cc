//
// Copyright 2026 gRPC authors.
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

#include <grpc/grpc.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/load_balancing/address_filtering.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/json/json.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/ref_counted_string.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

class WeightedTargetTest : public LoadBalancingPolicyTest {
 protected:
  WeightedTargetTest()
      : LoadBalancingPolicyTest("weighted_target_experimental") {}

  static RefCountedPtr<LoadBalancingPolicy::Config> MakeWeightedTargetConfig(
      const std::map<std::string, uint32_t>& targets) {
    Json::Object targets_json;
    for (const auto& [name, weight] : targets) {
      targets_json[name] = Json::FromObject({
          {"weight", Json::FromNumber(weight)},
          {"childPolicy", Json::FromArray({Json::FromObject(
                              {{"pick_first", Json::FromObject({})}})})},
      });
    }
    return MakeConfig(Json::FromArray({Json::FromObject(
        {{"weighted_target_experimental",
          Json::FromObject({{"targets", Json::FromObject(targets_json)}})}})}));
  }

  static EndpointAddresses MakeTargetEndpoint(absl::string_view address,
                                              absl::string_view target_name) {
    return MakeEndpointAddresses(
        {address}, ChannelArgs().SetObject(MakeRefCounted<HierarchicalPathArg>(
                       std::vector<RefCountedStringValue>{
                           RefCountedStringValue(target_name)})));
  }
};

TEST_F(WeightedTargetTest, ConnectingDelayReason) {
  constexpr absl::string_view kAddr0 = "ipv4:127.0.0.1:441";
  constexpr absl::string_view kAddr1 = "ipv4:127.0.0.1:442";
  auto config = MakeWeightedTargetConfig({{"target0", 1}, {"target1", 1}});
  std::vector<EndpointAddresses> endpoints = {
      MakeTargetEndpoint(kAddr0, "target0"),
      MakeTargetEndpoint(kAddr1, "target1"),
  };
  EXPECT_EQ(ApplyUpdate(BuildUpdate(endpoints, std::move(config)), lb_policy()),
            absl::OkStatus());
  auto* sc0 = FindSubchannel(kAddr0);
  auto* sc1 = FindSubchannel(kAddr1);
  ASSERT_NE(sc0, nullptr);
  ASSERT_NE(sc1, nullptr);
  EXPECT_TRUE(sc0->ConnectionRequested());
  EXPECT_TRUE(sc1->ConnectionRequested());
  // Initial state before children report pickers.
  ExpectConnectingUpdate(
      "weighted_target: connecting to targets (num_targets=2 num_connecting=2 "
      "num_idle=0 num_transient_failure=0), target target0: child policy has "
      "not yet returned a picker");
  // 1. Both children connecting -> delegates to target0.
  sc0->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectConnectingUpdate(absl::StrCat(
      "weighted_target: connecting to targets (num_targets=2 num_connecting=2 "
      "num_idle=0 num_transient_failure=0), target target0: pick_first: "
      "connecting to ",
      kAddr0, " (subchannel 1 of 1)"));
  sc1->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectConnectingUpdate(absl::StrCat(
      "weighted_target: connecting to targets (num_targets=2 num_connecting=2 "
      "num_idle=0 num_transient_failure=0), target target0: pick_first: "
      "connecting to ",
      kAddr0, " (subchannel 1 of 1)"));
  // 2. target0 is IDLE while target1 is CONNECTING -> prefers CONNECTING
  //    target1.
  sc0->SetConnectivityState(GRPC_CHANNEL_READY);
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  EXPECT_EQ(ExpectPickComplete(picker.get()), kAddr0);
  sc0->SetConnectivityState(GRPC_CHANNEL_IDLE);
  ExpectReresolutionRequest();
  EXPECT_TRUE(sc0->ConnectionRequested());
  ExpectConnectingUpdate(absl::StrCat(
      "weighted_target: connecting to targets (num_targets=2 num_connecting=1 "
      "num_idle=1 num_transient_failure=0), target target1: pick_first: "
      "connecting to ",
      kAddr1, " (subchannel 1 of 1)"));
  // 3. target1 fails while target0 is IDLE -> reports IDLE fallback, then
  //    CONNECTING as target0 reconnects, both with num_transient_failure=1
  //    and last failure.
  sc1->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                            absl::UnavailableError("sc1 failed"));
  ExpectReresolutionRequest();
  ExpectStateAndQueuingPicker(
      GRPC_CHANNEL_IDLE,
      "weighted_target: connecting to targets (num_targets=2 num_connecting=0 "
      "num_idle=1 num_transient_failure=1; last failure: UNAVAILABLE: "
      "failed to connect to all addresses; last error: UNAVAILABLE: "
      "sc1 failed), target target0: pick_first: connection closed; waiting "
      "for exit-idle signal to reconnect");
  sc0->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectConnectingUpdate(absl::StrCat(
      "weighted_target: connecting to targets (num_targets=2 num_connecting=1 "
      "num_idle=0 num_transient_failure=1; last failure: UNAVAILABLE: "
      "failed to connect to all addresses; last error: UNAVAILABLE: "
      "sc1 failed), target target0: pick_first: connecting to ",
      kAddr0, " (subchannel 1 of 1)"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
