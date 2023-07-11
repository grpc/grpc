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

#include <inttypes.h>
#include <stddef.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <ratio>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/json.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_writer.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

BackendMetricData MakeBackendMetricData(double app_utilization, double qps,
                                        double eps,
                                        double cpu_utilization = 0) {
  BackendMetricData b;
  b.cpu_utilization = cpu_utilization;
  b.application_utilization = app_utilization;
  b.qps = qps;
  b.eps = eps;
  return b;
}

class WeightedRoundRobinTest : public TimeAwareLoadBalancingPolicyTest {
 protected:
  class ConfigBuilder {
   public:
    ConfigBuilder() {
      // Set blackout period to 1s to make tests fast and deterministic.
      SetBlackoutPeriod(Duration::Seconds(1));
    }

    ConfigBuilder& SetEnableOobLoadReport(bool value) {
      json_["enableOobLoadReport"] = Json::FromBool(value);
      return *this;
    }
    ConfigBuilder& SetOobReportingPeriod(Duration duration) {
      json_["oobReportingPeriod"] = Json::FromString(duration.ToJsonString());
      return *this;
    }
    ConfigBuilder& SetBlackoutPeriod(Duration duration) {
      json_["blackoutPeriod"] = Json::FromString(duration.ToJsonString());
      return *this;
    }
    ConfigBuilder& SetWeightUpdatePeriod(Duration duration) {
      json_["weightUpdatePeriod"] = Json::FromString(duration.ToJsonString());
      return *this;
    }
    ConfigBuilder& SetWeightExpirationPeriod(Duration duration) {
      json_["weightExpirationPeriod"] =
          Json::FromString(duration.ToJsonString());
      return *this;
    }
    ConfigBuilder& SetErrorUtilizationPenalty(float value) {
      json_["errorUtilizationPenalty"] = Json::FromNumber(value);
      return *this;
    }

    RefCountedPtr<LoadBalancingPolicy::Config> Build() {
      Json config = Json::FromArray({Json::FromObject(
          {{"weighted_round_robin", Json::FromObject(json_)}})});
      gpr_log(GPR_INFO, "CONFIG: %s", JsonDump(config).c_str());
      return MakeConfig(config);
    }

   private:
    Json::Object json_;
  };

