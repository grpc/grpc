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

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/support/json.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/stateful_session/stateful_session_filter.h"
#include "src/core/ext/xds/xds_health_status.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/resolver/endpoint_addresses.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {
class XdsOverrideHostTest : public LoadBalancingPolicyTest {
 protected:
  XdsOverrideHostTest()
      : LoadBalancingPolicyTest("xds_override_host_experimental") {}

  static RefCountedPtr<LoadBalancingPolicy::Config> MakeXdsOverrideHostConfig(
      absl::Span<const absl::string_view> override_host_status = {"UNKNOWN",
                                                                  "HEALTHY"},
      std::string child_policy = "round_robin") {
    Json child_policy_config =
        Json::FromObject({{child_policy, Json::FromObject({})}});
    Json::Array override_host_status_array;
    for (const absl::string_view host_status : override_host_status) {
      override_host_status_array.push_back(
          Json::FromString(std::string(host_status)));
    }
    return MakeConfig(Json::FromArray({Json::FromObject(
        {{"xds_override_host_experimental",
          Json::FromObject(
              {{"childPolicy", Json::FromArray({child_policy_config})},
               {"overrideHostStatus",
                Json::FromArray(override_host_status_array)}})}})}));
  }

  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>
  ExpectStartupWithRoundRobin(absl::Span<const absl::string_view> addresses) {
    EXPECT_EQ(ApplyUpdate(BuildUpdate(addresses, MakeXdsOverrideHostConfig()),
                          lb_policy()),
              absl::OkStatus());
    return ExpectRoundRobinStartup(addresses);
  }

  EndpointAddresses MakeAddressWithHealthStatus(
      absl::string_view address, XdsHealthStatus::HealthStatus status) {
    return EndpointAddresses(
        MakeAddress(address),
        ChannelArgs().Set(GRPC_ARG_XDS_HEALTH_STATUS, status));
  }

  void ApplyUpdateWithHealthStatuses(
      absl::Span<const std::pair<const absl::string_view,
                                 XdsHealthStatus::HealthStatus>>
          addresses_and_statuses,
      absl::Span<const absl::string_view> override_host_status = {"UNKNOWN",
                                                                  "HEALTHY"}) {
    LoadBalancingPolicy::UpdateArgs update;
    update.config = MakeXdsOverrideHostConfig(override_host_status);
    update.addresses.emplace();
    for (auto address_and_status : addresses_and_statuses) {
      update.addresses->push_back(MakeAddressWithHealthStatus(
          address_and_status.first, address_and_status.second));
    }
    EXPECT_EQ(ApplyUpdate(update, lb_policy()), absl::OkStatus());
  }

  struct OverrideHostAttributeStorage {
    // Need to store the string externally, since
    // XdsOverrideHostAttribute only holds a string_view.
    std::string address_list;
    XdsOverrideHostAttribute attribute;

    explicit OverrideHostAttributeStorage(std::string addresses)
        : address_list(std::move(addresses)), attribute(address_list) {}
  };

  XdsOverrideHostAttribute* MakeOverrideHostAttribute(
      absl::Span<const absl::string_view> addresses) {
    std::vector<absl::string_view> address_list;
    address_list.reserve(addresses.size());
    for (absl::string_view address : addresses) {
      address_list.emplace_back(absl::StripPrefix(address, "ipv4:"));
    }
    attribute_storage_.emplace_back(
        std::make_unique<OverrideHostAttributeStorage>(
            absl::StrJoin(address_list, ",")));
    return &attribute_storage_.back()->attribute;
  }

  XdsOverrideHostAttribute* MakeOverrideHostAttribute(
      absl::string_view address) {
    const std::array<absl::string_view, 1> addresses = {address};
    return MakeOverrideHostAttribute(addresses);
  }

  void ExpectOverridePicks(
      LoadBalancingPolicy::SubchannelPicker* picker,
      XdsOverrideHostAttribute* attribute, absl::string_view expected,
      absl::Span<const absl::string_view> expected_address_list = {},
      SourceLocation location = SourceLocation()) {
    std::array<absl::string_view, 1> kArray = {expected};
    if (expected_address_list.empty()) expected_address_list = kArray;
    std::vector<absl::string_view> expected_addresses;
    expected_addresses.reserve(expected_address_list.size());
    for (absl::string_view address : expected_address_list) {
      expected_addresses.push_back(absl::StripPrefix(address, "ipv4:"));
    }
    std::string expected_addresses_str = absl::StrJoin(expected_addresses, ",");
    for (size_t i = 0; i < 3; ++i) {
      EXPECT_EQ(
          ExpectPickComplete(picker, {attribute},
                             /*subchannel_call_tracker=*/nullptr, location),
          expected)
          << location.file() << ":" << location.line();
      EXPECT_EQ(attribute->actual_address_list(), expected_addresses_str)
          << "Expected: " << attribute->actual_address_list() << "\n"
          << "  Actual: " << expected_addresses_str << "\n"
          << location.file() << ":" << location.line();
    }
  }

