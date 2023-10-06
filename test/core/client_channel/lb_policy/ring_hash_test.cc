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

#include <array>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/lb_policy/ring_hash/ring_hash.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/resolver/endpoint_addresses.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

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

// TODO(roth): I created this file when I fixed a bug and wrote only the
// test needed for that bug.  When we have time, we need a lot more
// tests here to cover all of the policy's functionality.

TEST_F(RingHashTest, Basic) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, MakeRingHashConfig()),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  auto* address0_attribute = MakeHashAttribute(kAddresses[0]);
  ExpectPickQueued(picker.get(), {address0_attribute});
  WaitForWorkSerializerToFlush();
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {address0_attribute});
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address =
      ExpectPickComplete(picker.get(), {MakeHashAttribute(kAddresses[0])});
  EXPECT_EQ(address, kAddresses[0]);
}

// FIXME: add multiple addresses per endpoint test
// FIXME: add test with the same address multiple times in the address list

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
