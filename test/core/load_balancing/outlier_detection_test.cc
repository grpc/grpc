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

#include <grpc/grpc.h>
#include <grpc/support/json.h>
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "src/core/load_balancing/backend_metric_data.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/json/json.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/test_config.h"

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
    std::optional<Json::Object> success_rate_;
    std::optional<Json::Object> failure_percentage_;
  };

  OutlierDetectionTest()
      : LoadBalancingPolicyTest("outlier_detection_experimental") {}

  void SetUp() override {
    LoadBalancingPolicyTest::SetUp();
    SetExpectedTimerDuration(std::chrono::seconds(10));
  }

  std::optional<std::string> DoPickWithFailedCall(
      LoadBalancingPolicy::SubchannelPicker* picker) {
    std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>
        subchannel_call_tracker;
    auto address = ExpectPickComplete(picker, {}, {}, &subchannel_call_tracker);
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
                                  .SetMaxEjectionTime(Duration::Seconds(1))
                                  .SetBaseEjectionTime(Duration::Seconds(1))
                                  .Build()),
      lb_policy());
  EXPECT_TRUE(status.ok()) << status;
  // Expect normal startup.
  auto picker = ExpectRoundRobinStartup(kAddresses);
  ASSERT_NE(picker, nullptr);
  LOG(INFO) << "### RR startup complete";
  // Do a pick and report a failed call.
  auto address = DoPickWithFailedCall(picker.get());
  ASSERT_TRUE(address.has_value());
  LOG(INFO) << "### failed RPC on " << *address;
  // Advance time and run the timer callback to trigger ejection.
  IncrementTimeBy(Duration::Seconds(10));
  LOG(INFO) << "### ejection complete";
  // Expect a picker update.
  std::vector<absl::string_view> remaining_addresses;
  for (const auto& addr : kAddresses) {
    if (addr != *address) remaining_addresses.push_back(addr);
  }
  WaitForRoundRobinListChange(kAddresses, remaining_addresses);
  // Advance time and run the timer callback to trigger un-ejection.
  IncrementTimeBy(Duration::Seconds(10));
  LOG(INFO) << "### un-ejection complete";
  // Expect a picker update.
  WaitForRoundRobinListChange(remaining_addresses, kAddresses);
}