  WeightedRoundRobinTest() {
    lb_policy_ = MakeLbPolicy("weighted_round_robin");
  }

  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>
  SendInitialUpdateAndWaitForConnected(
      absl::Span<const absl::string_view> addresses,
      ConfigBuilder config_builder = ConfigBuilder(),
      absl::Span<const absl::string_view> update_addresses = {},
      SourceLocation location = SourceLocation()) {
    if (update_addresses.empty()) update_addresses = addresses;
    EXPECT_EQ(ApplyUpdate(BuildUpdate(update_addresses, config_builder.Build()),
                          lb_policy_.get()),
              absl::OkStatus());
    // RR should have created a subchannel for each address.
    for (size_t i = 0; i < addresses.size(); ++i) {
      auto* subchannel = FindSubchannel(addresses[i]);
      EXPECT_NE(subchannel, nullptr)
          << addresses[i] << " at " << location.file() << ":"
          << location.line();
      if (subchannel == nullptr) return nullptr;
      // RR should ask each subchannel to connect.
      EXPECT_TRUE(subchannel->ConnectionRequested())
          << addresses[i] << " at " << location.file() << ":"
          << location.line();
      // The subchannel will connect successfully.
      subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
      // Expect the initial CONNECTNG update with a picker that queues.
      if (i == 0) ExpectConnectingUpdate(location);
      subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
    }
    return WaitForConnected(location);
  }

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
      const std::map<absl::string_view, size_t>& pick_map) {
    return absl::StrJoin(pick_map, ",", absl::PairFormatter("="));
  }

  // Returns the number of picks we need to do to check the specified
  // expectations.
  static size_t NumPicksNeeded(const std::map<absl::string_view /*address*/,
                                              size_t /*num_picks*/>& expected) {
    size_t num_picks = 0;
    for (const auto& p : expected) {
      num_picks += p.second;
    }
    return num_picks;
  }

  // For each pick in picks, reports the backend metrics to the LB policy.
  static void ReportBackendMetrics(
      absl::Span<const std::string> picks,
      const std::vector<
          std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>>&
          subchannel_call_trackers,
      const std::map<absl::string_view /*address*/, BackendMetricData>&
          backend_metrics) {
    for (size_t i = 0; i < picks.size(); ++i) {
      const auto& address = picks[i];
      auto& subchannel_call_tracker = subchannel_call_trackers[i];
      if (subchannel_call_tracker != nullptr) {
        subchannel_call_tracker->Start();
        absl::optional<BackendMetricData> backend_metric_data;
        auto it = backend_metrics.find(address);
        if (it != backend_metrics.end()) {
          backend_metric_data.emplace();
          backend_metric_data->qps = it->second.qps;
          backend_metric_data->eps = it->second.eps;
          backend_metric_data->cpu_utilization = it->second.cpu_utilization;
          backend_metric_data->application_utilization =
              it->second.application_utilization;
        }
        FakeMetadata metadata({});
        FakeBackendMetricAccessor backend_metric_accessor(
            std::move(backend_metric_data));
        LoadBalancingPolicy::SubchannelCallTrackerInterface::FinishArgs args = {
            address, absl::OkStatus(), &metadata, &backend_metric_accessor};
        subchannel_call_tracker->Finish(args);
      }
    }
  }

  void ReportOobBackendMetrics(
      const std::map<absl::string_view /*address*/, BackendMetricData>&
          backend_metrics) {
    for (const auto& p : backend_metrics) {
      auto* subchannel = FindSubchannel(p.first);
      BackendMetricData backend_metric_data;
      backend_metric_data.qps = p.second.qps;
      backend_metric_data.eps = p.second.eps;
      backend_metric_data.cpu_utilization = p.second.cpu_utilization;
      backend_metric_data.application_utilization =
          p.second.application_utilization;
      subchannel->SendOobBackendMetricReport(backend_metric_data);
    }
  }

  void ExpectWeightedRoundRobinPicks(
      LoadBalancingPolicy::SubchannelPicker* picker,
      const std::map<absl::string_view /*address*/, BackendMetricData>&
          backend_metrics,
      const std::map<absl::string_view /*address*/, size_t /*num_picks*/>&
          expected,
      SourceLocation location = SourceLocation()) {
    std::vector<
        std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>>
        subchannel_call_trackers;
    auto picks = GetCompletePicks(picker, NumPicksNeeded(expected), {},
                                  &subchannel_call_trackers, location);
    ASSERT_TRUE(picks.has_value()) << location.file() << ":" << location.line();
    gpr_log(GPR_INFO, "PICKS: %s", absl::StrJoin(*picks, " ").c_str());
    ReportBackendMetrics(*picks, subchannel_call_trackers, backend_metrics);
    auto actual = MakePickMap(*picks);
    gpr_log(GPR_INFO, "Pick map: %s", PickMapString(actual).c_str());
    EXPECT_EQ(expected, actual)
        << "Expected: " << PickMapString(expected)
        << "\nActual: " << PickMapString(actual) << "\nat " << location.file()
        << ":" << location.line();
  }

  bool WaitForWeightedRoundRobinPicks(
      RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>* picker,
      const std::map<absl::string_view /*address*/, BackendMetricData>&
          backend_metrics,
      std::map<absl::string_view /*address*/, size_t /*num_picks*/> expected,
      absl::Duration timeout = absl::Seconds(5),
      SourceLocation location = SourceLocation()) {
    gpr_log(GPR_INFO, "==> WaitForWeightedRoundRobinPicks(): Expecting %s",
            PickMapString(expected).c_str());
    size_t num_picks = NumPicksNeeded(expected);
    absl::Time deadline = absl::Now() + timeout;
    while (true) {
      gpr_log(GPR_INFO, "TOP OF LOOP");
      // We need to see the expected weights for 3 consecutive passes, just
      // to make sure we're consistently returning the right weights.
      size_t num_passes = 0;
      for (; num_passes < 3; ++num_passes) {
        gpr_log(GPR_INFO, "PASS %" PRIuPTR ": DOING PICKS", num_passes);
        std::vector<std::unique_ptr<
            LoadBalancingPolicy::SubchannelCallTrackerInterface>>
            subchannel_call_trackers;
        auto picks = GetCompletePicks(picker->get(), num_picks, {},
                                      &subchannel_call_trackers, location);
        EXPECT_TRUE(picks.has_value())
            << location.file() << ":" << location.line();
        if (!picks.has_value()) return false;
        gpr_log(GPR_INFO, "PICKS: %s", absl::StrJoin(*picks, " ").c_str());
        // Report backend metrics to the LB policy.
        ReportBackendMetrics(*picks, subchannel_call_trackers, backend_metrics);
        // Check the observed weights.
        auto actual = MakePickMap(*picks);
        gpr_log(GPR_INFO, "Pick map:\nExpected: %s\n  Actual: %s",
                PickMapString(expected).c_str(), PickMapString(actual).c_str());
        if (expected != actual) {
          // Make sure each address is one of the expected addresses,
          // even if the weights aren't as expected.
          for (const auto& address : *picks) {
            bool found = expected.find(address) != expected.end();
            EXPECT_TRUE(found)
                << "unexpected pick address " << address << " at "
                << location.file() << ":" << location.line();
            if (!found) return false;
          }
          break;
        }
        // If there's another picker update in the queue, don't bother
        // doing another pass, since we want to make sure we're using
        // the latest picker.
        if (!helper_->QueueEmpty()) break;
      }
      if (num_passes == 3) return true;
      // If we're out of time, give up.
      absl::Time now = absl::Now();
      EXPECT_LT(now, deadline) << location.file() << ":" << location.line();
      if (now >= deadline) return false;
      // Get a new picker if there is an update; otherwise, wait for the
      // weights to be recalculated.
      if (!helper_->QueueEmpty()) {
        *picker = ExpectState(GRPC_CHANNEL_READY, absl::OkStatus(), location);
        EXPECT_NE(*picker, nullptr)
            << location.file() << ":" << location.line();
        if (*picker == nullptr) return false;
      } else {
        gpr_log(GPR_INFO, "running timer callback...");
        RunTimerCallback();
      }
      // Increment time.
      time_cache_.IncrementBy(Duration::Seconds(1));
    }
  }

  void CheckExpectedTimerDuration(
      grpc_event_engine::experimental::EventEngine::Duration duration)
      override {
    EXPECT_EQ(duration, expected_weight_update_interval_)
        << "Expected: " << expected_weight_update_interval_.count() << "ns"
        << "\n  Actual: " << duration.count() << "ns";
  }

  OrphanablePtr<LoadBalancingPolicy> lb_policy_;
  grpc_event_engine::experimental::EventEngine::Duration
      expected_weight_update_interval_ = std::chrono::seconds(1);
};

