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

#include <stddef.h>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/stateful_session/stateful_session_filter.h"
#include "src/core/ext/xds/xds_health_status.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/resolver/server_address.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {
class XdsOverrideHostTest : public LoadBalancingPolicyTest {
 protected:
  XdsOverrideHostTest()
      : policy_(MakeLbPolicy("xds_override_host_experimental")) {}

  static RefCountedPtr<LoadBalancingPolicy::Config> MakeXdsOverrideHostConfig(
      Json::Array override_host_status = {"UNKNOWN", "HEALTHY"},
      std::string child_policy = "round_robin") {
    Json::Object child_policy_config = {{child_policy, Json::Object()}};
    return MakeConfig(Json::Array{Json::Object{
        {"xds_override_host_experimental",
         Json::Object{{"childPolicy", Json::Array{{child_policy_config}}},
                      {"overrideHostStatus", override_host_status}}}}});
  }

  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>
  ExpectStartupWithRoundRobin(absl::Span<const absl::string_view> addresses,
                              RefCountedPtr<LoadBalancingPolicy::Config>
                                  config = MakeXdsOverrideHostConfig()) {
    RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker;
    EXPECT_EQ(ApplyUpdate(BuildUpdate(addresses, config), policy_.get()),
              absl::OkStatus());
    ExpectConnectingUpdate();
    for (size_t i = 0; i < addresses.size(); ++i) {
      auto* subchannel = FindSubchannel(addresses[i]);
      EXPECT_NE(subchannel, nullptr);
      if (subchannel == nullptr) return nullptr;
      EXPECT_TRUE(subchannel->ConnectionRequested());
      subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
      subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
      if (i == 0) {
        picker = WaitForConnected();
        ExpectRoundRobinPicks(picker.get(), {addresses[0]});
      } else {
        picker = WaitForRoundRobinListChange(
            absl::MakeSpan(addresses).subspan(0, i),
            absl::MakeSpan(addresses).subspan(0, i + 1));
      }
    }
    return picker;
  }

  ServerAddress MakeAddressWithHealthStatus(
      absl::string_view address, XdsHealthStatus::HealthStatus status) {
    std::map<const char*, std::unique_ptr<ServerAddress::AttributeInterface>>
        attrs;
    attrs.emplace(XdsEndpointHealthStatusAttribute::kKey,
                  std::make_unique<XdsEndpointHealthStatusAttribute>(
                      XdsHealthStatus(status)));
    return {MakeAddress(address), {}, std::move(attrs)};
  }

  void ApplyUpdateWithHealthStatuses(
      absl::Span<const std::pair<const absl::string_view,
                                 XdsHealthStatus::HealthStatus>>
          addresses_and_statuses,
      Json::Array override_host_status = {"UNKNOWN", "HEALTHY"}) {
    LoadBalancingPolicy::UpdateArgs update;
    update.config = MakeXdsOverrideHostConfig(std::move(override_host_status));
    update.addresses.emplace();
    for (auto address_and_status : addresses_and_statuses) {
      update.addresses->push_back(MakeAddressWithHealthStatus(
          address_and_status.first, address_and_status.second));
    }
    EXPECT_EQ(ApplyUpdate(update, policy_.get()), absl::OkStatus());
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
  auto picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_NE(picker, nullptr);
  // Check that the host is overridden
  std::map<UniqueTypeName, absl::string_view> call_attributes{
      {XdsOverrideHostTypeName(), kAddresses[1]}};
  EXPECT_EQ(ExpectPickComplete(picker.get(), call_attributes), kAddresses[1]);
  EXPECT_EQ(ExpectPickComplete(picker.get(), call_attributes), kAddresses[1]);
  call_attributes[XdsOverrideHostTypeName()] = kAddresses[0];
  EXPECT_EQ(ExpectPickComplete(picker.get(), call_attributes), kAddresses[0]);
  EXPECT_EQ(ExpectPickComplete(picker.get(), call_attributes), kAddresses[0]);
}

TEST_F(XdsOverrideHostTest, SubchannelNotFound) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_NE(picker, nullptr);
  std::map<UniqueTypeName, absl::string_view> call_attributes{
      {XdsOverrideHostTypeName(), "no such host"}};
  ExpectRoundRobinPicks(picker.get(), kAddresses, call_attributes);
}

