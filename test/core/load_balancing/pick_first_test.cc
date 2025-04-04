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

#include "src/core/load_balancing/pick_first/pick_first.h"

#include <grpc/grpc.h>
#include <grpc/support/json.h>
#include <stddef.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/json/json.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/work_serializer.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class PickFirstTest : public LoadBalancingPolicyTest {
 protected:
  explicit PickFirstTest(ChannelArgs channel_args = ChannelArgs())
      : LoadBalancingPolicyTest("pick_first", channel_args) {}

  void SetUp() override {
    LoadBalancingPolicyTest::SetUp();
    SetExpectedTimerDuration(std::chrono::milliseconds(250));
  }

  static RefCountedPtr<LoadBalancingPolicy::Config> MakePickFirstConfig(
      std::optional<bool> shuffle_address_list = std::nullopt) {
    return MakeConfig(Json::FromArray({Json::FromObject(
        {{"pick_first",
          shuffle_address_list.has_value()
              ? Json::FromObject({{"shuffleAddressList",
                                   Json::FromBool(*shuffle_address_list)}})
              : Json::FromObject({})}})}));
  }

  // Gets order the addresses are being picked. Return type is void so
  // assertions can be used.
  void GetOrderAddressesArePicked(
      absl::Span<const absl::string_view> addresses,
      std::vector<absl::string_view>* out_address_order) {
    out_address_order->clear();
    ExitIdle();
    // Construct a map of subchannel to address.
    // We will remove entries as each subchannel starts to connect.
    std::map<SubchannelState*, absl::string_view> subchannels;
    for (auto address : addresses) {
      auto* subchannel = FindSubchannel(address);
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
        // We would normally call ExpectStateAndQueueingPicker() here instead of
        // just ExpectState(). However, calling the picker would also trigger
        // exiting IDLE, which we don't want here, because if the test is going
        // to send an address list update and call GetOrderAddressesArePicked()
        // again, we don't want to trigger a connection attempt on any
        // subchannel until after that next address list update is processed.
        ExpectState(GRPC_CHANNEL_IDLE);
      }
      // Remove the subchannel from the map.
      subchannels.erase(subchannel);
    }
  }
};

TEST_F(PickFirstTest, FirstAddressWorks) {
  // Send an update containing two addresses.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kAddresses[1]);
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
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel2, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
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
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
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

TEST_F(PickFirstTest, FlattensEndpointAddressesList) {
  // Send an update containing two endpoints, the first one with two addresses.
  constexpr std::array<absl::string_view, 2> kEndpoint1Addresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  constexpr std::array<absl::string_view, 1> kEndpoint2Addresses = {
      "ipv4:127.0.0.1:445"};
  const std::array<EndpointAddresses, 2> kEndpoints = {
      MakeEndpointAddresses(kEndpoint1Addresses),
      MakeEndpointAddresses(kEndpoint2Addresses)};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kEndpoints, MakePickFirstConfig(false)), lb_policy_.get());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for all 3 addresses.
  auto* subchannel = FindSubchannel(kEndpoint1Addresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kEndpoint1Addresses[1]);
  ASSERT_NE(subchannel2, nullptr);
  auto* subchannel3 = FindSubchannel(kEndpoint2Addresses[0]);
  ASSERT_NE(subchannel3, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // The other subchannels should not be connecting.
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  EXPECT_FALSE(subchannel3->ConnectionRequested());
  // The first subchannel's connection attempt fails.
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("failed to connect"));
  // The LB policy will start a connection attempt on the second subchannel.
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  EXPECT_FALSE(subchannel3->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // The connection attempt fails.
  subchannel2->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("failed to connect"));
  // The LB policy will start a connection attempt on the third subchannel.
  EXPECT_TRUE(subchannel3->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
  subchannel3->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // This one succeeds.
  subchannel3->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report CONNECTING some number of times (doesn't
  // matter how many) and then report READY.
  auto picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kEndpoint2Addresses[0]);
  }
}

TEST_F(PickFirstTest, FirstTwoAddressesInTransientFailureAtStart) {
  // Send an update containing three addresses.
  // The first two addresses are already in state TRANSIENT_FAILURE when the
  // LB policy gets the update.
  constexpr std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445"};
  auto* subchannel = CreateSubchannel(kAddresses[0]);
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("failed to connect"),
                                   /*validate_state_transition=*/false);
  auto* subchannel2 = CreateSubchannel(kAddresses[1]);
  subchannel2->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                    absl::UnavailableError("failed to connect"),
                                    /*validate_state_transition=*/false);
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for all addresses.
  auto* subchannel3 = FindSubchannel(kAddresses[2]);
  ASSERT_NE(subchannel3, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (TRANSIENT_FAILURE), it will move on to the second
  // subchannel.  The second subchannel is also in state TRANSIENT_FAILURE,
  // so the LB policy will move on to the third subchannel.  That
  // subchannel is in state IDLE, so the LB policy will request a connection
  // attempt on it.
  EXPECT_TRUE(subchannel3->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
  subchannel3->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // The connection attempt succeeds.
  subchannel3->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report CONNECTING some number of times (doesn't
  // matter how many) and then report READY.
  auto picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[2]);
  }
}