  void ExpectRoundRobinPicksWithAttribute(
      LoadBalancingPolicy::SubchannelPicker* picker,
      XdsOverrideHostAttribute* attribute,
      absl::Span<const absl::string_view> expected,
      SourceLocation location = SourceLocation()) {
    std::vector<std::string> actual_picks;
    for (size_t i = 0; i < expected.size(); ++i) {
      auto address = ExpectPickComplete(
          picker, {attribute}, /*subchannel_call_tracker=*/nullptr, location);
      ASSERT_TRUE(address.has_value())
          << location.file() << ":" << location.line();
      EXPECT_THAT(*address, ::testing::AnyOfArray(expected))
          << location.file() << ":" << location.line();
      EXPECT_EQ(attribute->actual_address_list(),
                absl::StripPrefix(*address, "ipv4:"))
          << "Expected: " << attribute->actual_address_list() << "\n"
          << "  Actual: " << absl::StripPrefix(*address, "ipv4:") << "\n"
          << location.file() << ":" << location.line();
      actual_picks.push_back(std::move(*address));
    }
    EXPECT_TRUE(PicksAreRoundRobin(expected, actual_picks))
        << location.file() << ":" << location.line();
  }

  std::vector<std::unique_ptr<OverrideHostAttributeStorage>> attribute_storage_;
};

TEST_F(XdsOverrideHostTest, DelegatesToChild) {
  ExpectStartupWithRoundRobin(
      {"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"});
}

TEST_F(XdsOverrideHostTest, NoConfigReportsError) {
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate({"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"}, nullptr),
          lb_policy()),
      absl::InvalidArgumentError("Missing policy config"));
}

TEST_F(XdsOverrideHostTest, OverrideHost) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_NE(picker, nullptr);
  auto* address1_attribute = MakeOverrideHostAttribute(kAddresses[1]);
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
  auto* address0_attribute = MakeOverrideHostAttribute(kAddresses[0]);
  ExpectOverridePicks(picker.get(), address0_attribute, kAddresses[0]);
}

TEST_F(XdsOverrideHostTest, SubchannelNotFound) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_NE(picker, nullptr);
  auto* attribute = MakeOverrideHostAttribute("no such host");
  ExpectRoundRobinPicksWithAttribute(picker.get(), attribute, kAddresses);
}

TEST_F(XdsOverrideHostTest, SubchannelsComeAndGo) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_NE(picker, nullptr);
  // Check that the host override works.
  auto* address1_attribute = MakeOverrideHostAttribute(kAddresses[1]);
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
  // The override address is removed.
  EXPECT_EQ(ApplyUpdate(BuildUpdate({kAddresses[0], kAddresses[2]},
                                    MakeXdsOverrideHostConfig()),
                        lb_policy()),
            absl::OkStatus());
  picker =
      WaitForRoundRobinListChange(kAddresses, {kAddresses[0], kAddresses[2]});
  // Picks are returned in round-robin order, because the address
  // pointed to by the cookie is not present.
  ExpectRoundRobinPicksWithAttribute(picker.get(), address1_attribute,
                                     {kAddresses[0], kAddresses[2]});
  // The override address comes back.
  EXPECT_EQ(ApplyUpdate(BuildUpdate({kAddresses[1], kAddresses[2]},
                                    MakeXdsOverrideHostConfig()),
                        lb_policy()),
            absl::OkStatus());
  picker = WaitForRoundRobinListChange({kAddresses[0], kAddresses[2]},
                                       {kAddresses[1], kAddresses[2]});
  // Make sure host override works.
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
}