TEST_F(OutlierDetectionTest, MultipleAddressesPerEndpoint) {
  // Can't use timer duration expectation here, because the Happy
  // Eyeballs timer inside pick_first will use a different duration than
  // the timer in outlier_detection.
  SetExpectedTimerDuration(std::nullopt);
  constexpr std::array<absl::string_view, 2> kEndpoint1Addresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  constexpr std::array<absl::string_view, 2> kEndpoint2Addresses = {
      "ipv4:127.0.0.1:445", "ipv4:127.0.0.1:446"};
  constexpr std::array<absl::string_view, 2> kEndpoint3Addresses = {
      "ipv4:127.0.0.1:447", "ipv4:127.0.0.1:448"};
  const std::array<EndpointAddresses, 3> kEndpoints = {
      MakeEndpointAddresses(kEndpoint1Addresses),
      MakeEndpointAddresses(kEndpoint2Addresses),
      MakeEndpointAddresses(kEndpoint3Addresses)};
  // Send initial update.
  absl::Status status = ApplyUpdate(
      BuildUpdate(kEndpoints, ConfigBuilder()
                                  .SetFailurePercentageThreshold(1)
                                  .SetFailurePercentageMinimumHosts(1)
                                  .SetFailurePercentageRequestVolume(1)
                                  .SetMaxEjectionTime(Duration::Seconds(1))
                                  .SetBaseEjectionTime(Duration::Seconds(1))
                                  .Build()),
      lb_policy_.get());
  EXPECT_TRUE(status.ok()) << status;
  // Expect normal startup.
  auto picker = ExpectRoundRobinStartup(kEndpoints);
  ASSERT_NE(picker, nullptr);
  LOG(INFO) << "### RR startup complete";
  // Do a pick and report a failed call.
  auto address = DoPickWithFailedCall(picker.get());
  ASSERT_TRUE(address.has_value());
  LOG(INFO) << "### failed RPC on " << *address;
  // Based on the address that the failed call went to, we determine
  // which addresses to use in the subsequent steps.
  absl::Span<const absl::string_view> ejected_endpoint_addresses;
  absl::Span<const absl::string_view> sentinel_endpoint_addresses;
  absl::string_view unmodified_endpoint_address;
  std::vector<absl::string_view> final_addresses;
  if (kEndpoint1Addresses[0] == *address) {
    ejected_endpoint_addresses = kEndpoint1Addresses;
    sentinel_endpoint_addresses = kEndpoint2Addresses;
    unmodified_endpoint_address = kEndpoint3Addresses[0];
    final_addresses = {kEndpoint1Addresses[1], kEndpoint2Addresses[1],
                       kEndpoint3Addresses[0]};
  } else if (kEndpoint2Addresses[0] == *address) {
    ejected_endpoint_addresses = kEndpoint2Addresses;
    sentinel_endpoint_addresses = kEndpoint1Addresses;
    unmodified_endpoint_address = kEndpoint3Addresses[0];
    final_addresses = {kEndpoint1Addresses[1], kEndpoint2Addresses[1],
                       kEndpoint3Addresses[0]};
  } else {
    ejected_endpoint_addresses = kEndpoint3Addresses;
    sentinel_endpoint_addresses = kEndpoint1Addresses;
    unmodified_endpoint_address = kEndpoint2Addresses[0];
    final_addresses = {kEndpoint1Addresses[1], kEndpoint2Addresses[0],
                       kEndpoint3Addresses[1]};
  }
  // Advance time and run the timer callback to trigger ejection.
  IncrementTimeBy(Duration::Seconds(10));
  LOG(INFO) << "### ejection complete";
  // Expect a picker that removes the ejected address.
  WaitForRoundRobinListChange(
      {kEndpoint1Addresses[0], kEndpoint2Addresses[0], kEndpoint3Addresses[0]},
      {sentinel_endpoint_addresses[0], unmodified_endpoint_address});
  LOG(INFO) << "### ejected endpoint removed";
  // Cause the connection to the ejected endpoint to fail, and then
  // have it reconnect to a different address.  The endpoint is still
  // ejected, so the new address should not be used.
  ExpectEndpointAddressChange(ejected_endpoint_addresses, 0, 1, nullptr);
  // Need to drain the picker updates before calling
  // ExpectEndpointAddressChange() again, since that will expect a
  // re-resolution request in the queue.
  DrainRoundRobinPickerUpdates(
      {sentinel_endpoint_addresses[0], unmodified_endpoint_address});
  LOG(INFO) << "### done changing address of ejected endpoint";
  // Do the same thing for the sentinel endpoint, so that we
  // know that the LB policy has seen the address change for the ejected
  // endpoint.
  ExpectEndpointAddressChange(sentinel_endpoint_addresses, 0, 1, [&]() {
    WaitForRoundRobinListChange(
        {sentinel_endpoint_addresses[0], unmodified_endpoint_address},
        {unmodified_endpoint_address});
  });
  WaitForRoundRobinListChange(
      {unmodified_endpoint_address},
      {sentinel_endpoint_addresses[1], unmodified_endpoint_address});
  LOG(INFO) << "### done changing address of ejected endpoint";
  // Advance time and run the timer callback to trigger un-ejection.
  IncrementTimeBy(Duration::Seconds(10));
  LOG(INFO) << "### un-ejection complete";
  // The ejected endpoint should come back using the new address.
  WaitForRoundRobinListChange(
      {sentinel_endpoint_addresses[1], unmodified_endpoint_address},
      final_addresses);
}