TEST_F(WeightedRoundRobinTest, Basic) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(kAddresses);
  ASSERT_NE(picker, nullptr);
  // Address 0 gets weight 1, address 1 gets weight 3.
  // No utilization report from backend 2, so it gets the average weight 2.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 2}});
  // Now have backend 2 report utilization the same as backend 1, so its
  // weight will be the same.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
}

TEST_F(WeightedRoundRobinTest, CpuUtilWithNoAppUtil) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(kAddresses);
  ASSERT_NE(picker, nullptr);
  // Address 0 gets weight 1, address 1 gets weight 3.
  // No utilization report from backend 2, so it gets the average weight 2.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.9)},
       {kAddresses[1],
        MakeBackendMetricData(/*app_utilization=*/0,
                              /*qps=*/100.0,
                              /*eps=*/0.0, /*cpu_utilization=*/0.3)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 2}});
  // Now have backend 2 report utilization the same as backend 1, so its
  // weight will be the same.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.9)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.3)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.3)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
}

TEST_F(WeightedRoundRobinTest, AppUtilOverCpuUtil) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(kAddresses);
  ASSERT_NE(picker, nullptr);
  // Address 0 gets weight 1, address 1 gets weight 3.
  // No utilization report from backend 2, so it gets the average weight 2.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.3)},
       {kAddresses[1],
        MakeBackendMetricData(/*app_utilization=*/0.3,
                              /*qps=*/100.0,
                              /*eps=*/0.0, /*cpu_utilization=*/0.4)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 2}});
  // Now have backend 2 report utilization the same as backend 1, so its
  // weight will be the same.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.2)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.6)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.5)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
}

