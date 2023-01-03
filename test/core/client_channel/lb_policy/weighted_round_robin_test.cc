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
#include <queue>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/event_engine/mock_event_engine.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

using ::testing::StrictMock;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::MockEventEngine;

class WeightedRoundRobinTest : public LoadBalancingPolicyTest {
 protected:
  class ConfigBuilder {
   public:
    ConfigBuilder& SetEnableOobLoadReport(bool value) {
      json_["enableOobLoadReport"] = value;
      return *this;
    }
    ConfigBuilder& SetOobReportingPeriod(Duration duration) {
      json_["oobReportingPeriod"] = duration.ToJsonString();
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

  WeightedRoundRobinTest() {
    mock_ee_ = std::make_shared<StrictMock<MockEventEngine>>();
    event_engine_ = mock_ee_;
    auto capture = [this](absl::AnyInvocable<void()> callback) {
      timer_callbacks_.push(std::move(callback));
    };
    ON_CALL(*mock_ee_,
            RunAfter(::testing::_, ::testing::A<absl::AnyInvocable<void()>>()))
        .WillByDefault(::testing::DoAll(
            ::testing::WithArg<1>(capture),
            ::testing::Return(EventEngine::TaskHandle{1, 2})));
    lb_policy_ = MakeLbPolicy("weighted_round_robin");
  }

  ~WeightedRoundRobinTest() override {
    EXPECT_TRUE(timer_callbacks_.empty())
        << "WARNING: Test did not run all timer callbacks; running "
           "outstanding callbacks to avoid blocking WRR destruction.";
    while (!timer_callbacks_.empty()) {
      RunTimerCallback();
    }
  }

  void RunTimerCallback() {
    ASSERT_FALSE(timer_callbacks_.empty());
    ASSERT_NE(timer_callbacks_.front(), nullptr);
    std::move(timer_callbacks_.front())();
    timer_callbacks_.pop();
  }

  bool WaitForTimerCallback(Duration timeout = Duration::Seconds(10)) {
    Timestamp deadline = Timestamp::Now() + timeout;
    bool retval = false;
    do {
      if (!timer_callbacks_.empty()) {
        RunTimerCallback();
        retval = true;
        break;
      }
      absl::SleepFor(absl::Milliseconds(500));
    } while (Timestamp::Now() < deadline);
    return retval;
  }

  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>
  SendInitialUpdateAndWaitForConnected(
      absl::Span<const absl::string_view> addresses,
      SourceLocation location = SourceLocation()) {
    EXPECT_EQ(
        ApplyUpdate(
            BuildUpdate(addresses, ConfigBuilder().Build()), lb_policy_.get()),
        absl::OkStatus());
    // Expect the initial CONNECTNG update with a picker that queues.
    ExpectConnectingUpdate(location);
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
    auto picks = GetCompletePicks(picker, NumPicksNeeded(expected), {},
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
      auto picks = GetCompletePicks(picker->get(), num_picks, {},
                                    &subchannel_call_trackers, location);
      EXPECT_TRUE(picks.has_value())
          << location.file() << ":" << location.line();
      if (!picks.has_value()) return false;
      gpr_log(GPR_INFO, "PICKS: %s", absl::StrJoin(*picks, " ").c_str());
      // Report backend metrics to the LB policy.
      ReportBackendMetrics(*picks, subchannel_call_trackers, backend_metrics);
      // If the picks have the expected weights, we're done.
      auto actual = MakePickMap(*picks);
      if (expected == actual) {
        // Do a couple more passes, just to make sure we're consistently
        // returning the right weights.
        for (size_t i = 0; i < 2; ++i) {
          gpr_log(GPR_INFO, "verifying WRR picks...");
          ExpectWeightedRoundRobinPicks(picker->get(), backend_metrics,
                                        expected, location);
        }
        return true;
      }
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
      // Wait for weights to be recalculated.
      EXPECT_TRUE(WaitForTimerCallback());
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
  std::shared_ptr<StrictMock<MockEventEngine>> mock_ee_;
  std::queue<absl::AnyInvocable<void()>> timer_callbacks_;
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
      {{kAddresses[0], {100, 0.9}}, {kAddresses[1], {100, 0.3}}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 2}});
  // Now have backend 2 report utilization the same as backend 1, so its
  // weight will be the same.
  WaitForWeightedRoundRobinPicks(
      &picker,
      {{kAddresses[0], {100, 0.9}},
       {kAddresses[1], {100, 0.3}},
       {kAddresses[2], {100, 0.3}}},
      {{kAddresses[0], 1}, {kAddresses[1], 3}, {kAddresses[2], 3}});
  // Backends stop reporting utilization, so all are weighted the same.
  WaitForWeightedRoundRobinPicks(
      &picker, {},
      {{kAddresses[0], 1}, {kAddresses[1], 1}, {kAddresses[2], 1}});
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
