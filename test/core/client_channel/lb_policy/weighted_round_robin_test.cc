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

TEST_F(WeightedRoundRobinTest, DevolvesToRoundRobinWithoutWeights) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  EXPECT_EQ(
      ApplyUpdate(
          BuildUpdate(kAddresses, ConfigBuilder().Build()), lb_policy_.get()),
      absl::OkStatus());
  // Expect the initial CONNECTNG update with a picker that queues.
  ExpectConnectingUpdate();
  // RR should have created a subchannel for each address.
  for (size_t i = 0; i < kAddresses.size(); ++i) {
    auto* subchannel = FindSubchannel(kAddresses[i]);
    ASSERT_NE(subchannel, nullptr);
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
      ExpectRoundRobinPicks(picker.get(), {kAddresses[0]});
    } else {
      // When each subsequent subchannel becomes READY, we accept any number
      // of READY updates where the picker returns only the previously
      // connected subchannel(s) followed by a READY update where the picker
      // returns the previously connected subchannel(s) *and* the newly
      // connected subchannel.
      WaitForRoundRobinListChange(absl::MakeSpan(kAddresses).subspan(0, i),
                                  absl::MakeSpan(kAddresses).subspan(0, i + 1));
    }
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
