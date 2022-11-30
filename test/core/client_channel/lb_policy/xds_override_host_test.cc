//
// Copyright 2022 gRPC authors.
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

#include "gmock/gmock.h"

#include "src/core/lib/gprpp/env.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class XdsOverrideHostTest : public LoadBalancingPolicyTest {
 protected:
  RefCountedPtr<LoadBalancingPolicy::Config> MakeXdsOverrideHostConfig(
      std::string child_policy = "pick_first") {
    Json::Object child_policy_config = {{child_policy, Json::Object()}};
    return MakeConfig(Json::Array{Json::Object{
        {"xds_override_host_experimental",
         Json::Object{{"childPolicy", Json::Array{{child_policy_config}}}}}}});
  }

  void SetUp() override {
    grpc_core::SetEnv("GRPC_EXPERIMENTAL_XDS_ENABLE_HOST_OVERRIDE", "TRUE");
    grpc_init();
    policy_ = MakeLbPolicy("xds_override_host_experimental");
  }

  void TearDown() override {
    // Note: Can't safely trigger this from inside the FakeHelper dtor,
    // because if there is a picker in the queue that is holding a ref
    // to the LB policy, that will prevent the LB policy from being
    // destroyed, and therefore the FakeHelper will not be destroyed.
    // (This will cause an ASAN failure, but it will not display the
    // queued events, so the failure will be harder to diagnose.)
    if (policy_ != nullptr) {
      helper_->ExpectQueueEmpty();
    }
    policy_.reset();
    grpc_shutdown_blocking();
    grpc_core::UnsetEnv("GRPC_EXPERIMENTAL_XDS_ENABLE_HOST_OVERRIDE");
    grpc_core::CoreConfiguration::Reset();
  }

  OrphanablePtr<LoadBalancingPolicy> policy_;
};

TEST_F(XdsOverrideHostTest, DelegatesToChild) {
  ASSERT_NE(policy_, nullptr);
  const std::array<absl::string_view, 2> kAddresses = {"ipv4:127.0.0.1:441",
                                                       "ipv4:127.0.0.1:442"};
  EXPECT_EQ(policy_->name(), "xds_override_host_experimental");
  // 1. We use pick_first as a child
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, MakeXdsOverrideHostConfig()),
                        policy_.get()),
            absl::OkStatus());
  ExpectConnectingUpdate();
  auto subchannel =
      FindSubchannel({kAddresses[0]},
                     ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
  ASSERT_NE(subchannel, nullptr);
  ASSERT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  subchannel =
      FindSubchannel({kAddresses[1]},
                     ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
  ASSERT_NE(subchannel, nullptr);
  ASSERT_FALSE(subchannel->ConnectionRequested());
  auto picker = WaitForConnected();
  // Pick first policy will always pick first!
  EXPECT_EQ(ExpectPickComplete(picker.get()), "ipv4:127.0.0.1:441");
  EXPECT_EQ(ExpectPickComplete(picker.get()), "ipv4:127.0.0.1:441");
}

TEST_F(XdsOverrideHostTest, SwapChildPolicy) {
  ASSERT_NE(policy_, nullptr);
  const std::array<absl::string_view, 2> kAddresses = {"ipv4:127.0.0.1:441",
                                                       "ipv4:127.0.0.1:442"};
  EXPECT_EQ(policy_->name(), "xds_override_host_experimental");
  // 1. We use pick_first as a child
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, MakeXdsOverrideHostConfig()),
                        policy_.get()),
            absl::OkStatus());
  // Pick first will only request connection on a first child
  ExpectConnectingUpdate();
  auto subchannel =
      FindSubchannel({kAddresses[0]},
                     ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
  ASSERT_NE(subchannel, nullptr);
  ASSERT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  ASSERT_NE(WaitForConnected(), nullptr);
  subchannel =
      FindSubchannel({kAddresses[1]},
                     ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
  ASSERT_NE(subchannel, nullptr);
  ASSERT_FALSE(subchannel->ConnectionRequested());
  ExpectQueueEmpty();
  // 2. Now we switch to a round-robin
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses,
                                    MakeXdsOverrideHostConfig("round_robin")),
                        policy_.get()),
            absl::OkStatus());
  for (absl::string_view address : kAddresses) {
    auto subchannel = FindSubchannel({address});
    ASSERT_NE(subchannel, nullptr);
    ASSERT_TRUE(subchannel->ConnectionRequested());
    subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
    subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
    EXPECT_NE(ExpectState(GRPC_CHANNEL_READY), nullptr);
  }
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  ExpectPickComplete(picker.get());
  std::unordered_set<std::string> picked;
  for (size_t i = 0; i < kAddresses.size(); i++) {
    auto pick = ExpectPickComplete(picker.get());
    EXPECT_TRUE(pick.has_value());
    picked.insert(*pick);
  }
  EXPECT_THAT(picked, ::testing::UnorderedElementsAreArray(kAddresses));
}

TEST_F(XdsOverrideHostTest, NoConfigReportsError) {
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate({"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"}),
                  policy_.get()),
      absl::InvalidArgumentError("Missing policy config"));
}

TEST_F(XdsOverrideHostTest, ValidateChildPolicyConfig) {
  auto result =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::Array{Json::Object{
              {"xds_override_host_experimental", Json::Object{}}}});
  EXPECT_EQ(result.status(),
            absl::InvalidArgumentError(
                "errors validating xds_override_host LB policy config: "
                "[field:childPolicy error:field not present]"));

  result =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::Array{Json::Object{
              {"xds_override_host_experimental",
               Json::Object{
                   {"childPolicy",
                    Json::Array{
                        {Json::Object{{"pick_first", Json::Object{}}}},
                        {Json::Object{{"round_robin", Json::Object{}}}}}}}

              }}});
  EXPECT_EQ(result.status(),
            absl::InvalidArgumentError(
                "errors validating xds_override_host LB policy config: "
                "[field:childPolicy error:exactly one child config should be "
                "specified]"));

  result =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::Array{
              Json::Object{{"xds_override_host_experimental",
                            Json::Object{{
                                "childPolicy",
                                {Json::Object{{"pick_first", Json::Object{}}}},
                            }}}

              }});
  EXPECT_EQ(result.status(),
            absl::InvalidArgumentError(
                "errors validating xds_override_host LB policy config: "
                "[field:childPolicy error:type should be array]"));

  result =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::Array{Json::Object{{"xds_override_host_experimental",
                                    Json::Object{{"childPolicy", Json::Array{}}}

          }}});
  EXPECT_EQ(result.status(),
            absl::InvalidArgumentError(
                "errors validating xds_override_host LB policy config: "
                "[field:childPolicy error:No known policies in list: ]"));
}

TEST_F(XdsOverrideHostTest, XdsOverrideHostLbPolicyDisabled) {
  policy_.reset();
  grpc_shutdown_blocking();
  grpc_core::UnsetEnv("GRPC_EXPERIMENTAL_XDS_ENABLE_HOST_OVERRIDE");
  grpc_core::CoreConfiguration::Reset();
  grpc_init();
  auto policy = MakeLbPolicy("xds_override_host_experimental");
  ASSERT_EQ(policy, nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