TEST_F(XdsOverrideHostTest,
       OverrideIsQueuedInIdleOrConnectingAndFailedInTransientFailure) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_NE(picker, nullptr);
  // Check that the host is overridden
  auto* address1_attribute = MakeOverrideHostAttribute(kAddresses[1]);
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
  // Subchannel for address 1 becomes disconnected.
  gpr_log(GPR_INFO, "### subchannel 1 reporting IDLE");
  auto subchannel = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel, nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  gpr_log(GPR_INFO, "### expecting re-resolution request");
  ExpectReresolutionRequest();
  gpr_log(GPR_INFO,
          "### expecting RR picks to exclude the disconnected subchannel");
  picker =
      WaitForRoundRobinListChange(kAddresses, {kAddresses[0], kAddresses[2]});
  // Picks with the override will be queued.
  ExpectPickQueued(picker.get(), {address1_attribute});
  // The subchannel starts trying to reconnect.
  gpr_log(GPR_INFO, "### subchannel 1 reporting CONNECTING");
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
  // Picks with the override will still be queued.
  ExpectPickQueued(picker.get(), {address1_attribute});
  // The connection attempt fails.
  gpr_log(GPR_INFO, "### subchannel 1 reporting TRANSIENT_FAILURE");
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::ResourceExhaustedError("Hmmmm"));
  gpr_log(GPR_INFO, "### expecting re-resolution request");
  ExpectReresolutionRequest();
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
  // The host override is not used.
  gpr_log(GPR_INFO, "### checking that host override is not used");
  ExpectRoundRobinPicksWithAttribute(picker.get(), address1_attribute,
                                     {kAddresses[0], kAddresses[2]});
}

TEST_F(XdsOverrideHostTest, DrainingState) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  ASSERT_NE(ExpectStartupWithRoundRobin(kAddresses), nullptr);
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kDraining},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kHealthy}},
      {"UNKNOWN", "HEALTHY", "DRAINING"});
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  // Picks without an override will round-robin over the two endpoints
  // that are not in draining state.
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
  // Picks with an override are able to select the draining endpoint.
  auto* address1_attribute = MakeOverrideHostAttribute(kAddresses[1]);
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
  // Send the LB policy an update that removes the draining endpoint.
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kHealthy}});
  picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  // Gone!
  ExpectRoundRobinPicksWithAttribute(picker.get(), address1_attribute,
                                     {kAddresses[0], kAddresses[2]});
}

TEST_F(XdsOverrideHostTest, DrainingSubchannelIsConnecting) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = ExpectStartupWithRoundRobin(kAddresses);
  ASSERT_NE(picker, nullptr);
  // Check that the host is overridden
  auto* address1_attribute = MakeOverrideHostAttribute(kAddresses[1]);
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
  // Send an update that marks the endpoints with different EDS health
  // states, but those states are present in override_host_status.
  // The picker should use the DRAINING host when a call's override
  // points to that hose, but the host should not be used if there is no
  // override pointing to it.
  gpr_log(GPR_INFO, "### sending update with DRAINING host");
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kDraining},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kHealthy}},
      {"UNKNOWN", "HEALTHY", "DRAINING"});
  auto subchannel = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel, nullptr);
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
  // Now the connection to the draining host gets dropped.
  // The picker should queue picks where the override host is IDLE.
  // All picks without an override host should not use this host.
  gpr_log(GPR_INFO, "### closing connection to DRAINING host");
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectPickQueued(picker.get(), {address1_attribute});
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
  // The subchannel should have been asked to reconnect as a result of the
  // queued pick above.  It will therefore transition into state CONNECTING.
  // The pick behavior is the same as above: The picker should queue
  // picks where the override host is CONNECTING.  All picks without an
  // override host should not use this host.
  gpr_log(GPR_INFO, "### subchannel starts reconnecting");
  WaitForWorkSerializerToFlush();
  EXPECT_TRUE(subchannel->ConnectionRequested());
  ExpectQueueEmpty();
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectPickQueued(picker.get(), {address1_attribute});
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
  // The subchannel now becomes connected again.
  // Now picks with this override host can be completed again.
  // Picks without an override host still don't use the draining host.
  gpr_log(GPR_INFO, "### subchannel becomes reconnected");
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
}