TEST_F(PickFirstTest, AllAddressesInTransientFailureAtStart) {
  // Send an update containing two addresses, both in TRANSIENT_FAILURE
  // when the LB policy gets the update.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  auto* subchannel = CreateSubchannel(kAddresses[0]);
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("failed to connect"),
                                   /*validate_state_transition=*/false);
  auto* subchannel2 = CreateSubchannel(kAddresses[1]);
  subchannel2->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                    absl::UnavailableError("failed to connect"),
                                    /*validate_state_transition=*/false);
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // The LB policy should request re-resolution.
  ExpectReresolutionRequest();
  // The LB policy should report TRANSIENT_FAILURE.
  WaitForConnectionFailed([&](const absl::Status& status) {
    EXPECT_EQ(status, absl::UnavailableError(
                          "failed to connect to all addresses; "
                          "last error: UNAVAILABLE: failed to connect"));
  });
  // No connections should have been requested.
  EXPECT_FALSE(subchannel->ConnectionRequested());
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // Now have the first subchannel report IDLE.
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  // The policy will ask it to connect.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // The connection attempt succeeds.
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report READY.
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[0]);
  }
}

TEST_F(PickFirstTest, StaysInTransientFailureAfterAddressListUpdate) {
  // Send an update containing two addresses, both in TRANSIENT_FAILURE
  // when the LB policy gets the update.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  auto* subchannel = CreateSubchannel(kAddresses[0]);
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("failed to connect"),
                                   /*validate_state_transition=*/false);
  auto* subchannel2 = CreateSubchannel(kAddresses[1]);
  subchannel2->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                    absl::UnavailableError("failed to connect"),
                                    /*validate_state_transition=*/false);
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // The LB policy should request re-resolution.
  ExpectReresolutionRequest();
  // The LB policy should report TRANSIENT_FAILURE.
  WaitForConnectionFailed([&](const absl::Status& status) {
    EXPECT_EQ(status, absl::UnavailableError(
                          "failed to connect to all addresses; "
                          "last error: UNAVAILABLE: failed to connect"));
  });
  // No connections should have been requested.
  EXPECT_FALSE(subchannel->ConnectionRequested());
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // Now send an address list update.  This contains the first address
  // from the previous update plus a new address, whose subchannel will
  // be in state IDLE.
  constexpr std::array<absl::string_view, 2> kAddresses2 = {
      kAddresses[0], "ipv4:127.0.0.1:445"};
  status = ApplyUpdate(BuildUpdate(kAddresses2, MakePickFirstConfig(false)),
                       lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // The LB policy should have created a subchannel for the new address.
  auto* subchannel3 = FindSubchannel(kAddresses2[1]);
  ASSERT_NE(subchannel3, nullptr);
  // The policy will ask it to connect.
  EXPECT_TRUE(subchannel3->ConnectionRequested());
  // This causes it to start to connect, so it reports CONNECTING.
  subchannel3->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // The connection attempt succeeds.
  subchannel3->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report READY.
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses2[1]);
  }
}

