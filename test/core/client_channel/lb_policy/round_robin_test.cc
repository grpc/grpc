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
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/resolver/server_address.h"
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
    // Expect the initial CONNECTNG update with a picker that queues.
    ExpectConnectingUpdate();
    // RR should have created a subchannel for each address.
    for (size_t i = 0; i < addresses.size(); ++i) {
      auto* subchannel = FindSubchannel(addresses[i]);
      ASSERT_NE(subchannel, nullptr) << "Address: " << addresses[i];
      // RR should ask each subchannel to connect.
      EXPECT_TRUE(subchannel->ConnectionRequested());
      // The subchannel will connect successfully.
      subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
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

TEST_F(RoundRobinTest, SingleAddress) {
  auto status = ApplyUpdate(BuildUpdate({"ipv4:127.0.0.1:441"}, nullptr),
                            lb_policy_.get());
  ASSERT_TRUE(status.ok()) << status;
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  auto subchannel = FindSubchannel("ipv4:127.0.0.1:441");
  ASSERT_NE(subchannel, nullptr);
  // LB policy should have requested a connection on this subchannel.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // Subchannel is ready
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  auto picker = WaitForConnected();
  // Picker should return the same subchannel repeatedly.
  ExpectRoundRobinPicks(picker.get(), {"ipv4:127.0.0.1:441"});
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  ExpectReresolutionRequest();
  ExpectConnectingUpdate();
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectConnectingUpdate();
  // There's a failure
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("a test"));
  auto expected_status = absl::UnavailableError(
      "connections to all backends failing; "
      "last error: UNAVAILABLE: a test");
  ExpectReresolutionRequest();
  WaitForConnectionFailedWithStatus(expected_status);
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  ExpectReresolutionRequest();
  WaitForConnectionFailedWithStatus(expected_status);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  WaitForConnectionFailedWithStatus(expected_status);
  // ... and a recovery!
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = WaitForConnected();
  ExpectRoundRobinPicks(picker.get(), {"ipv4:127.0.0.1:441"});
}

TEST_F(RoundRobinTest, ThreeAddresses) {
  std::array<absl::string_view, 3> addresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto status = ApplyUpdate(BuildUpdate(addresses, nullptr), lb_policy_.get());
  ASSERT_TRUE(status.ok()) << status;
  ExpectConnectingUpdate();
  std::vector<SubchannelState*> subchannels;
  for (const auto& address : addresses) {
    auto subchannel = FindSubchannel(address);
    ASSERT_NE(subchannel, nullptr) << address;
    subchannels.push_back(subchannel);
    EXPECT_TRUE(subchannel->ConnectionRequested());
    subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  }
  for (size_t i = 0; i < subchannels.size(); i++) {
    subchannels[i]->SetConnectivityState(GRPC_CHANNEL_READY);
    // For the first subchannel, we use WaitForConnected() to drain any queued
    // CONNECTING updates.  For each successive subchannel, we can read just
    // one READY update at a time.
    auto picker = i == 0 ? WaitForConnected() : ExpectState(GRPC_CHANNEL_READY);
    ASSERT_NE(picker, nullptr);
    ExpectRoundRobinPicks(
        picker.get(),
        absl::Span<const absl::string_view>(addresses).subspan(0, i + 1));
  }
  subchannels[1]->SetConnectivityState(GRPC_CHANNEL_IDLE);
  ExpectReresolutionRequest();
  auto picker = WaitForConnected();
  ExpectRoundRobinPicks(picker.get(), {addresses[0], addresses[2]});
  EXPECT_TRUE(subchannels[1]->ConnectionRequested());
  subchannels[1]->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = WaitForConnected();
  subchannels[1]->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                       absl::UnknownError("This is a test"));
  ExpectReresolutionRequest();
  picker = WaitForConnected();
  ExpectRoundRobinPicks(picker.get(), {addresses[0], addresses[2]});
}

TEST_F(RoundRobinTest, OneChannelReady) {
  auto subchannel = CreateSubchannel("ipv4:127.0.0.1:441");
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  auto status = ApplyUpdate(BuildUpdate(
                                {
                                    "ipv4:127.0.0.1:441",
                                    "ipv4:127.0.0.1:442",
                                    "ipv4:127.0.0.1:443",
                                },
                                nullptr),
                            lb_policy_.get());
  ASSERT_TRUE(status.ok()) << status;
  ExpectState(GRPC_CHANNEL_READY);
  ExpectState(GRPC_CHANNEL_READY);
  ExpectRoundRobinPicks(ExpectState(GRPC_CHANNEL_READY).get(),
                        {"ipv4:127.0.0.1:441"});
}

TEST_F(RoundRobinTest, AllTransientFailure) {
  auto status = ApplyUpdate(BuildUpdate(
                                {
                                    "ipv4:127.0.0.1:441",
                                    "ipv4:127.0.0.1:442",
                                    "ipv4:127.0.0.1:443",
                                },
                                nullptr),
                            lb_policy_.get());
  ASSERT_TRUE(status.ok()) << status;
  ExpectConnectingUpdate();
  for (auto address : {"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"}) {
    auto subchannel = FindSubchannel(address);
    ASSERT_NE(subchannel, nullptr);
    subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
    subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
    WaitForConnected();
    subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                     absl::UnknownError("error1"));
    ExpectReresolutionRequest();
    ExpectConnectingUpdate();
  }
  auto third_subchannel = FindSubchannel("ipv4:127.0.0.1:443");
  ASSERT_NE(third_subchannel, nullptr);
  third_subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  third_subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  WaitForConnected();
  third_subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                         absl::UnknownError("error2"));
  ExpectReresolutionRequest();
  WaitForConnectionFailedWithStatus(
      absl::UnavailableError("connections to all backends failing; "
                             "last error: UNKNOWN: error2"));
}

TEST_F(RoundRobinTest, EmptyAddressList) {
  LoadBalancingPolicy::UpdateArgs update_args;
  update_args.resolution_note = "This is a test";
  update_args.addresses.emplace();
  absl::Status status = ApplyUpdate(std::move(update_args), lb_policy_.get());
  EXPECT_EQ(status,
            absl::UnavailableError("empty address list: This is a test"));
  WaitForConnectionFailedWithStatus(
      absl::UnavailableError("empty address list: This is a test"));
  // Fixes memory leaks. Will debug at a later point.
  EXPECT_TRUE(ApplyUpdate(BuildUpdate({"ipv4:127.0.0.1:441"}, nullptr),
                          lb_policy_.get())
                  .ok());
  ExpectConnectingUpdate();
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