TEST_F(XdsOverrideHostTest, DrainingToHealthy) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  ASSERT_NE(ExpectStartupWithRoundRobin(kAddresses), nullptr);
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kDraining},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kHealthy}},
      {"UNKNOWN", "HEALTHY", "DRAINING"});
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[2]});
  auto* address1_attribute = MakeOverrideHostAttribute(kAddresses[1]);
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kHealthy},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kHealthy},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kHealthy}},
      {"UNKNOWN", "HEALTHY", "DRAINING"});
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
  ExpectRoundRobinPicks(picker.get(), kAddresses);
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
  auto* address0_attribute = MakeOverrideHostAttribute(kAddresses[0]);
  ExpectOverridePicks(picker.get(), address0_attribute, kAddresses[0]);
  auto* address1_attribute = MakeOverrideHostAttribute(kAddresses[1]);
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
  auto* address2_attribute = MakeOverrideHostAttribute(kAddresses[2]);
  ExpectOverridePicks(picker.get(), address2_attribute, kAddresses[2]);
  // UNKNOWN excluded: overrides for first endpoint are not honored.
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kHealthy},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kDraining}},
      {"HEALTHY", "DRAINING"});
  picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[1]});
  ExpectRoundRobinPicksWithAttribute(picker.get(), address0_attribute,
                                     {kAddresses[0], kAddresses[1]});
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
  ExpectOverridePicks(picker.get(), address2_attribute, kAddresses[2]);
  // HEALTHY excluded: overrides for second endpoint are not honored.
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kHealthy},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kDraining}},
      {"UNKNOWN", "DRAINING"});
  picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[1]});
  ExpectOverridePicks(picker.get(), address0_attribute, kAddresses[0]);
  ExpectRoundRobinPicksWithAttribute(picker.get(), address1_attribute,
                                     {kAddresses[0], kAddresses[1]});
  ExpectOverridePicks(picker.get(), address2_attribute, kAddresses[2]);
  // DRAINING excluded: overrides for third endpoint are not honored.
  ApplyUpdateWithHealthStatuses(
      {{kAddresses[0], XdsHealthStatus::HealthStatus::kUnknown},
       {kAddresses[1], XdsHealthStatus::HealthStatus::kHealthy},
       {kAddresses[2], XdsHealthStatus::HealthStatus::kDraining}},
      {"UNKNOWN", "HEALTHY"});
  picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  ExpectRoundRobinPicks(picker.get(), {kAddresses[0], kAddresses[1]});
  ExpectOverridePicks(picker.get(), address0_attribute, kAddresses[0]);
  ExpectOverridePicks(picker.get(), address1_attribute, kAddresses[1]);
  ExpectRoundRobinPicksWithAttribute(picker.get(), address2_attribute,
                                     {kAddresses[0], kAddresses[1]});
}

TEST_F(XdsOverrideHostTest, MultipleAddressesPerEndpoint) {
  if (!IsRoundRobinDelegateToPickFirstEnabled()) return;
  constexpr std::array<absl::string_view, 2> kEndpoint1Addresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  constexpr std::array<absl::string_view, 2> kEndpoint2Addresses = {
      "ipv4:127.0.0.1:445", "ipv4:127.0.0.1:446"};
  constexpr std::array<absl::string_view, 2> kEndpoint3Addresses = {
      "ipv4:127.0.0.1:447", "ipv4:127.0.0.1:448"};
  const std::array<EndpointAddresses, 3> kEndpoints = {
      MakeEndpointAddresses(kEndpoint1Addresses),
      MakeEndpointAddresses(kEndpoint2Addresses),
      MakeEndpointAddresses(kEndpoint3Addresses)};
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kEndpoints, MakeXdsOverrideHostConfig()),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectRoundRobinStartup(kEndpoints);
  ASSERT_NE(picker, nullptr);
  // Check that the host is overridden.
  auto* endpoint1_attribute = MakeOverrideHostAttribute(kEndpoint1Addresses);
  ExpectOverridePicks(picker.get(), endpoint1_attribute, kEndpoint1Addresses[0],
                      kEndpoint1Addresses);
  auto* endpoint2_attribute = MakeOverrideHostAttribute(kEndpoint2Addresses);
  ExpectOverridePicks(picker.get(), endpoint2_attribute, kEndpoint2Addresses[0],
                      kEndpoint2Addresses);
  // Change endpoint 1 to connect to its second address.
  ExpectEndpointAddressChange(kEndpoint1Addresses, 0, 1, [&]() {
    WaitForRoundRobinListChange(
        {kEndpoint1Addresses[0], kEndpoint2Addresses[0],
         kEndpoint3Addresses[0]},
        {kEndpoint2Addresses[0], kEndpoint3Addresses[0]});
  });
  WaitForRoundRobinListChange(
      {kEndpoint2Addresses[0], kEndpoint3Addresses[0]},
      {kEndpoint1Addresses[1], kEndpoint2Addresses[0], kEndpoint3Addresses[0]});
  // Now the cookie for endpoint 1 should cause us to use the second address.
  ExpectOverridePicks(picker.get(), endpoint1_attribute, kEndpoint1Addresses[1],
                      {kEndpoint1Addresses[1], kEndpoint1Addresses[0]});
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