// This tests a real-world bug in which PF ignored a resolver update if
// it had just created the subchannels but had not yet seen their
// initial connectivity state notification.
TEST_F(PickFirstTest, ResolverUpdateBeforeLeavingIdle) {
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  constexpr std::array<absl::string_view, 2> kNewAddresses = {
      "ipv4:127.0.0.1:445", "ipv4:127.0.0.1:446"};
  // Send initial update containing two addresses.
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kAddresses[1]);
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
  // Now the connection is closed, so we go IDLE.
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  ExpectReresolutionRequest();
  ExpectState(GRPC_CHANNEL_IDLE);
  // Now we tell the LB policy to exit idle.  This causes it to create a
  // new subchannel list from the original update.  However, before it
  // can get the initial connectivity state notifications for those
  // subchannels (i.e., before it can transition from IDLE to CONNECTING),
  // we send a new update.
  absl::Notification notification;
  work_serializer_->Run([&]() {
    // Inject second update into WorkSerializer queue before we
    // exit idle, so that the second update gets run before the initial
    // subchannel connectivity state notifications from the first update
    // are delivered.
    work_serializer_->Run([&]() {
      // Second update.
      absl::Status status = lb_policy()->UpdateLocked(
          BuildUpdate(kNewAddresses, MakePickFirstConfig(false)));
      EXPECT_TRUE(status.ok()) << status;
      // Trigger notification once all connectivity state
      // notifications have been delivered.
      work_serializer_->Run([&]() { notification.Notify(); });
    });
    // Exit idle.
    lb_policy()->ExitIdleLocked();
  });
  while (!notification.HasBeenNotified()) {
    fuzzing_ee_->Tick();
  }
  // The LB policy should have created subchannels for the new addresses.
  auto* subchannel3 = FindSubchannel(kNewAddresses[0]);
  ASSERT_NE(subchannel3, nullptr);
  auto* subchannel4 = FindSubchannel(kNewAddresses[1]);
  ASSERT_NE(subchannel4, nullptr);
  // The LB policy will request a connection on the first new subchannel,
  // none of the others.
  EXPECT_TRUE(subchannel3->ConnectionRequested());
  EXPECT_FALSE(subchannel->ConnectionRequested());
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  EXPECT_FALSE(subchannel4->ConnectionRequested());
  // The subchannel starts a connection attempt.
  subchannel3->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // The LB policy should now report CONNECTING.
  ExpectConnectingUpdate();
  // The connection attempt succeeds.
  subchannel3->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report CONNECTING some number of times (doesn't
  // matter how many) and then report READY.
  picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kNewAddresses[0]);
  }
}

TEST_F(PickFirstTest, HappyEyeballs) {
  // Send an update containing three addresses.
  constexpr std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel2, nullptr);
  auto* subchannel3 = FindSubchannel(kAddresses[2]);
  ASSERT_NE(subchannel3, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // The second subchannel should not be connecting.
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the second subchannel.
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // The second subchannel fails before the timer fires.
  subchannel2->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("failed to connect"));
  // This causes the LB policy to start connecting to the third subchannel.
  EXPECT_TRUE(subchannel3->ConnectionRequested());
  subchannel3->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // Incrementing the time here has no effect, because the LB policy
  // does not use a timer for the last subchannel in the list.
  // So if there are any queued updates at this point, they will be
  // CONNECTING state.
  IncrementTimeBy(Duration::Milliseconds(250));
  DrainConnectingUpdates();
  // The first subchannel becomes connected.
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

TEST_F(PickFirstTest, HappyEyeballsCompletesWithoutSuccess) {
  // Send an update containing three addresses.
  constexpr std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel2, nullptr);
  auto* subchannel3 = FindSubchannel(kAddresses[2]);
  ASSERT_NE(subchannel3, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // The second subchannel should not be connecting.
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the second subchannel.
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // The second subchannel fails before the timer fires.
  subchannel2->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("failed to connect"));
  // This causes the LB policy to start connecting to the third subchannel.
  EXPECT_TRUE(subchannel3->ConnectionRequested());
  subchannel3->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // Incrementing the time here has no effect, because the LB policy
  // does not use a timer for the last subchannel in the list.
  // So if there are any queued updates at this point, they will be
  // CONNECTING state.
  IncrementTimeBy(Duration::Milliseconds(250));
  DrainConnectingUpdates();
  // Set subchannel 2 back to IDLE, so it's already in that state when
  // Happy Eyeballs fails.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_IDLE);
  // Third subchannel fails to connect.
  subchannel3->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("failed to connect"));
  ExpectQueueEmpty();
  // Eventually, the first subchannel fails as well.
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("failed to connect"));
  // The LB policy should request re-resolution.
  ExpectReresolutionRequest();
  // The LB policy should report TRANSIENT_FAILURE.
  WaitForConnectionFailed([&](const absl::Status& status) {
    EXPECT_EQ(status, absl::UnavailableError(
                          "failed to connect to all addresses; "
                          "last error: UNAVAILABLE: failed to connect"));
  });
  // We are now done with the Happy Eyeballs pass, and we move into a
  // mode where we try to connect to all subchannels in parallel.
  // Subchannel 2 was already in state IDLE, so the LB policy will
  // immediately trigger a connection request on it.  It will not do so
  // for subchannels 1 or 3, which are in TRANSIENT_FAILURE.
  EXPECT_FALSE(subchannel->ConnectionRequested());
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  EXPECT_FALSE(subchannel3->ConnectionRequested());
  // Subchannel 2 reports CONNECTING.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // Now subchannel 1 reports IDLE.  This should trigger another
  // connection attempt.
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // Now subchannel 1 reports TRANSIENT_FAILURE.  This is the first failure
  // since we finished Happy Eyeballs.
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("failed to connect"));
  EXPECT_FALSE(subchannel->ConnectionRequested());
  // Now subchannel 3 reports IDLE.  This should trigger another
  // connection attempt.
  subchannel3->SetConnectivityState(GRPC_CHANNEL_IDLE);
  EXPECT_TRUE(subchannel3->ConnectionRequested());
  subchannel3->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // Subchannel 2 reports TF.  This is the second failure since we
  // finished Happy Eyeballs.
  subchannel2->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("failed to connect"));
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // Finally, subchannel 3 reports TF.  This is the third failure since
  // we finished Happy Eyeballs, so the LB policy will request
  // re-resolution and report TF again.
  subchannel3->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("failed to connect"));
  EXPECT_FALSE(subchannel3->ConnectionRequested());
  ExpectReresolutionRequest();
  ExpectTransientFailureUpdate(
      absl::UnavailableError("failed to connect to all addresses; "
                             "last error: UNAVAILABLE: failed to connect"));
  // Now the second subchannel goes IDLE.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_IDLE);
  // The LB policy asks it to connect.
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // This time, the connection attempt succeeds.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report READY.
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[1]);
  }
}