TEST_F(XdsOverrideHostTest, SubchannelsComeAndGo) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_NE(picker, nullptr);
  // Check that the host is overridden
  std::map<UniqueTypeName, absl::string_view> call_attributes{
      {XdsOverrideHostTypeName(), kAddresses[1]}};
  ExpectRoundRobinPicks(picker.get(), {kAddresses[1]}, call_attributes);
  // Some other address is gone
  EXPECT_EQ(ApplyUpdate(BuildUpdate({kAddresses[0], kAddresses[1]},
                                    MakeXdsOverrideHostConfig()),
                        policy_.get()),
            absl::OkStatus());
  // Wait for LB policy to return a new picker that uses the updated
  // addresses.  We can't use the host override for this, because then
  // we won't know when the new picker is actually using all of the new
  // addresses.
  picker =
      WaitForRoundRobinListChange(kAddresses, {kAddresses[0], kAddresses[1]});
  // Make sure host override still works.
  ExpectRoundRobinPicks(picker.get(), {kAddresses[1]}, call_attributes);
  // "Our" address is gone so others get returned in round-robin order
  EXPECT_EQ(ApplyUpdate(BuildUpdate({kAddresses[0], kAddresses[2]},
                                    MakeXdsOverrideHostConfig()),
                        policy_.get()),
            absl::OkStatus());
  // Wait for LB policy to return the new picker.
  // In this case, we can pass call_attributes while we wait instead of
  // checking again afterward, because the host override won't actually
  // be used.
  WaitForRoundRobinListChange({kAddresses[0], kAddresses[1]},
                              {kAddresses[0], kAddresses[2]}, call_attributes);
  // And now it is back
  EXPECT_EQ(ApplyUpdate(BuildUpdate({kAddresses[1], kAddresses[2]},
                                    MakeXdsOverrideHostConfig()),
                        policy_.get()),
            absl::OkStatus());
  // Wait for LB policy to return the new picker.
  picker = WaitForRoundRobinListChange({kAddresses[0], kAddresses[2]},
                                       {kAddresses[1], kAddresses[2]});
  // Make sure host override works.
  ExpectRoundRobinPicks(picker.get(), {kAddresses[1]}, call_attributes);
}

TEST_F(XdsOverrideHostTest, FailedSubchannelIsNotPicked) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_NE(picker, nullptr);
  // Check that the host is overridden
  std::map<UniqueTypeName, absl::string_view> pick_arg{
      {XdsOverrideHostTypeName(), kAddresses[1]}};
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[1]);
  auto subchannel = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel, nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  ExpectReresolutionRequest();
  ExpectRoundRobinPicks(ExpectState(GRPC_CHANNEL_READY).get(),
                        {kAddresses[0], kAddresses[2]});
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectRoundRobinPicks(ExpectState(GRPC_CHANNEL_READY).get(),
                        {kAddresses[0], kAddresses[2]});
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::ResourceExhaustedError("Hmmmm"));
  ExpectReresolutionRequest();
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]}, pick_arg);
}

TEST_F(XdsOverrideHostTest, ConnectingSubchannelIsQueued) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_NE(picker, nullptr);
  // Check that the host is overridden
  std::map<UniqueTypeName, absl::string_view> pick_arg{
      {XdsOverrideHostTypeName(), kAddresses[1]}};
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[1]);
  auto subchannel = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel, nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  ExpectReresolutionRequest();
  EXPECT_TRUE(subchannel->ConnectionRequested());
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectPickQueued(picker.get(), pick_arg);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectPickQueued(picker.get(), pick_arg);
}

TEST_F(XdsOverrideHostTest, DrainingState) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  ASSERT_NE(ExpectStartupWithRoundRobin(kAddresses), nullptr);
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kDraining},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kHealthy}},
      {"UNKNOWN", "HEALTHY", "DRAINING"});
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
  ExpectQueueEmpty();
  // Draining subchannel is returned
  std::map<UniqueTypeName, absl::string_view> pick_arg{
      {XdsOverrideHostTypeName(), kAddresses[1]}};
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[1]);
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kHealthy}});
  picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  // Gone!
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]}, pick_arg);
}

