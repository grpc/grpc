// Copyright 2024 The gRPC Authors.
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

#include "src/core/telemetry/metrics.h"

#include <memory>
#include <thread>

#include "absl/log/log.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

using experimental::StatsPluginChannelScope;

class MetricsTest : public ::testing::Test {
 protected:
  MetricsTest() : endpoint_config_(ChannelArgs()) {}

  void TearDown() override {
    GlobalInstrumentsRegistryTestPeer::ResetGlobalInstrumentsRegistry();
    GlobalStatsPluginRegistryTestPeer::ResetGlobalStatsPluginRegistry();
  }

  grpc_event_engine::experimental::ChannelArgsEndpointConfig endpoint_config_;
};

TEST_F(MetricsTest, UInt64Counter) {
  auto uint64_counter_handle =
      GlobalInstrumentsRegistry::RegisterUInt64Counter(
          "uint64_counter", "A simple uint64 counter.", "unit", true)
          .Labels("label_key_1", "label_key_2")
          .OptionalLabels("optional_label_key_1", "optional_label_key_2")
          .Build();
  std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                   "label_value_2"};
  std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  constexpr absl::string_view kDomain1To4 = "domain1.domain2.domain3.domain4";
  constexpr absl::string_view kDomain2To4 = "domain2.domain3.domain4";
  constexpr absl::string_view kDomain3To4 = "domain3.domain4";
  auto plugin1 = MakeStatsPluginForTarget(kDomain1To4);
  auto plugin2 = MakeStatsPluginForTarget(kDomain2To4);
  auto plugin3 = MakeStatsPluginForTarget(kDomain3To4);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain1To4, "", endpoint_config_))
      ->AddCounter(uint64_counter_handle, uint64_t(1), kLabelValues,
                   kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain2To4, "", endpoint_config_))
      ->AddCounter(uint64_counter_handle, uint64_t(2), kLabelValues,
                   kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain3To4, "", endpoint_config_))
      ->AddCounter(uint64_counter_handle, uint64_t(3), kLabelValues,
                   kOptionalLabelValues);
  EXPECT_THAT(plugin1->GetUInt64CounterValue(
                  uint64_counter_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(1));
  EXPECT_THAT(plugin2->GetUInt64CounterValue(
                  uint64_counter_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(3));
  EXPECT_THAT(plugin3->GetUInt64CounterValue(
                  uint64_counter_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(6));
}

TEST_F(MetricsTest, DoubleCounter) {
  auto double_counter_handle =
      GlobalInstrumentsRegistry::RegisterDoubleCounter(
          "double_counter", "A simple double counter.", "unit", true)
          .Labels("label_key_1", "label_key_2")
          .OptionalLabels("optional_label_key_1", "optional_label_key_2")
          .Build();
  std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                   "label_value_2"};
  std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  constexpr absl::string_view kDomain1To4 = "domain1.domain2.domain3.domain4";
  constexpr absl::string_view kDomain2To4 = "domain2.domain3.domain4";
  constexpr absl::string_view kDomain3To4 = "domain3.domain4";
  auto plugin1 = MakeStatsPluginForTarget(kDomain1To4);
  auto plugin2 = MakeStatsPluginForTarget(kDomain2To4);
  auto plugin3 = MakeStatsPluginForTarget(kDomain3To4);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain1To4, "", endpoint_config_))
      ->AddCounter(double_counter_handle, 1.23, kLabelValues,
                   kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain2To4, "", endpoint_config_))
      ->AddCounter(double_counter_handle, 2.34, kLabelValues,
                   kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain3To4, "", endpoint_config_))
      ->AddCounter(double_counter_handle, 3.45, kLabelValues,
                   kOptionalLabelValues);
  EXPECT_THAT(plugin1->GetDoubleCounterValue(
                  double_counter_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(1.23));
  EXPECT_THAT(plugin2->GetDoubleCounterValue(
                  double_counter_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(3.57));
  EXPECT_THAT(plugin3->GetDoubleCounterValue(
                  double_counter_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(7.02));
}

TEST_F(MetricsTest, UInt64Histogram) {
  auto uint64_histogram_handle =
      GlobalInstrumentsRegistry::RegisterUInt64Histogram(
          "uint64_histogram", "A simple uint64 histogram.", "unit", true)
          .Labels("label_key_1", "label_key_2")
          .OptionalLabels("optional_label_key_1", "optional_label_key_2")
          .Build();
  std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                   "label_value_2"};
  std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  constexpr absl::string_view kDomain1To4 = "domain1.domain2.domain3.domain4";
  constexpr absl::string_view kDomain2To4 = "domain2.domain3.domain4";
  constexpr absl::string_view kDomain3To4 = "domain3.domain4";
  auto plugin1 = MakeStatsPluginForTarget(kDomain1To4);
  auto plugin2 = MakeStatsPluginForTarget(kDomain2To4);
  auto plugin3 = MakeStatsPluginForTarget(kDomain3To4);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain1To4, "", endpoint_config_))
      ->RecordHistogram(uint64_histogram_handle, uint64_t(1), kLabelValues,
                        kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain2To4, "", endpoint_config_))
      ->RecordHistogram(uint64_histogram_handle, uint64_t(2), kLabelValues,
                        kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain3To4, "", endpoint_config_))
      ->RecordHistogram(uint64_histogram_handle, uint64_t(3), kLabelValues,
                        kOptionalLabelValues);
  EXPECT_THAT(plugin1->GetUInt64HistogramValue(
                  uint64_histogram_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(::testing::UnorderedElementsAre(1)));
  EXPECT_THAT(plugin2->GetUInt64HistogramValue(
                  uint64_histogram_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(::testing::UnorderedElementsAre(1, 2)));
  EXPECT_THAT(plugin3->GetUInt64HistogramValue(
                  uint64_histogram_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(::testing::UnorderedElementsAre(1, 2, 3)));
}

TEST_F(MetricsTest, DoubleHistogram) {
  auto double_histogram_handle =
      GlobalInstrumentsRegistry::RegisterDoubleHistogram(
          "double_histogram", "A simple double histogram.", "unit", true)
          .Labels("label_key_1", "label_key_2")
          .OptionalLabels("optional_label_key_1", "optional_label_key_2")
          .Build();
  std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                   "label_value_2"};
  std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  constexpr absl::string_view kDomain1To4 = "domain1.domain2.domain3.domain4";
  constexpr absl::string_view kDomain2To4 = "domain2.domain3.domain4";
  constexpr absl::string_view kDomain3To4 = "domain3.domain4";
  auto plugin1 = MakeStatsPluginForTarget(kDomain1To4);
  auto plugin2 = MakeStatsPluginForTarget(kDomain2To4);
  auto plugin3 = MakeStatsPluginForTarget(kDomain3To4);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain1To4, "", endpoint_config_))
      ->RecordHistogram(double_histogram_handle, 1.23, kLabelValues,
                        kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain2To4, "", endpoint_config_))
      ->RecordHistogram(double_histogram_handle, 2.34, kLabelValues,
                        kOptionalLabelValues);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain3To4, "", endpoint_config_))
      ->RecordHistogram(double_histogram_handle, 3.45, kLabelValues,
                        kOptionalLabelValues);
  EXPECT_THAT(plugin1->GetDoubleHistogramValue(
                  double_histogram_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(::testing::UnorderedElementsAre(1.23)));
  EXPECT_THAT(plugin2->GetDoubleHistogramValue(
                  double_histogram_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(::testing::UnorderedElementsAre(1.23, 2.34)));
  EXPECT_THAT(
      plugin3->GetDoubleHistogramValue(double_histogram_handle, kLabelValues,
                                       kOptionalLabelValues),
      ::testing::Optional(::testing::UnorderedElementsAre(1.23, 2.34, 3.45)));
}

TEST_F(MetricsTest, Int64CallbackGauge) {
  auto int64_gauge_handle =
      GlobalInstrumentsRegistry::RegisterCallbackInt64Gauge(
          "int64_gauge", "A simple int64 gauge.", "unit", true)
          .Labels("label_key_1", "label_key_2")
          .OptionalLabels("optional_label_key_1", "optional_label_key_2")
          .Build();
  std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                   "label_value_2"};
  std::array<absl::string_view, 2> kLabelValues2 = {"label_value_3",
                                                    "label_value_2"};
  std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  constexpr absl::string_view kDomain1To4 = "domain1.domain2.domain3.domain4";
  constexpr absl::string_view kDomain2To4 = "domain2.domain3.domain4";
  constexpr absl::string_view kDomain3To4 = "domain3.domain4";
  auto plugin1 = MakeStatsPluginForTarget(kDomain3To4);
  auto plugin2 = MakeStatsPluginForTarget(kDomain2To4);
  auto plugin3 = MakeStatsPluginForTarget(kDomain1To4);
  // Register two callbacks that set the same metric but with different
  // label values.  The callbacks get used only by plugin1.
  LOG(INFO) << "testing callbacks for: plugin1";
  auto group1 = GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain3To4, "", endpoint_config_));
  auto callback1 = group1->RegisterCallback(
      [&](CallbackMetricReporter& reporter) {
        reporter.Report(int64_gauge_handle, int64_t(1), kLabelValues,
                        kOptionalLabelValues);
      },
      Duration::Seconds(5), int64_gauge_handle);
  auto callback2 = group1->RegisterCallback(
      [&](CallbackMetricReporter& reporter) {
        reporter.Report(int64_gauge_handle, int64_t(2), kLabelValues2,
                        kOptionalLabelValues);
      },
      Duration::Seconds(5), int64_gauge_handle);
  // No plugins have data yet.
  EXPECT_EQ(plugin1->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin1->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin2->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin2->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  // Now invoke the callbacks.
  plugin1->TriggerCallbacks();
  plugin2->TriggerCallbacks();
  plugin3->TriggerCallbacks();
  // Now plugin1 should have data, but the others should not.
  EXPECT_THAT(plugin1->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(1));
  EXPECT_THAT(plugin1->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(2));
  EXPECT_EQ(plugin2->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin2->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  // De-register the callbacks.
  callback1.reset();
  callback2.reset();
  // Now register callbacks that hit both plugin1 and plugin2.
  LOG(INFO) << "testing callbacks for: plugin1, plugin2";
  auto group2 = GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain2To4, "", endpoint_config_));
  callback1 = group2->RegisterCallback(
      [&](CallbackMetricReporter& reporter) {
        reporter.Report(int64_gauge_handle, int64_t(3), kLabelValues,
                        kOptionalLabelValues);
      },
      Duration::Seconds(5), int64_gauge_handle);
  callback2 = group2->RegisterCallback(
      [&](CallbackMetricReporter& reporter) {
        reporter.Report(int64_gauge_handle, int64_t(4), kLabelValues2,
                        kOptionalLabelValues);
      },
      Duration::Seconds(5), int64_gauge_handle);
  // Plugin1 still has data from before, but the others have none.
  EXPECT_THAT(plugin1->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(1));
  EXPECT_THAT(plugin1->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(2));
  EXPECT_EQ(plugin2->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin2->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  // Now invoke the callbacks.
  plugin1->TriggerCallbacks();
  plugin2->TriggerCallbacks();
  plugin3->TriggerCallbacks();
  // Now plugin1 and plugin2 should have data, but plugin3 should not.
  EXPECT_THAT(plugin1->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(3));
  EXPECT_THAT(plugin1->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(4));
  EXPECT_THAT(plugin2->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(3));
  EXPECT_THAT(plugin2->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(4));
  EXPECT_EQ(plugin3->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  // De-register the callbacks.
  callback1.reset();
  callback2.reset();
  // Now register callbacks that hit all three plugins.
  LOG(INFO) << "testing callbacks for: plugin1, plugin2, plugin3";
  auto group3 = GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain1To4, "", endpoint_config_));
  callback1 = group3->RegisterCallback(
      [&](CallbackMetricReporter& reporter) {
        reporter.Report(int64_gauge_handle, int64_t(5), kLabelValues,
                        kOptionalLabelValues);
      },
      Duration::Seconds(5), int64_gauge_handle);
  callback2 = group3->RegisterCallback(
      [&](CallbackMetricReporter& reporter) {
        reporter.Report(int64_gauge_handle, int64_t(6), kLabelValues2,
                        kOptionalLabelValues);
      },
      Duration::Seconds(5), int64_gauge_handle);
  // Plugin1 and plugin2 still has data from before, but plugin3 has none.
  EXPECT_THAT(plugin1->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(3));
  EXPECT_THAT(plugin1->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(4));
  EXPECT_THAT(plugin2->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(3));
  EXPECT_THAT(plugin2->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(4));
  EXPECT_EQ(plugin3->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetInt64CallbackGaugeValue(
                int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  // Now invoke the callbacks.
  plugin1->TriggerCallbacks();
  plugin2->TriggerCallbacks();
  plugin3->TriggerCallbacks();
  // Now plugin1 and plugin2 should have data, but plugin3 should not.
  EXPECT_THAT(plugin1->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(5));
  EXPECT_THAT(plugin1->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(6));
  EXPECT_THAT(plugin2->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(5));
  EXPECT_THAT(plugin2->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(6));
  EXPECT_THAT(plugin3->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(5));
  EXPECT_THAT(plugin3->GetInt64CallbackGaugeValue(
                  int64_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(6));
  // Need to destroy callbacks before the plugin group that created them.
  callback1.reset();
  callback2.reset();
}

TEST_F(MetricsTest, DoubleCallbackGauge) {
  auto double_gauge_handle =
      GlobalInstrumentsRegistry::RegisterCallbackDoubleGauge(
          "double_gauge", "A simple double gauge.", "unit", true)
          .Labels("label_key_1", "label_key_2")
          .OptionalLabels("optional_label_key_1", "optional_label_key_2")
          .Build();
  std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                   "label_value_2"};
  std::array<absl::string_view, 2> kLabelValues2 = {"label_value_3",
                                                    "label_value_2"};
  std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  constexpr absl::string_view kDomain1To4 = "domain1.domain2.domain3.domain4";
  constexpr absl::string_view kDomain2To4 = "domain2.domain3.domain4";
  constexpr absl::string_view kDomain3To4 = "domain3.domain4";
  auto plugin1 = MakeStatsPluginForTarget(kDomain3To4);
  auto plugin2 = MakeStatsPluginForTarget(kDomain2To4);
  auto plugin3 = MakeStatsPluginForTarget(kDomain1To4);
  // Register two callbacks that set the same metric but with different
  // label values.  The callbacks get used only by plugin1.
  LOG(INFO) << "testing callbacks for: plugin1";
  auto group1 = GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain3To4, "", endpoint_config_));
  auto callback1 = group1->RegisterCallback(
      [&](CallbackMetricReporter& reporter) {
        reporter.Report(double_gauge_handle, 1.23, kLabelValues,
                        kOptionalLabelValues);
      },
      Duration::Seconds(5), double_gauge_handle);
  auto callback2 = group1->RegisterCallback(
      [&](CallbackMetricReporter& reporter) {
        reporter.Report(double_gauge_handle, 2.34, kLabelValues2,
                        kOptionalLabelValues);
      },
      Duration::Seconds(5), double_gauge_handle);
  // No plugins have data yet.
  EXPECT_EQ(plugin1->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin1->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin2->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin2->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  // Now invoke the callbacks.
  plugin1->TriggerCallbacks();
  plugin2->TriggerCallbacks();
  plugin3->TriggerCallbacks();
  // Now plugin1 should have data, but the others should not.
  EXPECT_THAT(plugin1->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(1.23));
  EXPECT_THAT(plugin1->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(2.34));
  EXPECT_EQ(plugin2->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin2->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  // De-register the callbacks.
  callback1.reset();
  callback2.reset();
  // Now register callbacks that hit both plugin1 and plugin2.
  LOG(INFO) << "testing callbacks for: plugin1, plugin2";
  auto group2 = GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain2To4, "", endpoint_config_));
  callback1 = group2->RegisterCallback(
      [&](CallbackMetricReporter& reporter) {
        reporter.Report(double_gauge_handle, 3.45, kLabelValues,
                        kOptionalLabelValues);
      },
      Duration::Seconds(5), double_gauge_handle);
  callback2 = group2->RegisterCallback(
      [&](CallbackMetricReporter& reporter) {
        reporter.Report(double_gauge_handle, 4.56, kLabelValues2,
                        kOptionalLabelValues);
      },
      Duration::Seconds(5), double_gauge_handle);
  // Plugin1 still has data from before, but the others have none.
  EXPECT_THAT(plugin1->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(1.23));
  EXPECT_THAT(plugin1->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(2.34));
  EXPECT_EQ(plugin2->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin2->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  // Now invoke the callbacks.
  plugin1->TriggerCallbacks();
  plugin2->TriggerCallbacks();
  plugin3->TriggerCallbacks();
  // Now plugin1 and plugin2 should have data, but plugin3 should not.
  EXPECT_THAT(plugin1->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(3.45));
  EXPECT_THAT(plugin1->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(4.56));
  EXPECT_THAT(plugin2->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(3.45));
  EXPECT_THAT(plugin2->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(4.56));
  EXPECT_EQ(plugin3->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  // De-register the callbacks.
  callback1.reset();
  callback2.reset();
  // Now register callbacks that hit all three plugins.
  LOG(INFO) << "testing callbacks for: plugin1, plugin2, plugin3";
  auto group3 = GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain1To4, "", endpoint_config_));
  callback1 = group3->RegisterCallback(
      [&](CallbackMetricReporter& reporter) {
        reporter.Report(double_gauge_handle, 5.67, kLabelValues,
                        kOptionalLabelValues);
      },
      Duration::Seconds(5), double_gauge_handle);
  callback2 = group3->RegisterCallback(
      [&](CallbackMetricReporter& reporter) {
        reporter.Report(double_gauge_handle, 6.78, kLabelValues2,
                        kOptionalLabelValues);
      },
      Duration::Seconds(5), double_gauge_handle);
  // Plugin1 and plugin2 still has data from before, but plugin3 has none.
  EXPECT_THAT(plugin1->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(3.45));
  EXPECT_THAT(plugin1->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(4.56));
  EXPECT_THAT(plugin2->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(3.45));
  EXPECT_THAT(plugin2->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(4.56));
  EXPECT_EQ(plugin3->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues, kOptionalLabelValues),
            std::nullopt);
  EXPECT_EQ(plugin3->GetDoubleCallbackGaugeValue(
                double_gauge_handle, kLabelValues2, kOptionalLabelValues),
            std::nullopt);
  // Now invoke the callbacks.
  plugin1->TriggerCallbacks();
  plugin2->TriggerCallbacks();
  plugin3->TriggerCallbacks();
  // Now plugin1 and plugin2 should have data, but plugin3 should not.
  EXPECT_THAT(plugin1->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(5.67));
  EXPECT_THAT(plugin1->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(6.78));
  EXPECT_THAT(plugin2->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(5.67));
  EXPECT_THAT(plugin2->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(6.78));
  EXPECT_THAT(plugin3->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues, kOptionalLabelValues),
              ::testing::Optional(5.67));
  EXPECT_THAT(plugin3->GetDoubleCallbackGaugeValue(
                  double_gauge_handle, kLabelValues2, kOptionalLabelValues),
              ::testing::Optional(6.78));
  // Need to destroy callbacks before the plugin group that created them.
  callback1.reset();
  callback2.reset();
}

