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

#include <unordered_set>

#include "gmock/gmock.h"

#include "src/core/ext/filters/stateful_session/stateful_session_filter.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

using ::testing::UnorderedElementsAreArray;

class XdsOverrideHostTest : public LoadBalancingPolicyTest {
 protected:
  XdsOverrideHostTest()
      : policy_(MakeLbPolicy("xds_override_host_experimental")) {}

  RefCountedPtr<LoadBalancingPolicy::Config> MakeXdsOverrideHostConfig(
      std::string child_policy = "round_robin") {
    Json::Object child_policy_config = {{child_policy, Json::Object()}};
    return MakeConfig(Json::Array{Json::Object{
        {"xds_override_host_experimental",
         Json::Object{{"childPolicy", Json::Array{{child_policy_config}}}}}}});
  }

  absl::optional<RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>>
  ExpectStartupWithRoundRobin(absl::Span<const absl::string_view> addresses) {
    absl::optional<RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>>
        maybe_picker;
    EXPECT_EQ(ApplyUpdate(BuildUpdate(addresses, MakeXdsOverrideHostConfig()),
                          policy_.get()),
              absl::OkStatus());
    ExpectConnectingUpdate();
    RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker;
    for (size_t i = 0; i < addresses.size(); ++i) {
      auto* subchannel = FindSubchannel(addresses[i]);
      EXPECT_NE(subchannel, nullptr);
      if (subchannel == nullptr) {
        return absl::nullopt;
      }
      EXPECT_TRUE(subchannel->ConnectionRequested());
      subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
      subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
      if (i == 0) {
        auto picker = WaitForConnected();
        ExpectRoundRobinPicks(picker.get(), {addresses[0]});
      } else {
        maybe_picker = WaitForRoundRobinListChange(
            absl::MakeSpan(addresses).subspan(0, i),
            absl::MakeSpan(addresses).subspan(0, i + 1));
      }
    }
    return maybe_picker;
  }

  OrphanablePtr<LoadBalancingPolicy> policy_;
};

TEST_F(XdsOverrideHostTest, DelegatesToChild) {
  ExpectStartupWithRoundRobin(
      {"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"});
}

TEST_F(XdsOverrideHostTest, NoConfigReportsError) {
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate({"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"}),
                  policy_.get()),
      absl::InvalidArgumentError("Missing policy config"));
}

TEST_F(XdsOverrideHostTest, OverrideHost) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto maybe_picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_TRUE(maybe_picker.has_value());
  // Check that the host is overridden
  std::map<UniqueTypeName, std::string> call_attributes{
      {XdsHostOverrideTypeName(), "127.0.0.1:442"}};
  EXPECT_EQ(ExpectPickComplete(maybe_picker->get(), call_attributes),
            kAddresses[1]);
  EXPECT_EQ(ExpectPickComplete(maybe_picker->get(), call_attributes),
            kAddresses[1]);
  call_attributes[XdsHostOverrideTypeName()] = std::string("127.0.0.1:441");
  EXPECT_EQ(ExpectPickComplete(maybe_picker->get(), call_attributes),
            kAddresses[0]);
  EXPECT_EQ(ExpectPickComplete(maybe_picker->get(), call_attributes),
            kAddresses[0]);
}

TEST_F(XdsOverrideHostTest, OverrideHostSubchannelNotFound) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto maybe_picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_TRUE(maybe_picker.has_value());
  std::map<UniqueTypeName, std::string> call_attributes{
      {XdsHostOverrideTypeName(), "no such host"}};
  ExpectRoundRobinPicks(maybe_picker->get(), kAddresses, call_attributes);
}

TEST_F(XdsOverrideHostTest, SubchannelsComeAndGo) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto maybe_picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_TRUE(maybe_picker.has_value());
  // Check that the host is overridden
  std::map<UniqueTypeName, std::string> call_attributes{
      {XdsHostOverrideTypeName(), "127.0.0.1:442"}};
  ExpectRoundRobinPicks(maybe_picker->get(), {kAddresses[1]}, call_attributes);
  // Some other address is gone
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate({"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"},
                              MakeXdsOverrideHostConfig("round_robin")),
                  policy_.get()),
      absl::OkStatus());
  auto picks = GetCompletePicks(ExpectState(GRPC_CHANNEL_READY).get(), {}, 2);
  ASSERT_TRUE(picks.has_value());
  std::vector<absl::string_view> pick_addresses(picks->begin(), picks->end());
  EXPECT_TRUE(PicksAreRoundRobin({"ipv4:127.0.0.1:441"}, pick_addresses));
  WaitForRoundRobinListChange(
      {"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"},
      {"ipv4:127.0.0.1:442"}, call_attributes);
  // "Our" address is gone so others get returned in round-robin order
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate({"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:443"},
                              MakeXdsOverrideHostConfig("round_robin")),
                  policy_.get()),
      absl::OkStatus());
  picks = GetCompletePicks(ExpectState(GRPC_CHANNEL_READY).get(), {}, 2);
  ASSERT_TRUE(picks.has_value());
  pick_addresses = {picks->begin(), picks->end()};
  EXPECT_TRUE(PicksAreRoundRobin({"ipv4:127.0.0.1:441"}, pick_addresses));
  WaitForRoundRobinListChange({"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"},
                              {"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:443"},
                              call_attributes);
  // And now it is back
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate({"ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"},
                              MakeXdsOverrideHostConfig("round_robin")),
                  policy_.get()),
      absl::OkStatus());
  picks = GetCompletePicks(ExpectState(GRPC_CHANNEL_READY).get(), {}, 2);
  ASSERT_TRUE(picks.has_value());
  pick_addresses = {picks->begin(), picks->end()};
  EXPECT_TRUE(PicksAreRoundRobin({"ipv4:127.0.0.1:442"}, pick_addresses));
  WaitForRoundRobinListChange({"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:443"},
                              {"ipv4:127.0.0.1:442"}, call_attributes);
}
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
