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

#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_override_host.h"

#include <unordered_set>

#include "gmock/gmock.h"

#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class XdsOverrideHostTest : public LoadBalancingPolicyTest {
 protected:
  XdsOverrideHostTest()
      : policy_(MakeLbPolicy("xds_override_host_experimental")) {}

  RefCountedPtr<LoadBalancingPolicy::Config> MakeXdsOverrideHostConfig(
      std::string child_policy = "pick_first") {
    Json::Object child_policy_config = {{child_policy, Json::Object()}};
    return MakeConfig(Json::Array{Json::Object{
        {"xds_override_host_experimental",
         Json::Object{{"childPolicy", Json::Array{{child_policy_config}}}}}}});
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

TEST_F(XdsOverrideHostTest, NoConfigReportsError) {
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate({"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"}),
                  policy_.get()),
      absl::InvalidArgumentError("Missing policy config"));
}

TEST_F(XdsOverrideHostTest, UseHostCookie) {
  std::array<absl::string_view, 2> addresses = {"ipv4:127.0.0.1:441",
                                                "ipv4:127.0.0.1:442"};
  EXPECT_EQ(ApplyUpdate(BuildUpdate(addresses,
                                    MakeXdsOverrideHostConfig("round_robin")),
                        policy_.get()),
            absl::OkStatus());
  for (absl::string_view address : addresses) {
    auto subchannel = FindSubchannel({address});
    ASSERT_TRUE(subchannel);
    subchannel->ConnectionRequested();
    subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
    subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  }
  WaitForConnected();
  WaitForConnected();
  auto picker = WaitForConnected();
  std::unordered_set<std::string> picked;
  for (size_t i = 0; i < addresses.size(); i++) {
    auto pick = ExpectPickComplete(
        picker.get(),
        {{"Cookie",
          "cookie1=1;global-session-cookie=<anaddress>; another-cookie=boop"}});
    EXPECT_TRUE(pick.has_value());
    picked.insert(*pick);
  }
  EXPECT_THAT(picked, ::testing::UnorderedElementsAreArray(addresses));
}

// Disabled tests act as TODOs to outline missing features
TEST_F(XdsOverrideHostTest, DISABLED_CustomCookieName) {}

TEST_F(XdsOverrideHostTest, DISABLED_CustomTTL) {}

TEST_F(XdsOverrideHostTest, OverrideHostStatus) {
  const std::array<absl::string_view, 2> kAddresses = {"ipv4:127.0.0.1:441",
                                                       "ipv4:127.0.0.1:442"};
  EXPECT_EQ(policy_->name(), "xds_override_host_experimental");
  // 1. We use pick_first as a child
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses,
                                    MakeXdsOverrideHostConfig("round_robin")),
                        policy_.get()),
            absl::OkStatus());
  ExpectConnectingUpdate();
  EXPECT_NE(ExpectState(GRPC_CHANNEL_CONNECTING), nullptr);
  EXPECT_NE(ExpectState(GRPC_CHANNEL_CONNECTING), nullptr);
  // Ready up both subchannels
  for (auto address : kAddresses) {
    auto subchannel = FindSubchannel({address});
    ASSERT_NE(subchannel, nullptr);
    ASSERT_TRUE(subchannel->ConnectionRequested());
    subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
    subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  }
  ASSERT_NE(WaitForConnected(), nullptr);
  ASSERT_NE(ExpectState(GRPC_CHANNEL_READY), nullptr);
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  // Make sure child policy works
  EXPECT_NE(ExpectPickComplete(picker.get()), ExpectPickComplete(picker.get()));
  // Check that the host is overridden
  std::map<std::string, std::string> pick_arg{
      {std::string(kOverrideHostHeaderName), "127.0.0.1:442"}};
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[1]);
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[1]);
  pick_arg[std::string(kOverrideHostHeaderName)] = std::string("127.0.0.1:441");
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[0]);
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[0]);
}

TEST_F(XdsOverrideHostTest, DISABLED_OverridenHostFailure) {}
}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
