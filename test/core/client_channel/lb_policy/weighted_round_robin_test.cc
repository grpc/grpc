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

  // Returns a map indicating the number of picks for each address.
  static std::map<absl::string_view, size_t> MakePickMap(
      absl::Span<const std::string> picks) {
    std::map<absl::string_view, size_t> actual;
    for (const auto& address : picks) {
      ++actual.emplace(address, 0).first->second;
    }
    return actual;
  }

  // Returns a human-readable string representing the number of picks
  // for each address.
  static std::string PickMapString(
      std::map<absl::string_view, size_t> pick_map) {
    return absl::StrJoin(pick_map, ",", absl::PairFormatter("="));
  }

  // Returns the number of picks we need to do to check the specified
  // expectations.
  static size_t NumPicksNeeded(
      const std::map<absl::string_view /*address*/, size_t /*num_picks*/>&
          expected) {
    size_t num_picks = 0;
    for (const auto& p : expected) {
      num_picks += p.second;
    }
    return num_picks;
  }

  // For each pick in picks, reports the backend metrics to the LB policy.
  static void ReportBackendMetrics(
      absl::Span<const std::string> picks,
      const std::vector<std::unique_ptr<
          LoadBalancingPolicy::SubchannelCallTrackerInterface>>&
          subchannel_call_trackers,
      const std::map<absl::string_view /*address*/,
                     std::pair<double /*qps*/, double /*cpu_utilization*/>>&
          backend_metrics) {
    for (size_t i = 0; i < picks.size(); ++i) {
      const auto& address = picks[i];
      auto& subchannel_call_tracker = subchannel_call_trackers[i];
      if (subchannel_call_tracker != nullptr) {
        subchannel_call_tracker->Start();
        BackendMetricData backend_metric_data;
        auto it = backend_metrics.find(address);
        if (it != backend_metrics.end()) {
          backend_metric_data.qps = it->second.first;
          backend_metric_data.cpu_utilization = it->second.second;
        }
        FakeMetadata metadata({});
        FakeBackendMetricAccessor backend_metric_accessor(
            backend_metric_data);
        LoadBalancingPolicy::SubchannelCallTrackerInterface::FinishArgs
            args = {
                address, absl::OkStatus(), &metadata,
                &backend_metric_accessor};
        subchannel_call_tracker->Finish(args);
      }
    }
  }

  void ExpectWeightedRoundRobinPicks(
      LoadBalancingPolicy::SubchannelPicker* picker,
      std::map<absl::string_view /*address*/,
               std::pair<double /*qps*/, double /*cpu_utilization*/>>
          backend_metrics,
      std::map<absl::string_view /*address*/, size_t /*num_picks*/> expected,
      SourceLocation location = SourceLocation()) {
    std::vector<std::unique_ptr<
        LoadBalancingPolicy::SubchannelCallTrackerInterface>>
        subchannel_call_trackers;
    auto picks = GetCompletePicks(picker, NumPicksNeeded(expected),
                                  &subchannel_call_trackers, location);
    ASSERT_TRUE(picks.has_value())
        << location.file() << ":" << location.line();
    gpr_log(GPR_INFO, "PICKS: %s", absl::StrJoin(*picks, " ").c_str());
    ReportBackendMetrics(*picks, subchannel_call_trackers, backend_metrics);
    auto actual = MakePickMap(*picks);
    EXPECT_EQ(expected, actual)
        << "Expected: " << PickMapString(expected) << "\nActual: "
        << PickMapString(actual) << "\nat " << location.file() << ":"
        << location.line();
  }

  bool WaitForWeightedRoundRobinPicks(
      RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>* picker,
      std::map<absl::string_view /*address*/,
               std::pair<double /*qps*/, double /*cpu_utilization*/>>
          backend_metrics,
      std::map<absl::string_view /*address*/, size_t /*num_picks*/> expected,
      Duration timeout = Duration::Seconds(20),
      SourceLocation location = SourceLocation()) {
    gpr_log(GPR_INFO, "==> WaitForWeightedRoundRobinPicks()");
    size_t num_picks = NumPicksNeeded(expected);
    Timestamp deadline = Timestamp::Now() + timeout;
    while (true) {
      gpr_log(GPR_INFO, "TOP OF LOOP: DOING PICKS");
      std::vector<std::unique_ptr<
          LoadBalancingPolicy::SubchannelCallTrackerInterface>>
          subchannel_call_trackers;
      auto picks = GetCompletePicks(picker->get(), num_picks,
                                    &subchannel_call_trackers, location);
      EXPECT_TRUE(picks.has_value())
          << location.file() << ":" << location.line();
      if (!picks.has_value()) return false;
      gpr_log(GPR_INFO, "PICKS: %s", absl::StrJoin(*picks, " ").c_str());
      // Report backend metrics to the LB policy.
      ReportBackendMetrics(*picks, subchannel_call_trackers, backend_metrics);
      // If the picks have the expected weights, we're done.
      auto actual = MakePickMap(*picks);
      if (expected == actual) return true;
      gpr_log(GPR_INFO, "Did not get expected picks:\nExpected: %s\nActual: %s",
              PickMapString(expected).c_str(), PickMapString(actual).c_str());
      // Make sure each address is one of the expected addresses.
      for (const auto& address : *picks) {
        bool found = expected.find(address) != expected.end();
        EXPECT_TRUE(found)
            << "unexpected pick address " << address << " at "
            << location.file() << ":" << location.line();
        if (!found) return false;
      }
      // If we're out of time, give up.
      Timestamp now = Timestamp::Now();
      EXPECT_LT(now, deadline) << location.file() << ":" << location.line();
      if (now >= deadline) return false;
      // Get a new picker if there is an update.
      if (!helper_->QueueEmpty()) {
        *picker = ExpectState(GRPC_CHANNEL_READY, absl::OkStatus(), location);
        EXPECT_NE(*picker, nullptr)
            << location.file() << ":" << location.line();
        if (*picker == nullptr) return false;
      }
    }
  }

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

TEST_F(WeightedRoundRobinTest, Basic) {
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
  }
  auto picker = WaitForConnected();
  // Address 0 gets weight 1, address 1 gets weight 3.
  // No utilization report from backend 2, so it gets the average weight 2.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], {100, 0.9}}, {kAddresses[1], {100, 0.3}}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 2}});
  // Now have backend 2 report utilization the same as backend 1, so its
  // weight will be the same.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], {100, 0.9}},
       {kAddresses[1], {100, 0.3}},
       {kAddresses[2], {100, 0.3}}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 2}});
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
