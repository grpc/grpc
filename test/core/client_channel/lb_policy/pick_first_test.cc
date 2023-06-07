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
#include <map>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/support/json.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/scoped_env_var.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class PickFirstTest : public LoadBalancingPolicyTest {
 protected:
  PickFirstTest() : lb_policy_(MakeLbPolicy("pick_first")) {}

  static RefCountedPtr<LoadBalancingPolicy::Config> MakePickFirstConfig(
      bool shuffle_address_list) {
    return MakeConfig(Json::FromArray({Json::FromObject({{
        "pick_first",
        Json::FromObject(
            {{"shuffleAddressList", Json::FromBool(shuffle_address_list)}}),
    }})}));
  }

  // Gets order the addresses are being picked. Return type is void so
  // assertions can be used
  void GetOrderAddressesArePicked(
      absl::Span<const absl::string_view> addresses,
      std::vector<absl::string_view>* out_address_order) {
    // Construct a map of subchannel to address.
    // We will remove entries as each subchannel starts to connect.
    std::map<SubchannelState*, absl::string_view> subchannels;
    for (auto address : addresses) {
      auto* subchannel = FindSubchannel(
          address, ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
      ASSERT_NE(subchannel, nullptr);
      subchannels.emplace(subchannel, address);
    }
    // Now process each subchannel in the order in which pick_first tries it.
    while (!subchannels.empty()) {
      // Find the subchannel that is being attempted.
      SubchannelState* subchannel = nullptr;
      for (const auto& p : subchannels) {
        if (p.first->ConnectionRequested()) {
          out_address_order->push_back(p.second);
          subchannel = p.first;
          break;
        }
      }
      ASSERT_NE(subchannel, nullptr);
      // The subchannel reports CONNECTING.
      subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
      // If this is the first subchannel being attempted, expect a CONNECTING
      // update.
      if (subchannels.size() == addresses.size()) {
        ExpectConnectingUpdate();
      }
      if (subchannels.size() > 1) {
        // Not the last subchannel in the list.  Connection attempt should fail.
        subchannel->SetConnectivityState(
            GRPC_CHANNEL_TRANSIENT_FAILURE,
            absl::UnavailableError("failed to connect"));
        subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
      } else {
        // Last subchannel in the list.  Connection attempt should succeed.
        subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
        auto picker = WaitForConnected();
        ASSERT_NE(picker, nullptr);
        EXPECT_EQ(ExpectPickComplete(picker.get()), out_address_order->back());
        // Then it should become disconnected.
        subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
        ExpectReresolutionRequest();
        ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE);
      }
      // Remove the subchannel from the map.
      subchannels.erase(subchannel);
    }
  }

  OrphanablePtr<LoadBalancingPolicy> lb_policy_;
};

TEST_F(PickFirstTest, FirstAddressWorks) {
  // Send an update containing two addresses.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  absl::Status status = ApplyUpdate(BuildUpdate(kAddresses), lb_policy_.get());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses with
  // the GRPC_ARG_INHIBIT_HEALTH_CHECKING channel arg.
  auto* subchannel = FindSubchannel(
      kAddresses[0], ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(
      kAddresses[1], ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
  ASSERT_NE(subchannel2, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // The second subchannel should not be connecting.
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // When the first subchannel becomes connected, it reports READY.
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report CONNECTING some number of times (doesn't
  // matter how many) and then report READY.
  auto picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[0]);
  }
}

TEST_F(PickFirstTest, FirstAddressFails) {
  // Send an update containing two addresses.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  absl::Status status = ApplyUpdate(BuildUpdate(kAddresses), lb_policy_.get());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses with
  // the GRPC_ARG_INHIBIT_HEALTH_CHECKING channel arg.
  auto* subchannel = FindSubchannel(
      kAddresses[0], ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(
      kAddresses[1], ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
  ASSERT_NE(subchannel2, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // The second subchannel should not be connecting.
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // The first subchannel's connection attempt fails.
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("failed to connect"));
  // The LB policy will start a connection attempt on the second subchannel.
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports CONNECTING.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // The connection attempt succeeds.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report CONNECTING some number of times (doesn't
  // matter how many) and then report READY.
  auto picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[1]);
  }
}

TEST_F(PickFirstTest, GoesIdleWhenConnectionFailsThenCanReconnect) {
  // Send an update containing two addresses.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  absl::Status status = ApplyUpdate(BuildUpdate(kAddresses), lb_policy_.get());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses with
  // the GRPC_ARG_INHIBIT_HEALTH_CHECKING channel arg.
  auto* subchannel = FindSubchannel(
      kAddresses[0], ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(
      kAddresses[1], ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
  ASSERT_NE(subchannel2, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // The second subchannel should not be connecting.
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // When the first subchannel becomes connected, it reports READY.
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report CONNECTING some number of times (doesn't
  // matter how many) and then report READY.
  auto picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[0]);
  }
  // Connection fails.
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  // We should see a re-resolution request.
  ExpectReresolutionRequest();
  // LB policy reports IDLE with a queueing picker.
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE);
  // By checking the picker, we told the LB policy to trigger a new
  // connection attempt, so it should start over with the first
  // subchannel.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // The subchannel starts connecting.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // Subchannel succeeds in connecting.
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  // LB policy reports READY.
  picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[0]);
  }
}

TEST_F(PickFirstTest, WithShuffle) {
  testing::ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_PICKFIRST_LB_CONFIG");
  // 6 addresses have 6! = 720 permutations or 0.1% chance that the shuffle
  // returns initial sequence
  constexpr std::array<absl::string_view, 6> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445",
      "ipv4:127.0.0.1:446", "ipv4:127.0.0.1:447", "ipv4:127.0.0.1:448"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(true)), lb_policy_.get());
  EXPECT_TRUE(status.ok()) << status;
  std::vector<absl::string_view> prev_attempt_connect_order;
  GetOrderAddressesArePicked(kAddresses, &prev_attempt_connect_order);
  // There is 0.1% chance this check fails by design. Not an assert to prevent
  // flake
  if (absl::MakeConstSpan(prev_attempt_connect_order) ==
      absl::MakeConstSpan(kAddresses)) {
    gpr_log(GPR_INFO, "Address order did not change");
  }
  constexpr size_t kMaxAttempts = 5;
  bool shuffled = false;
  for (size_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
    std::vector<absl::string_view> address_order;
    GetOrderAddressesArePicked(kAddresses, &address_order);
    if (address_order != prev_attempt_connect_order) {
      shuffled = true;
      break;
    }
    prev_attempt_connect_order = std::move(address_order);
  }
  ASSERT_TRUE(shuffled) << "Addresses are not reshuffled";
}

TEST_F(PickFirstTest, ShufflingDisabled) {
  constexpr std::array<absl::string_view, 6> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445",
      "ipv4:127.0.0.1:446", "ipv4:127.0.0.1:447", "ipv4:127.0.0.1:448"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(true)), lb_policy_.get());
  EXPECT_TRUE(status.ok()) << status;
  constexpr static size_t kMaxAttempts = 5;
  for (size_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
    std::vector<absl::string_view> address_order;
    GetOrderAddressesArePicked(kAddresses, &address_order);
    EXPECT_THAT(address_order, ::testing::ElementsAreArray(kAddresses));
  }
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