TEST_F(XdsOverrideHostTest, DrainingSubchannelIsConnecting) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_NE(picker, nullptr);
  // Check that the host is overridden
  std::map<UniqueTypeName, absl::string_view> pick_arg{
      {XdsOverrideHostTypeName(), kAddresses[1]}};
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[1]);
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kDraining},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kHealthy}},
      {"UNKNOWN", "HEALTHY", "DRAINING"});
  auto subchannel = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel, nullptr);
  // There are two notifications - one from child policy and one from the parent
  // policy due to draining channel update
  picker = ExpectState(GRPC_CHANNEL_READY);
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[1]);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectPickQueued(picker.get(), pick_arg);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
  EXPECT_TRUE(subchannel->ConnectionRequested());
  ExpectQueueEmpty();
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectPickQueued(picker.get(), pick_arg);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[1]);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
}

TEST_F(XdsOverrideHostTest, DrainingToHealthy) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  ASSERT_NE(ExpectStartupWithRoundRobin(kAddresses), nullptr);
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kDraining},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kHealthy}},
      {"UNKNOWN", "HEALTHY", "DRAINING"});
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
  ExpectQueueEmpty();
  std::map<UniqueTypeName, absl::string_view> pick_arg{
      {XdsOverrideHostTypeName(), kAddresses[1]}};
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[1]);
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kHealthy},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kHealthy},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kHealthy}},
      {"UNKNOWN", "HEALTHY", "DRAINING"});
  picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[1]);
  EXPECT_EQ(ExpectPickComplete(picker.get(), pick_arg), kAddresses[1]);
}

TEST_F(XdsOverrideHostTest, OverrideHostStatus) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  ASSERT_NE(ExpectStartupWithRoundRobin(kAddresses), nullptr);
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kHealthy},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kDraining}},
      {"UNKNOWN", "HEALTHY", "DRAINING"});
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[1]});
  EXPECT_EQ(ExpectPickComplete(picker.get(),
                               {{XdsOverrideHostTypeName(), kAddresses[0]}}),
            kAddresses[0]);
  EXPECT_EQ(ExpectPickComplete(picker.get(),
                               {{XdsOverrideHostTypeName(), kAddresses[1]}}),
            kAddresses[1]);
  EXPECT_EQ(ExpectPickComplete(picker.get(),
                               {{XdsOverrideHostTypeName(), kAddresses[2]}}),
            kAddresses[2]);
  // UNKNOWN excluded - first chanel does not get overridden
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kHealthy},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kDraining}},
      {"HEALTHY", "DRAINING"});
  picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[1]});
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[1]},
                        {{XdsOverrideHostTypeName(), kAddresses[0]}});
  EXPECT_EQ(ExpectPickComplete(picker.get(),
                               {{XdsOverrideHostTypeName(), kAddresses[1]}}),
            kAddresses[1]);
  EXPECT_EQ(ExpectPickComplete(picker.get(),
                               {{XdsOverrideHostTypeName(), kAddresses[2]}}),
            kAddresses[2]);
  // HEALTHY excluded - second chanel does not get overridden
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kHealthy},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kDraining}},
      {"UNKNOWN", "HEALTHY"});
  picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[1]});
  EXPECT_EQ(ExpectPickComplete(picker.get(),
                               {{XdsOverrideHostTypeName(), kAddresses[0]}}),
            kAddresses[0]);
  EXPECT_EQ(ExpectPickComplete(picker.get(),
                               {{XdsOverrideHostTypeName(), kAddresses[1]}}),
            kAddresses[1]);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[1]},
                        {{XdsOverrideHostTypeName(), kAddresses[2]}});
  // DRAINING excluded - third chanel does not get overridden
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kHealthy},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kDraining}},
      {"UNKNOWN", "HEALTHY"});
  picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[1]});
  EXPECT_EQ(ExpectPickComplete(picker.get(),
                               {{XdsOverrideHostTypeName(), kAddresses[0]}}),
            kAddresses[0]);
  EXPECT_EQ(ExpectPickComplete(picker.get(),
                               {{XdsOverrideHostTypeName(), kAddresses[1]}}),
            kAddresses[1]);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[1]},
                        {{XdsOverrideHostTypeName(), kAddresses[2]}});
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
