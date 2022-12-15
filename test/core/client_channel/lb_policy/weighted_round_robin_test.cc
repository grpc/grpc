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
#include <stdint.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class WeightedRoundRobinTest : public LoadBalancingPolicyTest {
 protected:
  class ConfigBuilder {
   public:
    ConfigBuilder& SetEnableOobLoadReport(bool value) {
      json_["enableOobLoadReport"] = value;
      return *this;
    }
    ConfigBuilder& SetBlackoutPeriod(Duration duration) {
      json_["blackoutPeriod"] = duration.ToJsonString();
      return *this;
    }
    ConfigBuilder& SetWeightUpdatePeriod(Duration duration) {
      json_["weightUpdatePeriod"] = duration.ToJsonString();
      return *this;
    }

    RefCountedPtr<LoadBalancingPolicy::Config> Build() {
      Json config = Json::Array{Json::Object{{"weighted_round_robin", json_}}};
      return MakeConfig(config);
    }

   private:
    Json::Object json_;
  };

  WeightedRoundRobinTest() : lb_policy_(MakeLbPolicy("weighted_round_robin")) {}

  OrphanablePtr<LoadBalancingPolicy> lb_policy_;
};

TEST_F(WeightedRoundRobinTest, Basic) {
  constexpr absl::string_view kAddress = "ipv4:127.0.0.1:443";
  // Send an update containing one address.
  absl::Status status = ApplyUpdate(
      BuildUpdate({kAddress}, ConfigBuilder().Build()), lb_policy_.get());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have reported CONNECTING state.
  ExpectConnectingUpdate();
  // LB policy should have created a subchannel for the address.
  auto* subchannel = FindSubchannel(kAddress);
  ASSERT_NE(subchannel, nullptr);
  // When the LB policy receives the subchannel's initial connectivity
  // state notification (IDLE), it will request a connection.
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // This causes the subchannel to start to connect, so it reports CONNECTING.
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  // When the subchannel becomes connected, it reports READY.
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  // The LB policy will report CONNECTING some number of times (doesn't
  // matter how many) and then report READY.
  auto picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddress);
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