TEST_F(MetricsTest, DisableByDefaultMetricIsNotRecordedByFakeStatsPlugin) {
  auto double_histogram_handle =
      GlobalInstrumentsRegistry::RegisterDoubleHistogram(
          "double_histogram", "A simple double histogram.", "unit", false)
          .Labels("label_key_1", "label_key_2")
          .OptionalLabels("optional_label_key_1", "optional_label_key_2")
          .Build();
  std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                   "label_value_2"};
  std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  constexpr absl::string_view kDomain1To4 = "domain1.domain2.domain3.domain4";
  auto plugin = MakeStatsPluginForTarget(kDomain1To4);
  GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
      StatsPluginChannelScope(kDomain1To4, "", endpoint_config_))
      ->RecordHistogram(double_histogram_handle, 1.23, kLabelValues,
                        kOptionalLabelValues);
  EXPECT_EQ(plugin->GetDoubleHistogramValue(double_histogram_handle,
                                            kLabelValues, kOptionalLabelValues),
            std::nullopt);
}

TEST_F(MetricsTest, FindInstrumentByName) {
  auto uint64_counter_handle =
      GlobalInstrumentsRegistry::RegisterUInt64Counter(
          "uint64_counter", "A simple uint64 counter.", "unit", true)
          .Labels("label_key_1", "label_key_2")
          .OptionalLabels("optional_label_key_1", "optional_label_key_2")
          .Build();
  auto instrument =
      GlobalInstrumentsRegistry::FindInstrumentByName("uint64_counter");
  EXPECT_THAT(instrument,
              ::testing::Optional(::testing::Field(
                  &GlobalInstrumentsRegistry::GlobalInstrumentHandle::index,
                  ::testing::Eq(uint64_counter_handle.index))));
}

