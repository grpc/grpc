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

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class RoundRobinTest : public LoadBalancingPolicyTest {
 protected:
  RoundRobinTest() : lb_policy_(MakeLbPolicy("round_robin")) {}

  void ExpectStartup(absl::Span<const absl::string_view> addresses) {
    EXPECT_EQ(ApplyUpdate(BuildUpdate(addresses, nullptr), lb_policy_.get()),
              absl::OkStatus());
    // RR should have created a subchannel for each address.
    for (size_t i = 0; i < addresses.size(); ++i) {
      auto* subchannel = FindSubchannel(addresses[i]);
      ASSERT_NE(subchannel, nullptr) << "Address: " << addresses[i];
      // RR should ask each subchannel to connect.
      EXPECT_TRUE(subchannel->ConnectionRequested());
      // The subchannel will connect successfully.
      subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
      // Expect the initial CONNECTNG update with a picker that queues.
      if (i == 0) ExpectConnectingUpdate();
      subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
      // As each subchannel becomes READY, we should get a new picker that
      // includes the behavior.  Note that there may be any number of
      // duplicate updates for the previous state in the queue before the
      // update that we actually want to see.
      if (i == 0) {
        // When the first subchannel becomes READY, accept any number of
        // CONNECTING updates with a picker that queues followed by a READY
        // update with a picker that repeatedly returns only the first address.
        auto picker = WaitForConnected();
        ExpectRoundRobinPicks(picker.get(), {addresses[0]});
      } else {
        // When each subsequent subchannel becomes READY, we accept any number
        // of READY updates where the picker returns only the previously
        // connected subchannel(s) followed by a READY update where the picker
        // returns the previously connected subchannel(s) *and* the newly
        // connected subchannel.
        WaitForRoundRobinListChange(
            absl::MakeSpan(addresses).subspan(0, i),
            absl::MakeSpan(addresses).subspan(0, i + 1));
      }
    }
  }

  OrphanablePtr<LoadBalancingPolicy> lb_policy_;
};

TEST_F(RoundRobinTest, Basic) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  ExpectStartup(kAddresses);
}

TEST_F(RoundRobinTest, AddressUpdates) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  ExpectStartup(kAddresses);
  // Send update to remove address 2.
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(absl::MakeSpan(kAddresses).first(2), nullptr),
                  lb_policy_.get()),
      absl::OkStatus());
  WaitForRoundRobinListChange(kAddresses, absl::MakeSpan(kAddresses).first(2));
  // Send update to remove address 0 and re-add address 2.
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(absl::MakeSpan(kAddresses).last(2), nullptr),
                  lb_policy_.get()),
      absl::OkStatus());
  WaitForRoundRobinListChange(absl::MakeSpan(kAddresses).first(2),
                              absl::MakeSpan(kAddresses).last(2));
}

TEST_F(RoundRobinTest, MultipleAddressesPerEndpoint) {
  if (!IsRoundRobinDelegateToPickFirstEnabled()) return;
  constexpr std::array<absl::string_view, 2> kEndpoint1Addresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  constexpr std::array<absl::string_view, 2> kEndpoint2Addresses = {
      "ipv4:127.0.0.1:445", "ipv4:127.0.0.1:446"};
  const std::array<EndpointAddresses, 2> kEndpoints = {
      MakeEndpointAddresses(kEndpoint1Addresses),
      MakeEndpointAddresses(kEndpoint2Addresses)};
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kEndpoints, nullptr), lb_policy_.get()),
            absl::OkStatus());
  // RR should have created a subchannel for each address.
  auto* subchannel1_0 = FindSubchannel(kEndpoint1Addresses[0]);
  ASSERT_NE(subchannel1_0, nullptr) << "Address: " << kEndpoint1Addresses[0];
  auto* subchannel1_1 = FindSubchannel(kEndpoint1Addresses[1]);
  ASSERT_NE(subchannel1_1, nullptr) << "Address: " << kEndpoint1Addresses[1];
  auto* subchannel2_0 = FindSubchannel(kEndpoint2Addresses[0]);
  ASSERT_NE(subchannel2_0, nullptr) << "Address: " << kEndpoint2Addresses[0];
  auto* subchannel2_1 = FindSubchannel(kEndpoint2Addresses[1]);
  ASSERT_NE(subchannel2_1, nullptr) << "Address: " << kEndpoint2Addresses[1];
  // PF for each endpoint should try to connect to the first subchannel.
  EXPECT_TRUE(subchannel1_0->ConnectionRequested());
  EXPECT_FALSE(subchannel1_1->ConnectionRequested());
  EXPECT_TRUE(subchannel2_0->ConnectionRequested());
  EXPECT_FALSE(subchannel2_1->ConnectionRequested());
  // In the first endpoint, the first subchannel reports CONNECTING.
  // This causes RR to report CONNECTING.
  subchannel1_0->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectConnectingUpdate();
  // In the second endpoint, the first subchannel reports CONNECTING.
  subchannel2_0->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // In the first endpoint, the first subchannel fails to connect.
  // This causes PF to start a connection attempt on the second subchannel.
  subchannel1_0->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                      absl::UnavailableError("ugh"));
  EXPECT_TRUE(subchannel1_1->ConnectionRequested());
  subchannel1_1->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // In the second endpoint, the first subchannel becomes connected.
  // This causes RR to report READY with all RPCs going to a single address.
  subchannel2_0->SetConnectivityState(GRPC_CHANNEL_READY);
  auto picker = WaitForConnected();
  ExpectRoundRobinPicks(picker.get(), {kEndpoint2Addresses[0]});
  // In the first endpoint, the second subchannel becomes connected.
  // This causes RR to add it to the rotation.
  subchannel1_1->SetConnectivityState(GRPC_CHANNEL_READY);
  WaitForRoundRobinListChange({kEndpoint2Addresses[0]},
                              {kEndpoint2Addresses[0], kEndpoint1Addresses[1]});
  // No more connection attempts triggered.
  EXPECT_FALSE(subchannel1_0->ConnectionRequested());
  EXPECT_FALSE(subchannel1_1->ConnectionRequested());
  EXPECT_FALSE(subchannel2_0->ConnectionRequested());
  EXPECT_FALSE(subchannel2_1->ConnectionRequested());
}

// TODO(roth): Add test cases:
// - empty address list
// - subchannels failing connection attempts

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