TEST_F(PickFirstTest,
       HappyEyeballsLastSubchannelFailsWhileAnotherIsStillPending) {
  // Send an update containing three addresses.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel2, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // The second subchannel should not be connecting.
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the second subchannel.
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // The second subchannel fails.
  subchannel2->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("failed to connect"));
  // The LB policy should not yet report TRANSIENT_FAILURE, because the
  // first subchannel is still CONNECTING.
  DrainConnectingUpdates();
  // Set subchannel 2 back to IDLE, so it's already in that state when
  // Happy Eyeballs fails.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_IDLE);
  // Now the first subchannel fails.
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("failed to connect"));
  // The LB policy should request re-resolution.
  ExpectReresolutionRequest();
  // The LB policy should report TRANSIENT_FAILURE.
  WaitForConnectionFailed([&](const absl::Status& status) {
    EXPECT_EQ(status, absl::UnavailableError(
                          "failed to connect to all addresses; "
                          "last error: UNAVAILABLE: failed to connect"));
  });
  // We are now done with the Happy Eyeballs pass, and we move into a
  // mode where we try to connect to all subchannels in parallel.
  // Subchannel 2 was already in state IDLE, so the LB policy will
  // immediately trigger a connection request on it.  It will not do so
  // for subchannel 1, which is still in TRANSIENT_FAILURE.
  EXPECT_FALSE(subchannel->ConnectionRequested());
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  // Subchannel 2 reports CONNECTING.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // Subchannel 2 reports READY.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report READY.
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[1]);
  }
}

