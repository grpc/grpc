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
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/resolver/server_address.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class OutlierDetectionTest : public LoadBalancingPolicyTest {
 protected:
  class ConfigBuilder {
   public:
    ConfigBuilder() {
      SetChildPolicy(Json::Object{{"round_robin", Json::Object()}});
    }

    ConfigBuilder& SetInterval(Duration duration) {
      json_["interval"] = duration.ToJsonString();
      return *this;
    }
    ConfigBuilder& SetBaseEjectionTime(Duration duration) {
      json_["baseEjectionTime"] = duration.ToJsonString();
      return *this;
    }
    ConfigBuilder& SetMaxEjectionTime(Duration duration) {
      json_["maxEjectionTime"] = duration.ToJsonString();
      return *this;
    }
    ConfigBuilder& SetMaxEjectionPercent(uint32_t value) {
      json_["maxEjectionPercent"] = value;
      return *this;
    }
    ConfigBuilder& SetChildPolicy(Json::Object child_policy) {
      json_["childPolicy"] = Json::Array{std::move(child_policy)};
      return *this;
    }

    ConfigBuilder& SetSuccessRateStdevFactor(uint32_t value) {
      GetSuccessRate()["stdevFactor"] = value;
      return *this;
    }
    ConfigBuilder& SetSuccessRateEnforcementPercentage(uint32_t value) {
      GetSuccessRate()["enforcementPercentage"] = value;
      return *this;
    }
    ConfigBuilder& SetSuccessRateMinHosts(uint32_t value) {
      GetSuccessRate()["minimumHosts"] = value;
      return *this;
    }
    ConfigBuilder& SetSuccessRateRequestVolume(uint32_t value) {
      GetSuccessRate()["requestVolume"] = value;
      return *this;
    }

    ConfigBuilder& SetFailurePercentageThreshold(uint32_t value) {
      GetFailurePercentage()["threshold"] = value;
      return *this;
    }
    ConfigBuilder& SetFailurePercentageEnforcementPercentage(uint32_t value) {
      GetFailurePercentage()["enforcementPercentage"] = value;
      return *this;
    }
    ConfigBuilder& SetFailurePercentageMinimumHosts(uint32_t value) {
      GetFailurePercentage()["minimumHosts"] = value;
      return *this;
    }
    ConfigBuilder& SetFailurePercentageRequestVolume(uint32_t value) {
      GetFailurePercentage()["requestVolume"] = value;
      return *this;
    }

    RefCountedPtr<LoadBalancingPolicy::Config> Build() {
      Json config =
          Json::Array{Json::Object{{"outlier_detection_experimental", json_}}};
      return MakeConfig(config);
    }

   private:
    Json::Object& GetSuccessRate() {
      auto it = json_.emplace("successRateEjection", Json::Object()).first;
      return *it->second.mutable_object();
    }

    Json::Object& GetFailurePercentage() {
      auto it =
          json_.emplace("failurePercentageEjection", Json::Object()).first;
      return *it->second.mutable_object();
    }

    Json::Object json_;
  };

  OutlierDetectionTest()
      : lb_policy_(MakeLbPolicy("outlier_detection_experimental")) {}

  OrphanablePtr<LoadBalancingPolicy> lb_policy_;
};

TEST_F(OutlierDetectionTest, Basic) {
  constexpr absl::string_view kAddressUri = "ipv4:127.0.0.1:443";
  const grpc_resolved_address address = MakeAddress(kAddressUri);
  // Send an update containing one address.
  LoadBalancingPolicy::UpdateArgs update_args;
  update_args.config = ConfigBuilder().Build();
  update_args.addresses.emplace();
  update_args.addresses->emplace_back(address, ChannelArgs());
  absl::Status status = ApplyUpdate(std::move(update_args), lb_policy_.get());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have reported CONNECTING state.
  auto picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get());
  // LB policy should have created a subchannel for the address.
  SubchannelKey key(address, ChannelArgs());
  auto it = subchannel_pool_.find(key);
  ASSERT_NE(it, subchannel_pool_.end());
  auto& subchannel_state = it->second;
  // LB policy should have requested a connection on this subchannel.
  EXPECT_TRUE(subchannel_state.ConnectionRequested());
  // Tell subchannel to report CONNECTING.
  subchannel_state.SetConnectivityState(GRPC_CHANNEL_CONNECTING,
                                        absl::OkStatus());
  // LB policy should again report CONNECTING.
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get());
  // Tell subchannel to report READY.
  subchannel_state.SetConnectivityState(GRPC_CHANNEL_READY, absl::OkStatus());
  // LB policy should eventually report READY.
  picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    ExpectPickComplete(picker.get(), kAddressUri);
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
