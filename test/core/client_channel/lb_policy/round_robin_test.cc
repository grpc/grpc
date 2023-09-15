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

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class RoundRobinTest : public LoadBalancingPolicyTest {
 protected:
  RoundRobinTest() : LoadBalancingPolicyTest("round_robin") {}
};

TEST_F(RoundRobinTest, Basic) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, nullptr), lb_policy()),
            absl::OkStatus());
  ExpectRoundRobinStartup(kAddresses);
}

TEST_F(RoundRobinTest, AddressUpdates) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, nullptr), lb_policy()),
            absl::OkStatus());
  ExpectRoundRobinStartup(kAddresses);
  // Send update to remove address 2.
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(absl::MakeSpan(kAddresses).first(2), nullptr),
                  lb_policy()),
      absl::OkStatus());
  WaitForRoundRobinListChange(kAddresses, absl::MakeSpan(kAddresses).first(2));
  // Send update to remove address 0 and re-add address 2.
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(absl::MakeSpan(kAddresses).last(2), nullptr),
                  lb_policy()),
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
  return RUN_ALL_TESTS();
}
