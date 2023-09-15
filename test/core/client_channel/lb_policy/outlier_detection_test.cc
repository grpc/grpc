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
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/support/json.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
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
      SetChildPolicy(Json::Object{{"round_robin", Json::FromObject({})}});
    }

    ConfigBuilder& SetInterval(Duration duration) {
      json_["interval"] = Json::FromString(duration.ToJsonString());
      return *this;
    }
    ConfigBuilder& SetBaseEjectionTime(Duration duration) {
      json_["baseEjectionTime"] = Json::FromString(duration.ToJsonString());
      return *this;
    }
    ConfigBuilder& SetMaxEjectionTime(Duration duration) {
      json_["maxEjectionTime"] = Json::FromString(duration.ToJsonString());
      return *this;
    }
    ConfigBuilder& SetMaxEjectionPercent(uint32_t value) {
      json_["maxEjectionPercent"] = Json::FromNumber(value);
      return *this;
    }
    ConfigBuilder& SetChildPolicy(Json::Object child_policy) {
      json_["childPolicy"] =
          Json::FromArray({Json::FromObject(std::move(child_policy))});
      return *this;
    }

    ConfigBuilder& SetSuccessRateStdevFactor(uint32_t value) {
      GetSuccessRate()["stdevFactor"] = Json::FromNumber(value);
      return *this;
    }
    ConfigBuilder& SetSuccessRateEnforcementPercentage(uint32_t value) {
      GetSuccessRate()["enforcementPercentage"] = Json::FromNumber(value);
      return *this;
    }
    ConfigBuilder& SetSuccessRateMinHosts(uint32_t value) {
      GetSuccessRate()["minimumHosts"] = Json::FromNumber(value);
      return *this;
    }
    ConfigBuilder& SetSuccessRateRequestVolume(uint32_t value) {
      GetSuccessRate()["requestVolume"] = Json::FromNumber(value);
      return *this;
    }

    ConfigBuilder& SetFailurePercentageThreshold(uint32_t value) {
      GetFailurePercentage()["threshold"] = Json::FromNumber(value);
      return *this;
    }
    ConfigBuilder& SetFailurePercentageEnforcementPercentage(uint32_t value) {
      GetFailurePercentage()["enforcementPercentage"] = Json::FromNumber(value);
      return *this;
    }
    ConfigBuilder& SetFailurePercentageMinimumHosts(uint32_t value) {
      GetFailurePercentage()["minimumHosts"] = Json::FromNumber(value);
      return *this;
    }
    ConfigBuilder& SetFailurePercentageRequestVolume(uint32_t value) {
      GetFailurePercentage()["requestVolume"] = Json::FromNumber(value);
      return *this;
    }

    RefCountedPtr<LoadBalancingPolicy::Config> Build() {
      Json::Object fields = json_;
      if (success_rate_.has_value()) {
        fields["successRateEjection"] = Json::FromObject(*success_rate_);
      }
      if (failure_percentage_.has_value()) {
        fields["failurePercentageEjection"] =
            Json::FromObject(*failure_percentage_);
      }
      Json config = Json::FromArray(
          {Json::FromObject({{"outlier_detection_experimental",
                              Json::FromObject(std::move(fields))}})});
      return MakeConfig(config);
    }

   private:
    Json::Object& GetSuccessRate() {
      if (!success_rate_.has_value()) success_rate_.emplace();
      return *success_rate_;
    }

    Json::Object& GetFailurePercentage() {
      if (!failure_percentage_.has_value()) failure_percentage_.emplace();
      return *failure_percentage_;
    }

    Json::Object json_;
    absl::optional<Json::Object> success_rate_;
    absl::optional<Json::Object> failure_percentage_;
  };

  OutlierDetectionTest()
      : LoadBalancingPolicyTest("outlier_detection_experimental") {}

  void SetUp() override {
    LoadBalancingPolicyTest::SetUp();
    SetExpectedTimerDuration(std::chrono::seconds(10));
  }

  absl::optional<std::string> DoPickWithFailedCall(
      LoadBalancingPolicy::SubchannelPicker* picker) {
    std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>
        subchannel_call_tracker;
    auto address = ExpectPickComplete(picker, {}, &subchannel_call_tracker);
    if (address.has_value()) {
      subchannel_call_tracker->Start();
      FakeMetadata metadata({});
      FakeBackendMetricAccessor backend_metric_accessor({});
      LoadBalancingPolicy::SubchannelCallTrackerInterface::FinishArgs args = {
          *address, absl::UnavailableError("uh oh"), &metadata,
          &backend_metric_accessor};
      subchannel_call_tracker->Finish(args);
    }
    return address;
  }
};