TEST_F(PickFirstTest, HappyEyeballsAddressInterleaving) {
  // Send an update containing four IPv4 addresses followed by two
  // IPv6 addresses.
  constexpr std::array<absl::string_view, 6> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445",
      "ipv4:127.0.0.1:446", "ipv6:[::1]:444",     "ipv6:[::1]:445"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for all addresses.
  auto* subchannel_ipv4_1 = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel_ipv4_1, nullptr);
  auto* subchannel_ipv4_2 = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel_ipv4_2, nullptr);
  auto* subchannel_ipv4_3 = FindSubchannel(kAddresses[2]);
  ASSERT_NE(subchannel_ipv4_3, nullptr);
  auto* subchannel_ipv4_4 = FindSubchannel(kAddresses[3]);
  ASSERT_NE(subchannel_ipv4_4, nullptr);
  auto* subchannel_ipv6_1 = FindSubchannel(kAddresses[4]);
  ASSERT_NE(subchannel_ipv6_1, nullptr);
  auto* subchannel_ipv6_2 = FindSubchannel(kAddresses[5]);
  ASSERT_NE(subchannel_ipv6_2, nullptr);
  // When the LB policy receives the subchannels' initial connectivity
  // state notifications (all IDLE), it will request a connection on the
  // first IPv4 subchannel.
  EXPECT_TRUE(subchannel_ipv4_1->ConnectionRequested());
  subchannel_ipv4_1->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // No other subchannels should be connecting.
  EXPECT_FALSE(subchannel_ipv4_2->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_3->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_4->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv6_1->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv6_2->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the first IPv6
  // subchannel.
  EXPECT_TRUE(subchannel_ipv6_1->ConnectionRequested());
  subchannel_ipv6_1->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // No other subchannels should be connecting.
  EXPECT_FALSE(subchannel_ipv4_2->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_3->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_4->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv6_2->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the second IPv4
  // subchannel.
  EXPECT_TRUE(subchannel_ipv4_2->ConnectionRequested());
  subchannel_ipv4_2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // No other subchannels should be connecting.
  EXPECT_FALSE(subchannel_ipv4_3->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_4->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv6_2->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the second IPv6
  // subchannel.
  EXPECT_TRUE(subchannel_ipv6_2->ConnectionRequested());
  subchannel_ipv6_2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // No other subchannels should be connecting.
  EXPECT_FALSE(subchannel_ipv4_3->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_4->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the third IPv4
  // subchannel.
  EXPECT_TRUE(subchannel_ipv4_3->ConnectionRequested());
  subchannel_ipv4_3->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // No other subchannels should be connecting.
  EXPECT_FALSE(subchannel_ipv4_4->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the fourth IPv4
  // subchannel.
  EXPECT_TRUE(subchannel_ipv4_4->ConnectionRequested());
  subchannel_ipv4_4->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
}

TEST_F(PickFirstTest,
       HappyEyeballsAddressInterleavingSecondFamilyHasMoreAddresses) {
  // Send an update containing two IPv6 addresses followed by four IPv4
  // addresses.
  constexpr std::array<absl::string_view, 6> kAddresses = {
      "ipv6:[::1]:444",     "ipv6:[::1]:445",     "ipv4:127.0.0.1:443",
      "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445", "ipv4:127.0.0.1:446"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for all addresses.
  auto* subchannel_ipv6_1 = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel_ipv6_1, nullptr);
  auto* subchannel_ipv6_2 = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel_ipv6_2, nullptr);
  auto* subchannel_ipv4_1 = FindSubchannel(kAddresses[2]);
  ASSERT_NE(subchannel_ipv4_1, nullptr);
  auto* subchannel_ipv4_2 = FindSubchannel(kAddresses[3]);
  ASSERT_NE(subchannel_ipv4_2, nullptr);
  auto* subchannel_ipv4_3 = FindSubchannel(kAddresses[4]);
  ASSERT_NE(subchannel_ipv4_3, nullptr);
  auto* subchannel_ipv4_4 = FindSubchannel(kAddresses[5]);
  ASSERT_NE(subchannel_ipv4_4, nullptr);
  // When the LB policy receives the subchannels' initial connectivity
  // state notifications (all IDLE), it will request a connection on the
  // first IPv6 subchannel.
  EXPECT_TRUE(subchannel_ipv6_1->ConnectionRequested());
  subchannel_ipv6_1->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // No other subchannels should be connecting.
  EXPECT_FALSE(subchannel_ipv6_2->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_1->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_2->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_3->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_4->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the first IPv4
  // subchannel.
  EXPECT_TRUE(subchannel_ipv4_1->ConnectionRequested());
  subchannel_ipv4_1->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // No other subchannels should be connecting.
  EXPECT_FALSE(subchannel_ipv6_2->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_2->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_3->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_4->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the second IPv6
  // subchannel.
  EXPECT_TRUE(subchannel_ipv6_2->ConnectionRequested());
  subchannel_ipv6_2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // No other subchannels should be connecting.
  EXPECT_FALSE(subchannel_ipv4_2->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_3->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_4->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the second IPv4
  // subchannel.
  EXPECT_TRUE(subchannel_ipv4_2->ConnectionRequested());
  subchannel_ipv4_2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // No other subchannels should be connecting.
  EXPECT_FALSE(subchannel_ipv4_3->ConnectionRequested());
  EXPECT_FALSE(subchannel_ipv4_4->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the third IPv4
  // subchannel.
  EXPECT_TRUE(subchannel_ipv4_3->ConnectionRequested());
  subchannel_ipv4_3->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // No other subchannels should be connecting.
  EXPECT_FALSE(subchannel_ipv4_4->ConnectionRequested());
  // The timer fires before the connection attempt completes.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This causes the LB policy to start connecting to the fourth IPv4
  // subchannel.
  EXPECT_TRUE(subchannel_ipv4_4->ConnectionRequested());
  subchannel_ipv4_4->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
}

TEST_F(PickFirstTest, FirstAddressGoesIdleBeforeSecondOneFails) {
  // Send an update containing two addresses.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel2, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
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
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // Before the second subchannel's attempt completes, the first
  // subchannel reports IDLE.
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  // Now the connection attempt on the second subchannel fails.
  subchannel2->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("failed to connect"));
  // The LB policy should request re-resolution.
  ExpectReresolutionRequest();
  // The LB policy will report TRANSIENT_FAILURE.
  WaitForConnectionFailed([&](const absl::Status& status) {
    EXPECT_EQ(status, absl::UnavailableError(
                          "failed to connect to all addresses; "
                          "last error: UNAVAILABLE: failed to connect"));
  });
  // It will then start connecting to the first address again.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This time, the connection attempt succeeds.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report READY.
  auto picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[0]);
  }
}

TEST_F(PickFirstTest, GoesIdleWhenConnectionFailsThenCanReconnect) {
  // Send an update containing two addresses.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kAddresses[1]);
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
  // Note that the picker will have enqueued the ExitIdle() call in the
  // WorkSerializer, so the first flush will execute that call.  But
  // executing that call will result in enqueueing subchannel
  // connectivity state notifications, so we need to flush again to make
  // sure all of that work is done before we continue.
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
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

TEST_F(PickFirstTest, AddressUpdateRemovedSelectedAddress) {
  // Send an update containing two addresses.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kAddresses[1]);
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
  // Now send an address update that removes the selected address.
  status = ApplyUpdate(BuildUpdate({kAddresses[1]}, MakePickFirstConfig(false)),
                       lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // We should see a re-resolution request.
  ExpectReresolutionRequest();
  // LB policy reports IDLE with a queueing picker.
  ExpectStateAndQueuingPicker(GRPC_CHANNEL_IDLE);
  // By checking the picker, we told the LB policy to trigger a new
  // connection attempt, so it should start one on the subchannel for
  // the remaining address.
  // Note that the picker will have enqueued the ExitIdle() call in the
  // WorkSerializer, so the first flush will execute that call.  But
  // executing that call will result in enqueueing subchannel
  // connectivity state notifications, so we need to flush again to make
  // sure all of that work is done before we continue.
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  // The subchannel starts connecting.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // Subchannel succeeds in connecting.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_READY);
  // LB policy reports READY.
  picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[1]);
  }
}

TEST_F(PickFirstTest, AddressUpdateRetainsSelectedAddress) {
  // Send an update containing two addresses.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kAddresses[1]);
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
  // Now send an address update that reverses the order of the addresses.
  status = ApplyUpdate(
      BuildUpdate({kAddresses[1], kAddresses[0]}, MakePickFirstConfig(false)),
      lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // The address we were already connected to is second in the new list,
  // but since it's already READY, we should stick with it.
  picker = ExpectState(GRPC_CHANNEL_READY);
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[0]);
  }
  EXPECT_FALSE(subchannel2->ConnectionRequested());
}

// This exercizes a bug seen in the wild that caused a crash.  For
// details, see https://github.com/grpc/grpc/pull/38144.
TEST_F(PickFirstTest, SubchannelNotificationAfterShutdown) {
  // Send an update containing two addresses.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for each address.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel2, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // The following things happen in order:
  // 1. We enqueue a READY notification for the subchannel in the
  //    WorkSerializer, but do not yet execute it.
  // 2. We enqueue the Happy Eyeballs timer callback in the
  //    WorkSerializer, but do not yet execute it.
  // 3. We shut down the LB policy.  This will try to cancel the Happy
  //    Eyeballs timer, but since the timer has already fired,
  //    cancellation will fail.
  // 4. Now we drain the WorkSerializer queue.  The LB policy sees the READY
  //    notification.  Before the bug fix, this caused us to select the
  //    subchannel instead of ignoring the notification.  With the bug fix,
  //    this update should never actually be delivered to the LB policy,
  //    since it will have already shut down the subchannel.
  // 5. The LB policy now sees the Happy Eyeballs timer callback.  This
  //    is a no-op, because the LB policy has already been shut down,
  //    but it will release the last ref to the subchannel list.
  //
  // To get the ordering right here, we need to do steps 2 and 3
  // inside the WorkSerializer, after the READY notification has been
  // enqueued but before we drain the WorkSerializer queue.
  subchannel->SetConnectivityState(
      GRPC_CHANNEL_READY, /*status=*/absl::OkStatus(),
      /*validate_state_transition=*/true,
      /*run_before_flush=*/[&]() {
        // Step 2: Trigger the timer.  The callback will be enqueued in
        // the WorkSerializer, but we don't drain it yet.
        IncrementTimeBy(Duration::Milliseconds(250),
                        /*flush_work_serializer=*/false);
        // Step 3: Shut down the LB policy.
        lb_policy_.reset();
      });
  // Now the subchannel reports IDLE.  Before the bug fix, this
  // triggered a crash.
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
}

// This exercizes a bug seen in the wild that caused us to silently stop
// triggering connection attempts at the end of the Happy Eyeballs pass.
TEST_F(PickFirstTest,
       SubchannelInitiallyReportsTransientFailureButIsIdleForHappyEyeballs) {
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  // Pre-initialize the second subchannel to report TRANSIENT_FAILURE as
  // its initial state.
  auto* subchannel2 = CreateSubchannel(kAddresses[1]);
  subchannel2->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                    absl::UnavailableError("failed to connect"),
                                    /*validate_state_transition=*/false);
  // Send an update containing two addresses.
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for the first address.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // Second subchannel finishes backoff.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_IDLE);
  // No connection attempt triggered on the second subchannel yet.
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // Now the Happy Eyeballs timer fires.
  IncrementTimeBy(Duration::Milliseconds(250));
  // This triggers a connection attempt on the second subchannel.
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports CONNECTING.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // Second subchannel fails immediately.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                    absl::UnavailableError("ugh"));
  // Second subchannel finishes backoff.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_IDLE);
  // Now the first subchannel fails.
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("ugh2"));
  // This should trigger an immediate re-attempt on the second subchannel.
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  // Subchannel should report CONNECTING.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // The LB policy should request re-resolution.
  ExpectReresolutionRequest();
  // The LB policy will report TRANSIENT_FAILURE.
  WaitForConnectionFailed([&](const absl::Status& status) {
    EXPECT_EQ(status,
              absl::UnavailableError("failed to connect to all addresses; "
                                     "last error: UNAVAILABLE: ugh2"));
  });
}

TEST_F(PickFirstTest, WithShuffle) {
  constexpr std::array<absl::string_view, 6> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445",
      "ipv4:127.0.0.1:446", "ipv4:127.0.0.1:447", "ipv4:127.0.0.1:448"};
  // 6 addresses have 6! = 720 permutations or roughly 0.14% chance that
  // the shuffle returns same permutation. We allow for several tries to
  // prevent flake test.
  constexpr size_t kMaxTries = 10;
  std::vector<absl::string_view> addresses_after_update;
  bool shuffled = false;
  for (size_t i = 0; i < kMaxTries; ++i) {
    absl::Status status = ApplyUpdate(
        BuildUpdate(kAddresses, MakePickFirstConfig(true)), lb_policy());
    EXPECT_TRUE(status.ok()) << status;
    GetOrderAddressesArePicked(kAddresses, &addresses_after_update);
    if (absl::MakeConstSpan(addresses_after_update) !=
        absl::MakeConstSpan(kAddresses)) {
      shuffled = true;
      break;
    }
  }
  ASSERT_TRUE(shuffled);
  // Address order should be stable between updates
  std::vector<absl::string_view> addresses_on_another_try;
  GetOrderAddressesArePicked(kAddresses, &addresses_on_another_try);
  EXPECT_EQ(addresses_on_another_try, addresses_after_update);
}