TEST_F(WeightedRoundRobinTest, Eps) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(
      kAddresses, ConfigBuilder().SetErrorUtilizationPenalty(1.0));
  ASSERT_NE(picker, nullptr);
  // Expected weights: 1/(0.1+0.5) : 1/(0.1+0.2) : 1/(0.1+0.1) = 1:2:3
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.1,
                                             /*qps=*/100.0, /*eps=*/50.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.1,
                                             /*qps=*/100.0, /*eps=*/20.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.1,
                                             /*qps=*/100.0, /*eps=*/10.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 2}, {kAddresses[2], 3}});
}

TEST_F(WeightedRoundRobinTest, IgnoresDuplicateAddresses) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  const std::array<absl::string_view, 4> kUpdateAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443",
      "ipv4:127.0.0.1:441"};
  auto picker = SendInitialUpdateAndWaitForConnected(
      kAddresses, ConfigBuilder(), kUpdateAddresses);
  ASSERT_NE(picker, nullptr);
  // Address 0 gets weight 1, address 1 gets weight 3.
  // No utilization report from backend 2, so it gets the average weight 2.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 2}});
  // Now have backend 2 report utilization the same as backend 1, so its
  // weight will be the same.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
}

TEST_F(WeightedRoundRobinTest, FallsBackToRoundRobinWithoutWeights) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(kAddresses);
  ASSERT_NE(picker, nullptr);
  // Backends do not report utilization, so all are weighted the same.
  WaitForWeightedRoundRobinPicks(
      &picker, {},
      {{kAddresses[0], 1}, {kAddresses[1], 1}, {kAddresses[2], 1}});
}

TEST_F(WeightedRoundRobinTest, OobReporting) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(
      kAddresses, ConfigBuilder().SetEnableOobLoadReport(true));
  ASSERT_NE(picker, nullptr);
  // Address 0 gets weight 1, address 1 gets weight 3.
  // No utilization report from backend 2, so it gets the average weight 2.
  ReportOobBackendMetrics(
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}});
  WaitForWeightedRoundRobinPicks(
      &picker, {},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 2}});
  // Now have backend 2 report utilization the same as backend 1, so its
  // weight will be the same.
  ReportOobBackendMetrics(
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}});
  WaitForWeightedRoundRobinPicks(
      &picker, {},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
  // Verify that OOB reporting interval is the default.
  for (const auto& address : kAddresses) {
    auto* subchannel = FindSubchannel(address);
    ASSERT_NE(subchannel, nullptr);
    subchannel->CheckOobReportingPeriod(Duration::Seconds(10));
  }
}