TEST_F(MetricsTest, ParallelStatsPluginRegistrationAndLookup) {
  std::vector<std::thread> register_threads;
  std::vector<std::thread> lookup_threads;
  register_threads.reserve(100);
  lookup_threads.reserve(100);
  // 100 threads that register 100 stats plugins each
  for (int i = 0; i < 100; ++i) {
    register_threads.emplace_back([] {
      for (int j = 0; j < 100; ++j) {
        FakeStatsPluginBuilder().BuildAndRegister();
      }
    });
  }
  // 100 threads that keep looking up stats plugins till they see 10000 stats
  // plugins
  for (int i = 0; i < 100; ++i) {
    lookup_threads.emplace_back([this] {
      while (GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
                 StatsPluginChannelScope("", "", endpoint_config_))
                 ->size() < 10000) {
        // Yield to avoid starving the register threads.
        std::this_thread::yield();
      };
    });
  }
  for (int i = 0; i < 100; ++i) {
    register_threads[i].join();
    lookup_threads[i].join();
  }
  EXPECT_THAT(GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
                  StatsPluginChannelScope("", "", endpoint_config_)),
              ::testing::Pointee(::testing::SizeIs(10000)));
}

using MetricsDeathTest = MetricsTest;

TEST_F(MetricsDeathTest, RegisterTheSameMetricNameWouldCrash) {
  (void)GlobalInstrumentsRegistry::RegisterDoubleHistogram(
      "double_histogram", "A simple double histogram.", "unit", true)
      .Labels("label_key_1", "label_key_2")
      .OptionalLabels("optional_label_key_1", "optional_label_key_2")
      .Build();
  EXPECT_DEATH(
      GlobalInstrumentsRegistry::RegisterDoubleHistogram(
          "double_histogram", "A simple double histogram.", "unit", true)
          .Labels("label_key_1", "label_key_2")
          .OptionalLabels("optional_label_key_1", "optional_label_key_2")
          .Build(),
      "Metric name double_histogram has already been registered.");
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_core::GlobalInstrumentsRegistryTestPeer::
      ResetGlobalInstrumentsRegistry();
  int ret = RUN_ALL_TESTS();
  return ret;
}