TEST_F(PickFirstTest, ShufflingDisabled) {
  constexpr std::array<absl::string_view, 6> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445",
      "ipv4:127.0.0.1:446", "ipv4:127.0.0.1:447", "ipv4:127.0.0.1:448"};
  constexpr static size_t kMaxAttempts = 5;
  for (size_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
    absl::Status status = ApplyUpdate(
        BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
    EXPECT_TRUE(status.ok()) << status;
    std::vector<absl::string_view> address_order;
    GetOrderAddressesArePicked(kAddresses, &address_order);
    EXPECT_THAT(address_order, ::testing::ElementsAreArray(kAddresses));
  }
}

TEST_F(PickFirstTest, MetricDefinitionDisconnections) {
  const auto* descriptor =
      GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
          "grpc.lb.pick_first.disconnections");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->value_type,
            GlobalInstrumentsRegistry::ValueType::kUInt64);
  EXPECT_EQ(descriptor->instrument_type,
            GlobalInstrumentsRegistry::InstrumentType::kCounter);
  EXPECT_EQ(descriptor->enable_by_default, false);
  EXPECT_EQ(descriptor->name, "grpc.lb.pick_first.disconnections");
  EXPECT_EQ(descriptor->unit, "{disconnection}");
  EXPECT_THAT(descriptor->label_keys, ::testing::ElementsAre("grpc.target"));
  EXPECT_THAT(descriptor->optional_label_keys, ::testing::ElementsAre());
}

