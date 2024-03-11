//
// Copyright 2023 gRPC authors.
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

#include "src/core/load_balancing/ring_hash/ring_hash.h"

#include <stdint.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/support/json.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/xxhash_inline.h"
#include "src/core/lib/json/json.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

// TODO(roth): I created this file when I fixed a bug and wrote only a
// very basic test and the test needed for that bug.  When we have time,
// we need a lot more tests here to cover all of the policy's functionality.

class RingHashTest : public LoadBalancingPolicyTest {
 protected:
  RingHashTest() : LoadBalancingPolicyTest("ring_hash_experimental") {}

  static RefCountedPtr<LoadBalancingPolicy::Config> MakeRingHashConfig(
      int min_ring_size = 0, int max_ring_size = 0) {
    Json::Object fields;
    if (min_ring_size > 0) {
      fields["minRingSize"] = Json::FromString(absl::StrCat(min_ring_size));
    }
    if (max_ring_size > 0) {
      fields["maxRingSize"] = Json::FromString(absl::StrCat(max_ring_size));
    }
    return MakeConfig(Json::FromArray({Json::FromObject(
        {{"ring_hash_experimental", Json::FromObject(fields)}})}));
  }

  RequestHashAttribute* MakeHashAttribute(absl::string_view address) {
    std::string hash_input =
        absl::StrCat(absl::StripPrefix(address, "ipv4:"), "_0");
    uint64_t hash = XXH64(hash_input.data(), hash_input.size(), 0);
    attribute_storage_.emplace_back(
        std::make_unique<RequestHashAttribute>(hash));
    return attribute_storage_.back().get();
  }

  std::vector<std::unique_ptr<RequestHashAttribute>> attribute_storage_;
};

TEST_F(RingHashTest, Basic) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses, MakeRingHashConfig()), lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  auto* address0_attribute = MakeHashAttribute(kAddresses[0]);
  ExpectPickQueued(picker.get(), {address0_attribute});
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {address0_attribute});
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {address0_attribute});
  EXPECT_EQ(address, kAddresses[0]);
}

TEST_F(RingHashTest, SameAddressListedMultipleTimes) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:441"};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses, MakeRingHashConfig()), lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  auto* address0_attribute = MakeHashAttribute(kAddresses[0]);
  ExpectPickQueued(picker.get(), {address0_attribute});
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {address0_attribute});
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {address0_attribute});
  EXPECT_EQ(address, kAddresses[0]);
}

TEST_F(RingHashTest, MultipleAddressesPerEndpoint) {
  constexpr std::array<absl::string_view, 2> kEndpoint1Addresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  constexpr std::array<absl::string_view, 2> kEndpoint2Addresses = {
      "ipv4:127.0.0.1:445", "ipv4:127.0.0.1:446"};
  const std::array<EndpointAddresses, 2> kEndpoints = {
      MakeEndpointAddresses(kEndpoint1Addresses),
      MakeEndpointAddresses(kEndpoint2Addresses)};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kEndpoints, MakeRingHashConfig()), lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  // Normal connection to first address of the first endpoint.
  auto* address0_attribute = MakeHashAttribute(kEndpoint1Addresses[0]);
  ExpectPickQueued(picker.get(), {address0_attribute});
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  auto* subchannel = FindSubchannel(kEndpoint1Addresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  auto* subchannel2 = FindSubchannel(kEndpoint1Addresses[1]);
  ASSERT_NE(subchannel2, nullptr);
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {address0_attribute});
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {address0_attribute});
  EXPECT_EQ(address, kEndpoint1Addresses[0]);
  // Now that connection fails.
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  ExpectReresolutionRequest();
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  EXPECT_FALSE(subchannel->ConnectionRequested());
  EXPECT_FALSE(subchannel2->ConnectionRequested());
  // The LB policy will try to reconnect when it gets another pick.
  ExpectPickQueued(picker.get(), {address0_attribute});
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {address0_attribute});
  // The connection attempt fails.
  subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                   absl::UnavailableError("ugh"));
  // The PF child policy will try to connect to the second address for the
  // endpoint.
  EXPECT_TRUE(subchannel2->ConnectionRequested());
  subchannel2->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {address0_attribute});
  subchannel2->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  address = ExpectPickComplete(picker.get(), {address0_attribute});
  EXPECT_EQ(address, kEndpoint1Addresses[1]);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
