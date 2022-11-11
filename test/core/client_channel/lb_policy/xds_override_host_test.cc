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
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/resolved_address.h"
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

  RefCountedPtr<LoadBalancingPolicy::Config> MakeXdsOverrideHostConfig(
      std::string child_policy = "pick_first") {
    Json::Object child_policy_config = {{child_policy, Json::Object()}};

    return MakeConfig(Json::Array{Json::Object{
        {"xds_override_host_experimental",
         Json::Object{{"childPolicy", Json::Array{{child_policy_config}}}}}}});
  }

  OrphanablePtr<LoadBalancingPolicy> policy_;
};

TEST_F(XdsOverrideHostTest, DelegatesToChild) {
  ASSERT_NE(policy_, nullptr);
  std::array<absl::string_view, 2> addresses = {"ipv4:127.0.0.1:441",
                                                "ipv4:127.0.0.1:442"};
  EXPECT_EQ(policy_->name(), "xds_override_host_experimental");
  // 1. We use pick_first as a child
  EXPECT_EQ(ApplyUpdate(BuildUpdate(addresses, MakeXdsOverrideHostConfig()),
                        policy_.get()),
            absl::OkStatus());
  ExpectConnectingUpdate();
  for (absl::string_view address : addresses) {
    auto subchannel = FindSubchannel(
        {address}, ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
    ASSERT_TRUE(subchannel);
    subchannel->ConnectionRequested();
    subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
    subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  }
  auto picker = WaitForConnected();
  // Pick first policy will always pick first!
  EXPECT_EQ(ExpectPickComplete(picker.get()), "ipv4:127.0.0.1:441");
  EXPECT_EQ(ExpectPickComplete(picker.get()), "ipv4:127.0.0.1:441");
  ExpectQueueEmpty();
  // 2. Now we switch to a round-robin
  EXPECT_EQ(ApplyUpdate(BuildUpdate(addresses,
                                    MakeXdsOverrideHostConfig("round_robin")),
                        policy_.get()),
            absl::OkStatus());
  for (absl::string_view address : addresses) {
    auto subchannel = FindSubchannel({address});
    ASSERT_TRUE(subchannel);
    subchannel->ConnectionRequested();
    subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
    subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  }
  WaitForConnected();
  WaitForConnected();
  picker = WaitForConnected();
  std::unordered_set<std::string> picked;
  for (size_t i = 0; i < addresses.size(); i++) {
    auto pick = ExpectPickComplete(picker.get());
    EXPECT_TRUE(pick.has_value());
    picked.insert(*pick);
  }
  EXPECT_THAT(picked, ::testing::UnorderedElementsAreArray(addresses));
}

TEST_F(XdsOverrideHostTest, NoConfigReportsError) {
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate({"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"}),
                  policy_.get()),
      absl::InvalidArgumentError("Missing policy config"));
}

TEST_F(XdsOverrideHostTest, ConfigRequiresChildPolicy) {
  auto result =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::Array{Json::Object{
              {"xds_override_host_experimental", Json::Object{}}}});
  EXPECT_EQ(result.status(),
            absl::InvalidArgumentError(
                "errors validating xds_override_host LB policy config: "
                "[field:childPolicy error:field not present]"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_core::SetEnv("GRPC_EXPERIMENTAL_XDS_ENABLE_HOST_OVERRIDE", "TRUE");
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