TEST_F(WeightedRoundRobinTest, OobReportingCpuUtilWithNoAppUtil) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(
      kAddresses, ConfigBuilder().SetEnableOobLoadReport(true));
  ASSERT_NE(picker, nullptr);
  // Address 0 gets weight 1, address 1 gets weight 3.
  // No utilization report from backend 2, so it gets the average weight 2.
  ReportOobBackendMetrics(
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.9)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.3)}});
  WaitForWeightedRoundRobinPicks(
      &picker, {},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 2}});
  // Now have backend 2 report utilization the same as backend 1, so its
  // weight will be the same.
  ReportOobBackendMetrics(
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.9)},
       {kAddresses[1],
        MakeBackendMetricData(/*app_utilization=*/0,
                              /*qps=*/100.0,
                              /*eps=*/0.0, /*cpu_utilization=*/0.3)},
       {kAddresses[2],
        MakeBackendMetricData(/*app_utilization=*/0,
                              /*qps=*/100.0,
                              /*eps=*/0.0, /*cpu_utilization=*/0.3)}});
  WaitForWeightedRoundRobinPicks(
      &picker, {},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
  // Verify that OOB reporting interval is the default.
  for (const auto& address : kAddresses) {
    auto* subchannel = FindSubchannel(address);
    ASSERT_NE(subchannel, nullptr);
    subchannel->CheckOobReportingPeriod(Duration::Seconds(10));
  }
}

TEST_F(WeightedRoundRobinTest, OobReportingAppUtilOverCpuUtil) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(
      kAddresses, ConfigBuilder().SetEnableOobLoadReport(true));
  ASSERT_NE(picker, nullptr);
  // Address 0 gets weight 1, address 1 gets weight 3.
  // No utilization report from backend 2, so it gets the average weight 2.
  ReportOobBackendMetrics(
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.3)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.4)}});
  WaitForWeightedRoundRobinPicks(
      &picker, {},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 2}});
  // Now have backend 2 report utilization the same as backend 1, so its
  // weight will be the same.
  ReportOobBackendMetrics(
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0,
                                             /*cpu_utilization=*/0.2)},
       {kAddresses[1],
        MakeBackendMetricData(/*app_utilization=*/0.3,
                              /*qps=*/100.0,
                              /*eps=*/0.0, /*cpu_utilization=*/0.6)},
       {kAddresses[2],
        MakeBackendMetricData(/*app_utilization=*/0.3,
                              /*qps=*/100.0,
                              /*eps=*/0.0, /*cpu_utilization=*/0.5)}});
  WaitForWeightedRoundRobinPicks(
      &picker, {},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
  // Verify that OOB reporting interval is the default.
  for (const auto& address : kAddresses) {
    auto* subchannel = FindSubchannel(address);
    ASSERT_NE(subchannel, nullptr);
    subchannel->CheckOobReportingPeriod(Duration::Seconds(10));
  }
}

TEST_F(WeightedRoundRobinTest, HonorsOobReportingPeriod) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(
      kAddresses,
      ConfigBuilder().SetEnableOobLoadReport(true).SetOobReportingPeriod(
          Duration::Seconds(5)));
  ASSERT_NE(picker, nullptr);
  ReportOobBackendMetrics(
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}});
  WaitForWeightedRoundRobinPicks(
      &picker, {},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
  for (const auto& address : kAddresses) {
    auto* subchannel = FindSubchannel(address);
    ASSERT_NE(subchannel, nullptr);
    subchannel->CheckOobReportingPeriod(Duration::Seconds(5));
  }
}

TEST_F(WeightedRoundRobinTest, HonorsWeightUpdatePeriod) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  expected_weight_update_interval_ = std::chrono::seconds(2);
  auto picker = SendInitialUpdateAndWaitForConnected(
      kAddresses, ConfigBuilder().SetWeightUpdatePeriod(Duration::Seconds(2)));
  ASSERT_NE(picker, nullptr);
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
}

TEST_F(WeightedRoundRobinTest, WeightUpdatePeriodLowerBound) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  expected_weight_update_interval_ = std::chrono::milliseconds(100);
  auto picker = SendInitialUpdateAndWaitForConnected(
      kAddresses,
      ConfigBuilder().SetWeightUpdatePeriod(Duration::Milliseconds(10)));
  ASSERT_NE(picker, nullptr);
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
}