TEST_F(OutlierDetectionTest, Basic) {
  constexpr absl::string_view kAddressUri = "ipv4:127.0.0.1:443";
  // Send an update containing one address.
  absl::Status status = ApplyUpdate(
      BuildUpdate({kAddressUri}, ConfigBuilder().Build()), lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for the address.
  auto* subchannel = FindSubchannel(kAddressUri);
  ASSERT_NE(subchannel, nullptr);
  // When the LB policy receives the subchannel's initial connectivity
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
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddressUri);
  }
}

TEST_F(OutlierDetectionTest, FailurePercentage) {
  constexpr std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:440", "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"};
  // Send initial update.
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses, ConfigBuilder()
                                  .SetFailurePercentageThreshold(1)
                                  .SetFailurePercentageMinimumHosts(1)
                                  .SetFailurePercentageRequestVolume(1)
                                  .Build()),
      lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // Expect normal startup.
  auto picker = ExpectRoundRobinStartup(kAddresses);
  ASSERT_NE(picker, nullptr);
  gpr_log(GPR_INFO, "### RR startup complete");
  // Do a pick and report a failed call.
  auto address = DoPickWithFailedCall(picker.get());
  ASSERT_TRUE(address.has_value());
  gpr_log(GPR_INFO, "### failed RPC on %s", address->c_str());
  // Advance time and run the timer callback to trigger ejection.
  IncrementTimeBy(Duration::Seconds(10));
  gpr_log(GPR_INFO, "### ejection complete");
  if (!IsRoundRobinDelegateToPickFirstEnabled()) ExpectReresolutionRequest();
  // Expect a picker update.
  std::vector<absl::string_view> remaining_addresses;
  for (const auto& addr : kAddresses) {
    if (addr != *address) remaining_addresses.push_back(addr);
  }
  picker = WaitForRoundRobinListChange(kAddresses, remaining_addresses);
}

TEST_F(OutlierDetectionTest, DoesNotWorkWithPickFirst) {
  constexpr std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:440", "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"};
  // Send initial update.
  absl::Status status = ApplyUpdate(
      BuildUpdate(kAddresses,
                  ConfigBuilder()
                      .SetFailurePercentageThreshold(1)
                      .SetFailurePercentageMinimumHosts(1)
                      .SetFailurePercentageRequestVolume(1)
                      .SetChildPolicy({{"pick_first", Json::FromObject({})}})
                      .Build()),
      lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // LB policy should have created a subchannel for the first address.
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  // When the LB policy receives the subchannel's initial connectivity
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
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ExpectPickComplete(picker.get()), kAddresses[0]);
  }
  gpr_log(GPR_INFO, "### PF startup complete");
  // Now have an RPC to that subchannel fail.
  auto address = DoPickWithFailedCall(picker.get());
  ASSERT_TRUE(address.has_value());
  gpr_log(GPR_INFO, "### failed RPC on %s", address->c_str());
  // Advance time and run the timer callback to trigger ejection.
  IncrementTimeBy(Duration::Seconds(10));
  gpr_log(GPR_INFO, "### ejection timer pass complete");
  // Subchannel should not be ejected.
  ExpectQueueEmpty();
  // Subchannel should not see a reconnection request.
  EXPECT_FALSE(subchannel->ConnectionRequested());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