TEST_F(PickFirstTest, MetricDefinitionConnectionAttemptsSucceeded) {
  const auto* descriptor =
      GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
          "grpc.lb.pick_first.connection_attempts_succeeded");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->value_type,
            GlobalInstrumentsRegistry::ValueType::kUInt64);
  EXPECT_EQ(descriptor->instrument_type,
            GlobalInstrumentsRegistry::InstrumentType::kCounter);
  EXPECT_EQ(descriptor->enable_by_default, false);
  EXPECT_EQ(descriptor->name,
            "grpc.lb.pick_first.connection_attempts_succeeded");
  EXPECT_EQ(descriptor->unit, "{attempt}");
  EXPECT_THAT(descriptor->label_keys, ::testing::ElementsAre("grpc.target"));
  EXPECT_THAT(descriptor->optional_label_keys, ::testing::ElementsAre());
}

TEST_F(PickFirstTest, MetricDefinitionConnectionAttemptsFailed) {
  const auto* descriptor =
      GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
          "grpc.lb.pick_first.connection_attempts_failed");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->value_type,
            GlobalInstrumentsRegistry::ValueType::kUInt64);
  EXPECT_EQ(descriptor->instrument_type,
            GlobalInstrumentsRegistry::InstrumentType::kCounter);
  EXPECT_EQ(descriptor->enable_by_default, false);
  EXPECT_EQ(descriptor->name, "grpc.lb.pick_first.connection_attempts_failed");
  EXPECT_EQ(descriptor->unit, "{attempt}");
  EXPECT_THAT(descriptor->label_keys, ::testing::ElementsAre("grpc.target"));
  EXPECT_THAT(descriptor->optional_label_keys, ::testing::ElementsAre());
}