TEST_F(WeightedRoundRobinTest, WeightExpirationPeriod) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(
      kAddresses,
      ConfigBuilder().SetWeightExpirationPeriod(Duration::Seconds(2)));
  ASSERT_NE(picker, nullptr);
  // All backends report weights.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
  // Advance time to make weights stale and trigger the timer callback
  // to recompute weights.
  time_cache_.IncrementBy(Duration::Seconds(2));
  RunTimerCallback();
  // Picker should now be falling back to round-robin.
  ExpectWeightedRoundRobinPicks(
      picker.get(), {},
      {{kAddresses[0], 3}, {kAddresses[1], 3}, {kAddresses[2], 3}});
}

TEST_F(WeightedRoundRobinTest, BlackoutPeriodAfterWeightExpiration) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(
      kAddresses,
      ConfigBuilder().SetWeightExpirationPeriod(Duration::Seconds(2)));
  ASSERT_NE(picker, nullptr);
  // All backends report weights.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
  // Advance time to make weights stale and trigger the timer callback
  // to recompute weights.
  time_cache_.IncrementBy(Duration::Seconds(2));
  RunTimerCallback();
  // Picker should now be falling back to round-robin.
  ExpectWeightedRoundRobinPicks(
      picker.get(), {},
      {{kAddresses[0], 3}, {kAddresses[1], 3}, {kAddresses[2], 3}});
  // Now start sending weights again.  They should not be used yet,
  // because we're still in the blackout period.
  ExpectWeightedRoundRobinPicks(
      picker.get(),
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)}},
      {{kAddresses[0], 3}, {kAddresses[1], 3}, {kAddresses[2], 3}});
  // Advance time past the blackout period.  This should cause the
  // weights to be used.
  time_cache_.IncrementBy(Duration::Seconds(1));
  RunTimerCallback();
  ExpectWeightedRoundRobinPicks(
      picker.get(), {},
      {{kAddresses[0], 3}, {kAddresses[1], 3}, {kAddresses[2], 1}});
}

TEST_F(WeightedRoundRobinTest, BlackoutPeriodAfterDisconnect) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(
      kAddresses,
      ConfigBuilder().SetWeightExpirationPeriod(Duration::Seconds(2)));
  ASSERT_NE(picker, nullptr);
  // All backends report weights.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
  // Trigger disconnection and reconnection on address 2.
  auto* subchannel = FindSubchannel(kAddresses[2]);
  subchannel->SetConnectivityState(GRPC_CHANNEL_IDLE);
  ExpectReresolutionRequest();
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  // Wait for the address to come back.  Note that we have not advanced
  // time, so the address will still be in the blackout period,
  // resulting in it being assigned the average weight.
  picker = ExpectState(GRPC_CHANNEL_READY, absl::OkStatus());
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 2}});
  // Advance time to exceed the blackout period and trigger the timer
  // callback to recompute weights.
  time_cache_.IncrementBy(Duration::Seconds(1));
  RunTimerCallback();
  ExpectWeightedRoundRobinPicks(
      picker.get(),
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.3,
                                             /*qps=*/100.0, /*eps=*/0.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.9,
                                             /*qps=*/100.0, /*eps=*/0.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
}

TEST_F(WeightedRoundRobinTest, ZeroErrorUtilPenalty) {
  // Send address list to LB policy.
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  auto picker = SendInitialUpdateAndWaitForConnected(
      kAddresses, ConfigBuilder().SetErrorUtilizationPenalty(0.0));
  ASSERT_NE(picker, nullptr);
  // Expected weights: 1:1:1
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], MakeBackendMetricData(/*app_utilization=*/0.1,
                                             /*qps=*/100.0, /*eps=*/50.0)},
       {kAddresses[1], MakeBackendMetricData(/*app_utilization=*/0.1,
                                             /*qps=*/100.0, /*eps=*/20.0)},
       {kAddresses[2], MakeBackendMetricData(/*app_utilization=*/0.1,
                                             /*qps=*/100.0, /*eps=*/10.0)}},
      {{kAddresses[0], 1}, {kAddresses[1], 1}, {kAddresses[2], 1}});
}

// FIXME: add test for multiple addresses per endpoint

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