TEST_F(OutlierDetectionTest, EjectionStateResetsWhenEndpointAddressesChange) {
  // Can't use timer duration expectation here, because the Happy
  // Eyeballs timer inside pick_first will use a different duration than
  // the timer in outlier_detection.
  SetExpectedTimerDuration(std::nullopt);
  constexpr std::array<absl::string_view, 2> kEndpoint1Addresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  constexpr std::array<absl::string_view, 2> kEndpoint2Addresses = {
      "ipv4:127.0.0.1:445", "ipv4:127.0.0.1:446"};
  constexpr std::array<absl::string_view, 2> kEndpoint3Addresses = {
      "ipv4:127.0.0.1:447", "ipv4:127.0.0.1:448"};
  const std::array<EndpointAddresses, 3> kEndpoints = {
      MakeEndpointAddresses(kEndpoint1Addresses),
      MakeEndpointAddresses(kEndpoint2Addresses),
      MakeEndpointAddresses(kEndpoint3Addresses)};
  auto kConfig = ConfigBuilder()
                     .SetFailurePercentageThreshold(1)
                     .SetFailurePercentageMinimumHosts(1)
                     .SetFailurePercentageRequestVolume(1)
                     .SetMaxEjectionTime(Duration::Seconds(1))
                     .SetBaseEjectionTime(Duration::Seconds(1))
                     .Build();
  // Send initial update.
  absl::Status status =
      ApplyUpdate(BuildUpdate(kEndpoints, kConfig), lb_policy_.get());
  EXPECT_TRUE(status.ok()) << status;
  // Expect normal startup.
  auto picker = ExpectRoundRobinStartup(kEndpoints);
  ASSERT_NE(picker, nullptr);
  LOG(INFO) << "### RR startup complete";
  // Do a pick and report a failed call.
  auto ejected_address = DoPickWithFailedCall(picker.get());
  ASSERT_TRUE(ejected_address.has_value());
  LOG(INFO) << "### failed RPC on " << *ejected_address;
  // Based on the address that the failed call went to, we determine
  // which addresses to use in the subsequent steps.
  std::vector<absl::string_view> expected_round_robin_while_ejected;
  std::vector<EndpointAddresses> new_endpoints;
  if (kEndpoint1Addresses[0] == *ejected_address) {
    expected_round_robin_while_ejected = {kEndpoint2Addresses[0],
                                          kEndpoint3Addresses[0]};
    new_endpoints = {MakeEndpointAddresses({kEndpoint1Addresses[0]}),
                     MakeEndpointAddresses(kEndpoint2Addresses),
                     MakeEndpointAddresses(kEndpoint3Addresses)};
  } else if (kEndpoint2Addresses[0] == *ejected_address) {
    expected_round_robin_while_ejected = {kEndpoint1Addresses[0],
                                          kEndpoint3Addresses[0]};
    new_endpoints = {MakeEndpointAddresses(kEndpoint1Addresses),
                     MakeEndpointAddresses({kEndpoint2Addresses[0]}),
                     MakeEndpointAddresses(kEndpoint3Addresses)};
  } else {
    expected_round_robin_while_ejected = {kEndpoint1Addresses[0],
                                          kEndpoint2Addresses[0]};
    new_endpoints = {MakeEndpointAddresses(kEndpoint1Addresses),
                     MakeEndpointAddresses(kEndpoint2Addresses),
                     MakeEndpointAddresses({kEndpoint3Addresses[0]})};
  }
  // Advance time and run the timer callback to trigger ejection.
  IncrementTimeBy(Duration::Seconds(10));
  LOG(INFO) << "### ejection complete";
  // Expect a picker that removes the ejected address.
  WaitForRoundRobinListChange(
      {kEndpoint1Addresses[0], kEndpoint2Addresses[0], kEndpoint3Addresses[0]},
      expected_round_robin_while_ejected);
  LOG(INFO) << "### ejected endpoint removed";
  // Send an update that removes the other address from the ejected endpoint.
  status = ApplyUpdate(BuildUpdate(new_endpoints, kConfig), lb_policy_.get());
  EXPECT_TRUE(status.ok()) << status;
  // This should cause the address to start getting used again, since
  // it's now associated with a different endpoint.
  WaitForRoundRobinListChange(
      expected_round_robin_while_ejected,
      {kEndpoint1Addresses[0], kEndpoint2Addresses[0], kEndpoint3Addresses[0]});
}

TEST_F(OutlierDetectionTest, DoesNotWorkWithPickFirst) {
  // Can't use timer duration expectation here, because the Happy
  // Eyeballs timer inside pick_first will use a different duration than
  // the timer in outlier_detection.
  SetExpectedTimerDuration(std::nullopt);
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
  LOG(INFO) << "### PF startup complete";
  // Now have an RPC to that subchannel fail.
  auto address = DoPickWithFailedCall(picker.get());
  ASSERT_TRUE(address.has_value());
  LOG(INFO) << "### failed RPC on " << *address;
  // Advance time and run the timer callback to trigger ejection.
  IncrementTimeBy(Duration::Seconds(10));
  LOG(INFO) << "### ejection timer pass complete";
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