TEST_F(PickFirstTest, MetricValues) {
  const auto kDisconnections =
      GlobalInstrumentsRegistryTestPeer::FindUInt64CounterHandleByName(
          "grpc.lb.pick_first.disconnections")
          .value();
  const auto kConnectionAttemptsSucceeded =
      GlobalInstrumentsRegistryTestPeer::FindUInt64CounterHandleByName(
          "grpc.lb.pick_first.connection_attempts_succeeded")
          .value();
  const auto kConnectionAttemptsFailed =
      GlobalInstrumentsRegistryTestPeer::FindUInt64CounterHandleByName(
          "grpc.lb.pick_first.connection_attempts_failed")
          .value();
  const absl::string_view kLabelValues[] = {target_};
  auto stats_plugin = std::make_shared<FakeStatsPlugin>(
      nullptr, /*use_disabled_by_default_metrics=*/true);
  stats_plugin_group_.AddStatsPlugin(stats_plugin, nullptr);
  // Send an update containing two addresses.
  constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, MakePickFirstConfig(false)), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for both addresses.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  auto* subchannel2 = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel2, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // The second subchannel should not be connecting.
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // The first subchannel's connection attempt fails.
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("failed to connect"));
  EXPECT_THAT(stats_plugin->GetUInt64CounterValue(kConnectionAttemptsFailed,
                                                  kLabelValues, {}),
              ::testing::Optional(1));
  // The LB policy will start a connection attempt on the second subchannel.
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports
  // CONNECTING.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // The connection attempt succeeds.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_READY);
  EXPECT_THAT(stats_plugin->GetUInt64CounterValue(kConnectionAttemptsSucceeded,
                                                  kLabelValues, {}),
              ::testing::Optional(1));
  // The LB policy will report CONNECTING some number of times (doesn't
  // matter how many) and then report READY.
  auto picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[1]);
  }
  // Now the subchannel becomes disconnected.
  subchannel2->SetConnectivityState(GRPC_CHANNEL_IDLE);
  ExpectReresolutionRequest();
  ExpectState(GRPC_CHANNEL_IDLE);
  EXPECT_THAT(
      stats_plugin->GetUInt64CounterValue(kDisconnections, kLabelValues, {}),
      ::testing::Optional(1));
}

class PickFirstHealthCheckingEnabledTest : public PickFirstTest {
 protected:
  PickFirstHealthCheckingEnabledTest()
      : PickFirstTest(ChannelArgs().Set(
            GRPC_ARG_INTERNAL_PICK_FIRST_ENABLE_HEALTH_CHECKING, true)) {}
};

TEST_F(PickFirstHealthCheckingEnabledTest, UpdateWithReadyChannel) {
  constexpr absl::string_view kAddress = "ipv4:127.0.0.1:443";
  LoadBalancingPolicy::UpdateArgs update =
      BuildUpdate({kAddress}, MakePickFirstConfig());
  absl::Status status = ApplyUpdate(update, lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for the address.
  auto* subchannel = FindSubchannel(kAddress);
  ASSERT_NE(subchannel, nullptr);
  // When the LB policy receives the first subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // When the subchannel becomes connected, it reports READY.
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report CONNECTING some number of times (doesn't
  // matter how many) and then report READY.
  auto picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
  EXPECT_EQ(ExpectPickComplete(picker.get()), kAddress);
  // Reapply the same update we did before. The the underlying
  // subchannel will immediately become ready.
  status =
      ApplyUpdate(BuildUpdate({kAddress}, MakePickFirstConfig()), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  picker = ExpectState(GRPC_CHANNEL_READY);
  EXPECT_EQ(ExpectPickComplete(picker.get()), kAddress);
  // At this point, NumWatchers() should account for our
  // subchannel connectivity watcher and our health watcher.
  EXPECT_EQ(subchannel->NumWatchers(), 2);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
