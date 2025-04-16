//
//
// Copyright 2025 gRPC authors.
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
//

#include <grpcpp/ext/otel_plugin.h>
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <ratio>
#include <type_traits>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/metrics/data/metric_data.h"
#include "opentelemetry/sdk/metrics/data/point_data.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/ext/otel/otel_test_library.h"

namespace grpc {
namespace testing {
namespace {

#define GRPC_ARG_SERVER_SELECTOR_KEY "grpc.testing.server_selector_key"
#define GRPC_ARG_SERVER_SELECTOR_VALUE "grpc.testing.server_selector_value"

class OpenTelemetryPluginNPCMetricsTest
    : public OpenTelemetryPluginEnd2EndTest {
 protected:
  OpenTelemetryPluginNPCMetricsTest()
      : endpoint_config_(grpc_core::ChannelArgs()) {}

  void TearDown() override {
    // We are tearing down OpenTelemetryPluginEnd2EndTest first to ensure that
    // gRPC has shutdown before we reset the instruments registry.
    OpenTelemetryPluginEnd2EndTest::TearDown();
    grpc_core::GlobalInstrumentsRegistryTestPeer::
        ResetGlobalInstrumentsRegistry();
  }

  grpc_event_engine::experimental::ChannelArgsEndpointConfig endpoint_config_;
};

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordUInt64Counter) {
  constexpr absl::string_view kMetricName = "uint64_counter";
  constexpr uint64_t kCounterValues[] = {1, 2, 3};
  constexpr int64_t kCounterResult = 6;
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  auto handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
          kMetricName, "A simple uint64 counter.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  Init(std::move(Options()
                     .set_metric_names({kMetricName})
                     .set_channel_scope_filter(
                         [](const OpenTelemetryPluginBuilder::ChannelScope&
                                channel_scope) {
                           return absl::StartsWith(channel_scope.target(),
                                                   "dns:///");
                         })
                     .add_optional_label(kOptionalLabelKeys[0])
                     .add_optional_label(kOptionalLabelKeys[1])));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope(
              "dns:///localhost:8080", "", endpoint_config_));
  for (auto v : kCounterValues) {
    stats_plugins->AddCounter(handle, v, kLabelValues, kOptionalLabelValues);
  }
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(data,
              ::testing::ElementsAre(::testing::Pair(
                  kMetricName,
                  ::testing::ElementsAre(::testing::AllOf(
                      AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                   kOptionalLabelValues),
                      CounterResultEq(::testing::Eq(kCounterResult)))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordDoubleCounter) {
  constexpr absl::string_view kMetricName = "double_counter";
  constexpr double kCounterValues[] = {1.23, 2.34, 3.45};
  constexpr double kCounterResult = 7.02;
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  auto handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterDoubleCounter(
          kMetricName, "A simple double counter.", "unit",
          /*enable_by_default=*/false)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  Init(std::move(Options()
                     .set_metric_names({kMetricName})
                     .set_channel_scope_filter(
                         [](const OpenTelemetryPluginBuilder::ChannelScope&
                                channel_scope) {
                           return absl::StartsWith(channel_scope.target(),
                                                   "dns:///");
                         })
                     .add_optional_label(kOptionalLabelKeys[0])
                     .add_optional_label(kOptionalLabelKeys[1])));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope(
              "dns:///localhost:8080", "", endpoint_config_));
  for (auto v : kCounterValues) {
    stats_plugins->AddCounter(handle, v, kLabelValues, kOptionalLabelValues);
  }
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(data,
              ::testing::ElementsAre(::testing::Pair(
                  kMetricName,
                  ::testing::ElementsAre(::testing::AllOf(
                      AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                   kOptionalLabelValues),
                      CounterResultEq(::testing::DoubleEq(kCounterResult)))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordUInt64Histogram) {
  constexpr absl::string_view kMetricName = "uint64_histogram";
  constexpr uint64_t kHistogramValues[] = {1, 1, 2, 3, 4, 4, 5, 6};
  constexpr int64_t kSum = 26;
  constexpr int64_t kMin = 1;
  constexpr int64_t kMax = 6;
  constexpr int64_t kCount = 8;
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  auto handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Histogram(
          kMetricName, "A simple uint64 histogram.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  Init(std::move(
      Options()
          .set_metric_names({kMetricName})
          .set_server_selector([](const grpc_core::ChannelArgs& args) {
            return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                   GRPC_ARG_SERVER_SELECTOR_VALUE;
          })
          .add_optional_label(kOptionalLabelKeys[0])
          .add_optional_label(kOptionalLabelKeys[1])));
  grpc_core::ChannelArgs args;
  args = args.Set(GRPC_ARG_SERVER_SELECTOR_KEY, GRPC_ARG_SERVER_SELECTOR_VALUE);
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
  for (auto v : kHistogramValues) {
    stats_plugins->RecordHistogram(handle, v, kLabelValues,
                                   kOptionalLabelValues);
  }
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(
      data, ::testing::ElementsAre(::testing::Pair(
                kMetricName,
                ::testing::ElementsAre(::testing::AllOf(
                    AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                 kOptionalLabelValues),
                    HistogramResultEq(::testing::Eq(kSum), ::testing::Eq(kMin),
                                      ::testing::Eq(kMax), kCount))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, RecordDoubleHistogram) {
  constexpr absl::string_view kMetricName = "double_histogram";
  constexpr double kHistogramValues[] = {1.1, 1.2, 2.2, 3.3,
                                         4.4, 4.5, 5.5, 6.6};
  constexpr double kSum = 28.8;
  constexpr double kMin = 1.1;
  constexpr double kMax = 6.6;
  constexpr double kCount = 8;
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  auto handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
          kMetricName, "A simple double histogram.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  Init(std::move(
      Options()
          .set_metric_names({kMetricName})
          .set_server_selector([](const grpc_core::ChannelArgs& args) {
            return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                   GRPC_ARG_SERVER_SELECTOR_VALUE;
          })
          .add_optional_label(kOptionalLabelKeys[0])
          .add_optional_label(kOptionalLabelKeys[1])));
  grpc_core::ChannelArgs args;
  args = args.Set(GRPC_ARG_SERVER_SELECTOR_KEY, GRPC_ARG_SERVER_SELECTOR_VALUE);
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
  for (auto v : kHistogramValues) {
    stats_plugins->RecordHistogram(handle, v, kLabelValues,
                                   kOptionalLabelValues);
  }
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(data,
              ::testing::ElementsAre(::testing::Pair(
                  kMetricName,
                  ::testing::ElementsAre(::testing::AllOf(
                      AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                   kOptionalLabelValues),
                      HistogramResultEq(::testing::DoubleEq(kSum),
                                        ::testing::DoubleEq(kMin),
                                        ::testing::DoubleEq(kMax), kCount))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest,
       RegisterMultipleOpenTelemetryPlugins) {
  constexpr absl::string_view kMetricName = "yet_another_double_histogram";
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2"};
  auto handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
          kMetricName, "A simple double histogram.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  // Build and register a separate OpenTelemetryPlugin and verify its histogram
  // recording.
  auto reader = BuildAndRegisterOpenTelemetryPlugin(std::move(
      Options()
          .set_metric_names({kMetricName})
          .set_server_selector([](const grpc_core::ChannelArgs& args) {
            return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                   GRPC_ARG_SERVER_SELECTOR_VALUE;
          })
          .add_optional_label(kOptionalLabelKeys[0])
          .add_optional_label(kOptionalLabelKeys[1])));
  grpc_core::ChannelArgs args;
  args = args.Set(GRPC_ARG_SERVER_SELECTOR_KEY, GRPC_ARG_SERVER_SELECTOR_VALUE);
  {
    constexpr double kHistogramValues[] = {1.23, 2.34, 3.45, 4.56};
    constexpr double kSum = 11.58;
    constexpr double kMin = 1.23;
    constexpr double kMax = 4.56;
    constexpr int kCount = 4;
    auto stats_plugins =
        grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
    for (auto v : kHistogramValues) {
      stats_plugins->RecordHistogram(handle, v, kLabelValues,
                                     kOptionalLabelValues);
    }
    auto data = ReadCurrentMetricsData(
        [&](const absl::flat_hash_map<
            std::string,
            std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                data) { return !data.contains(kMetricName); },
        reader.get());
    EXPECT_THAT(
        data, ::testing::ElementsAre(::testing::Pair(
                  kMetricName,
                  ::testing::ElementsAre(::testing::AllOf(
                      AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                   kOptionalLabelValues),
                      HistogramResultEq(::testing::DoubleEq(kSum),
                                        ::testing::DoubleEq(kMin),
                                        ::testing::DoubleEq(kMax), kCount))))));
  }
  // Now build and register another OpenTelemetryPlugin using the test fixture
  // and record histogram.
  constexpr double kHistogramValues[] = {1.1, 1.2, 2.2, 3.3,
                                         4.4, 4.5, 5.5, 6.6};
  constexpr double kSum = 28.8;
  constexpr double kMin = 1.1;
  constexpr double kMax = 6.6;
  constexpr int kCount = 8;
  Init(std::move(
      Options()
          .set_metric_names({kMetricName})
          .set_server_selector([](const grpc_core::ChannelArgs& args) {
            return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                   GRPC_ARG_SERVER_SELECTOR_VALUE;
          })
          .add_optional_label(kOptionalLabelKeys[0])
          .add_optional_label(kOptionalLabelKeys[1])));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
  for (auto v : kHistogramValues) {
    stats_plugins->RecordHistogram(handle, v, kLabelValues,
                                   kOptionalLabelValues);
  }
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(data,
              ::testing::ElementsAre(::testing::Pair(
                  kMetricName,
                  ::testing::ElementsAre(::testing::AllOf(
                      AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                   kOptionalLabelValues),
                      HistogramResultEq(::testing::DoubleEq(kSum),
                                        ::testing::DoubleEq(kMin),
                                        ::testing::DoubleEq(kMax), kCount))))));
  // Verify that the first plugin gets the data as well.
  data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); },
      reader.get());
  EXPECT_THAT(data,
              ::testing::ElementsAre(::testing::Pair(
                  kMetricName,
                  ::testing::ElementsAre(::testing::AllOf(
                      AttributesEq(kLabelKeys, kLabelValues, kOptionalLabelKeys,
                                   kOptionalLabelValues),
                      HistogramResultEq(::testing::DoubleEq(kSum),
                                        ::testing::DoubleEq(kMin),
                                        ::testing::DoubleEq(kMax), kCount))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest,
       DisabledOptionalLabelKeysShouldNotBeRecorded) {
  constexpr absl::string_view kMetricName =
      "yet_another_yet_another_double_histogram";
  constexpr double kHistogramValues[] = {1.1, 1.2, 2.2, 3.3,
                                         4.4, 4.5, 5.5, 6.6};
  constexpr double kSum = 28.8;
  constexpr double kMin = 1.1;
  constexpr double kMax = 6.6;
  constexpr double kCount = 8;
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 4> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2", "optional_label_key_3",
      "optional_label_key_4"};
  constexpr std::array<absl::string_view, 3> kActualOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2", "optional_label_key_4"};
  constexpr std::array<absl::string_view, 2> kLabelValues = {"label_value_1",
                                                             "label_value_2"};
  constexpr std::array<absl::string_view, 4> kOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2",
      "optional_label_value_3", "optional_label_value_4"};
  constexpr std::array<absl::string_view, 3> kActualOptionalLabelValues = {
      "optional_label_value_1", "optional_label_value_2",
      "optional_label_value_4"};
  auto handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
          kMetricName, "A simple double histogram.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1],
                          kOptionalLabelKeys[2], kOptionalLabelKeys[3])
          .Build();
  Init(std::move(
      Options()
          .set_metric_names({kMetricName})
          .set_server_selector([](const grpc_core::ChannelArgs& args) {
            return args.GetString(GRPC_ARG_SERVER_SELECTOR_KEY) ==
                   GRPC_ARG_SERVER_SELECTOR_VALUE;
          })
          .add_optional_label(kOptionalLabelKeys[0])
          .add_optional_label(kOptionalLabelKeys[1])
          .add_optional_label(kOptionalLabelKeys[3])));
  grpc_core::ChannelArgs args;
  args = args.Set(GRPC_ARG_SERVER_SELECTOR_KEY, GRPC_ARG_SERVER_SELECTOR_VALUE);
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
  for (auto v : kHistogramValues) {
    stats_plugins->RecordHistogram(handle, v, kLabelValues,
                                   kOptionalLabelValues);
  }
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  EXPECT_THAT(
      data,
      ::testing::ElementsAre(::testing::Pair(
          kMetricName,
          ::testing::ElementsAre(::testing::AllOf(
              AttributesEq(kLabelKeys, kLabelValues, kActualOptionalLabelKeys,
                           kActualOptionalLabelValues),
              HistogramResultEq(::testing::DoubleEq(kSum),
                                ::testing::DoubleEq(kMin),
                                ::testing::DoubleEq(kMax), kCount))))));
}

TEST_F(OpenTelemetryPluginNPCMetricsTest, InstrumentsEnabledTest) {
  constexpr absl::string_view kDoubleHistogramMetricName =
      "yet_another_yet_another_double_histogram";
  constexpr absl::string_view kUnit64CounterMetricName = "uint64_counter";
  auto histogram_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterDoubleHistogram(
          kDoubleHistogramMetricName, "A simple double histogram.", "unit",
          /*enable_by_default=*/false)
          .Build();
  auto counter_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
          kUnit64CounterMetricName, "A simple unit64 counter.", "unit",
          /*enable_by_default=*/false)
          .Build();
  Init(std::move(Options().set_metric_names({kDoubleHistogramMetricName})));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForServer(
          grpc_core::ChannelArgs());
  EXPECT_TRUE(stats_plugins->IsInstrumentEnabled(histogram_handle));
  EXPECT_FALSE(stats_plugins->IsInstrumentEnabled(counter_handle));
}

using OpenTelemetryPluginCallbackMetricsTest =
    OpenTelemetryPluginNPCMetricsTest;

// The callback minimal interval is longer than the OT reporting interval, so we
// expect to collect duplicated (cached) values.
TEST_F(OpenTelemetryPluginCallbackMetricsTest,
       ReportDurationLongerThanCollectDuration) {
  constexpr absl::string_view kInt64CallbackGaugeMetric =
      "int64_callback_gauge";
  constexpr absl::string_view kDoubleCallbackGaugeMetric =
      "double_callback_gauge";
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValuesSet1 = {
      "label_value_set_1", "label_value_set_1"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValuesSet1 = {
      "optional_label_value_set_1", "optional_label_value_set_1"};
  constexpr std::array<absl::string_view, 2> kLabelValuesSet2 = {
      "label_value_set_2", "label_value_set_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValuesSet2 = {
      "optional_label_value_set_2", "optional_label_value_set_2"};
  auto integer_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackInt64Gauge(
          kInt64CallbackGaugeMetric, "An int64 callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  auto double_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackDoubleGauge(
          kDoubleCallbackGaugeMetric, "A double callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  Init(std::move(Options()
                     .set_metric_names({kInt64CallbackGaugeMetric,
                                        kDoubleCallbackGaugeMetric})
                     .add_optional_label(kOptionalLabelKeys[0])
                     .add_optional_label(kOptionalLabelKeys[1])));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope(
              "dns:///localhost:8080", "", endpoint_config_));
  // Multiple callbacks for the same metrics, each reporting different
  // label values.
  int report_count_1 = 0;
  int64_t int_value_1 = 1;
  double double_value_1 = 0.5;
  auto registered_metric_callback_1 = stats_plugins->RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_1;
        reporter.Report(integer_gauge_handle, int_value_1++, kLabelValuesSet1,
                        kOptionalLabelValuesSet1);
        reporter.Report(double_gauge_handle, double_value_1++, kLabelValuesSet1,
                        kOptionalLabelValuesSet1);
        ;
      },
      grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
      integer_gauge_handle, double_gauge_handle);
  int report_count_2 = 0;
  int64_t int_value_2 = 1;
  double double_value_2 = 0.5;
  auto registered_metric_callback_2 = stats_plugins->RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_2;
        reporter.Report(integer_gauge_handle, int_value_2++, kLabelValuesSet2,
                        kOptionalLabelValuesSet2);
        reporter.Report(double_gauge_handle, double_value_2++, kLabelValuesSet2,
                        kOptionalLabelValuesSet2);
      },
      grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
      integer_gauge_handle, double_gauge_handle);
  constexpr int kIterations = 100;
  MetricsCollectorThread collector{
      this, grpc_core::Duration::Milliseconds(10) * grpc_test_slowdown_factor(),
      kIterations,
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) {
        return !data.contains(kInt64CallbackGaugeMetric) ||
               !data.contains(kDoubleCallbackGaugeMetric);
      }};
  absl::flat_hash_map<
      std::string,
      std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
      data = collector.Stop();
  // Verify that data is incremental with duplications (cached values).
  EXPECT_LT(report_count_1, kIterations);
  EXPECT_LT(report_count_2, kIterations);
  EXPECT_EQ(data[kInt64CallbackGaugeMetric].size(),
            data[kDoubleCallbackGaugeMetric].size());
  // Verify labels.
  ASSERT_THAT(
      data,
      ::testing::UnorderedElementsAre(
          ::testing::Pair(
              kInt64CallbackGaugeMetric,
              ::testing::Each(::testing::AnyOf(
                  AttributesEq(kLabelKeys, kLabelValuesSet1, kOptionalLabelKeys,
                               kOptionalLabelValuesSet1),
                  AttributesEq(kLabelKeys, kLabelValuesSet2, kOptionalLabelKeys,
                               kOptionalLabelValuesSet2)))),
          ::testing::Pair(
              kDoubleCallbackGaugeMetric,
              ::testing::Each(::testing::AnyOf(
                  AttributesEq(kLabelKeys, kLabelValuesSet1, kOptionalLabelKeys,
                               kOptionalLabelValuesSet1),
                  AttributesEq(kLabelKeys, kLabelValuesSet2, kOptionalLabelKeys,
                               kOptionalLabelValuesSet2))))));
  EXPECT_THAT(data, GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                        kInt64CallbackGaugeMetric, kLabelKeys, kLabelValuesSet1,
                        kOptionalLabelKeys, kOptionalLabelValuesSet1,
                        int64_t(0), false));
  EXPECT_THAT(data, GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                        kInt64CallbackGaugeMetric, kLabelKeys, kLabelValuesSet2,
                        kOptionalLabelKeys, kOptionalLabelValuesSet2,
                        int64_t(0), false));
  EXPECT_THAT(data,
              GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                  kDoubleCallbackGaugeMetric, kLabelKeys, kLabelValuesSet1,
                  kOptionalLabelKeys, kOptionalLabelValuesSet1, 0.0, false));
  EXPECT_THAT(data,
              GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                  kDoubleCallbackGaugeMetric, kLabelKeys, kLabelValuesSet2,
                  kOptionalLabelKeys, kOptionalLabelValuesSet2, 0.0, false));
}

// The callback minimal interval is shorter than the OT reporting interval, so
// for each collect we should go update the cache and report the latest values.
TEST_F(OpenTelemetryPluginCallbackMetricsTest,
       ReportDurationShorterThanCollectDuration) {
  constexpr absl::string_view kInt64CallbackGaugeMetric =
      "yet_another_int64_callback_gauge";
  constexpr absl::string_view kDoubleCallbackGaugeMetric =
      "yet_another_double_callback_gauge";
  constexpr std::array<absl::string_view, 2> kLabelKeys = {"label_key_1",
                                                           "label_key_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelKeys = {
      "optional_label_key_1", "optional_label_key_2"};
  constexpr std::array<absl::string_view, 2> kLabelValuesSet1 = {
      "label_value_set_1", "label_value_set_1"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValuesSet1 = {
      "optional_label_value_set_1", "optional_label_value_set_1"};
  constexpr std::array<absl::string_view, 2> kLabelValuesSet2 = {
      "label_value_set_2", "label_value_set_2"};
  constexpr std::array<absl::string_view, 2> kOptionalLabelValuesSet2 = {
      "optional_label_value_set_2", "optional_label_value_set_2"};
  auto integer_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackInt64Gauge(
          kInt64CallbackGaugeMetric, "An int64 callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  auto double_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackDoubleGauge(
          kDoubleCallbackGaugeMetric, "A double callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Labels(kLabelKeys[0], kLabelKeys[1])
          .OptionalLabels(kOptionalLabelKeys[0], kOptionalLabelKeys[1])
          .Build();
  Init(std::move(Options()
                     .set_metric_names({kInt64CallbackGaugeMetric,
                                        kDoubleCallbackGaugeMetric})
                     .add_optional_label(kOptionalLabelKeys[0])
                     .add_optional_label(kOptionalLabelKeys[1])));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope(
              "dns:///localhost:8080", "", endpoint_config_));
  // Multiple callbacks for the same metrics, each reporting different
  // label values.
  int report_count_1 = 0;
  int64_t int_value_1 = 1;
  double double_value_1 = 0.5;
  auto registered_metric_callback_1 = stats_plugins->RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_1;
        reporter.Report(integer_gauge_handle, int_value_1++, kLabelValuesSet1,
                        kOptionalLabelValuesSet1);
        reporter.Report(double_gauge_handle, double_value_1++, kLabelValuesSet1,
                        kOptionalLabelValuesSet1);
      },
      grpc_core::Duration::Milliseconds(50) * grpc_test_slowdown_factor(),
      integer_gauge_handle, double_gauge_handle);
  int report_count_2 = 0;
  int64_t int_value_2 = 1;
  double double_value_2 = 0.5;
  auto registered_metric_callback_2 = stats_plugins->RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_2;
        reporter.Report(integer_gauge_handle, int_value_2++, kLabelValuesSet2,
                        kOptionalLabelValuesSet2);
        reporter.Report(double_gauge_handle, double_value_2++, kLabelValuesSet2,
                        kOptionalLabelValuesSet2);
      },
      grpc_core::Duration::Milliseconds(50) * grpc_test_slowdown_factor(),
      integer_gauge_handle, double_gauge_handle);
  constexpr int kIterations = 50;
  MetricsCollectorThread collector{
      this,
      grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
      kIterations,
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) {
        return !data.contains(kInt64CallbackGaugeMetric) ||
               !data.contains(kDoubleCallbackGaugeMetric);
      }};
  absl::flat_hash_map<
      std::string,
      std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>
      data = collector.Stop();
  // Verify that data is incremental without duplications (cached
  // values).
  EXPECT_EQ(report_count_1, kIterations);
  EXPECT_EQ(report_count_2, kIterations);
  EXPECT_EQ(data[kInt64CallbackGaugeMetric].size(),
            data[kDoubleCallbackGaugeMetric].size());
  // Verify labels.
  ASSERT_THAT(
      data,
      ::testing::UnorderedElementsAre(
          ::testing::Pair(
              kInt64CallbackGaugeMetric,
              ::testing::Each(::testing::AnyOf(
                  AttributesEq(kLabelKeys, kLabelValuesSet1, kOptionalLabelKeys,
                               kOptionalLabelValuesSet1),
                  AttributesEq(kLabelKeys, kLabelValuesSet2, kOptionalLabelKeys,
                               kOptionalLabelValuesSet2)))),
          ::testing::Pair(
              kDoubleCallbackGaugeMetric,
              ::testing::Each(::testing::AnyOf(
                  AttributesEq(kLabelKeys, kLabelValuesSet1, kOptionalLabelKeys,
                               kOptionalLabelValuesSet1),
                  AttributesEq(kLabelKeys, kLabelValuesSet2, kOptionalLabelKeys,
                               kOptionalLabelValuesSet2))))));
  EXPECT_THAT(data, GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                        kInt64CallbackGaugeMetric, kLabelKeys, kLabelValuesSet1,
                        kOptionalLabelKeys, kOptionalLabelValuesSet1,
                        int64_t(0), true));
  EXPECT_THAT(data, GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                        kInt64CallbackGaugeMetric, kLabelKeys, kLabelValuesSet2,
                        kOptionalLabelKeys, kOptionalLabelValuesSet2,
                        int64_t(0), true));
  EXPECT_THAT(data,
              GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                  kDoubleCallbackGaugeMetric, kLabelKeys, kLabelValuesSet1,
                  kOptionalLabelKeys, kOptionalLabelValuesSet1, 0.0, true));
  EXPECT_THAT(data,
              GaugeDataIsIncrementalForSpecificMetricAndLabelSet(
                  kDoubleCallbackGaugeMetric, kLabelKeys, kLabelValuesSet2,
                  kOptionalLabelKeys, kOptionalLabelValuesSet2, 0.0, true));
}

// Verifies that callbacks are cleaned up when the OpenTelemetry plugin is
// destroyed.
TEST_F(OpenTelemetryPluginCallbackMetricsTest, VerifyCallbacksAreCleanedUp) {
  constexpr absl::string_view kInt64CallbackGaugeMetric =
      "yet_another_int64_callback_gauge";
  constexpr absl::string_view kDoubleCallbackGaugeMetric =
      "yet_another_double_callback_gauge";
  auto integer_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackInt64Gauge(
          kInt64CallbackGaugeMetric, "An int64 callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Build();
  auto double_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackDoubleGauge(
          kDoubleCallbackGaugeMetric, "A double callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Build();
  Init(std::move(Options().set_metric_names(
      {kInt64CallbackGaugeMetric, kDoubleCallbackGaugeMetric})));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope(
              "dns:///localhost:8080", "", endpoint_config_));
  // Multiple callbacks for the same metrics, each reporting different
  // label values.
  int report_count_1 = 0;
  int64_t int_value_1 = 1;
  double double_value_1 = 0.5;
  auto registered_metric_callback_1 = stats_plugins->RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_1;
        reporter.Report(integer_gauge_handle, int_value_1++, {}, {});
        reporter.Report(double_gauge_handle, double_value_1++, {}, {});
      },
      grpc_core::Duration::Milliseconds(50) * grpc_test_slowdown_factor(),
      integer_gauge_handle, double_gauge_handle);
  int report_count_2 = 0;
  int64_t int_value_2 = 1;
  double double_value_2 = 0.5;
  auto registered_metric_callback_2 = stats_plugins->RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_2;
        reporter.Report(integer_gauge_handle, int_value_2++, {}, {});
        reporter.Report(double_gauge_handle, double_value_2++, {}, {});
      },
      grpc_core::Duration::Milliseconds(50) * grpc_test_slowdown_factor(),
      integer_gauge_handle, double_gauge_handle);
  constexpr int kIterations = 50;
  {
    MetricsCollectorThread collector{
        this,
        grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
        kIterations,
        [&](const absl::flat_hash_map<
            std::string,
            std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                data) {
          return !data.contains(kInt64CallbackGaugeMetric) ||
                 !data.contains(kDoubleCallbackGaugeMetric);
        }};
  }
  // Verify that callbacks are invoked
  EXPECT_EQ(report_count_1, kIterations);
  EXPECT_EQ(report_count_2, kIterations);
  // Remove one of the callbacks
  registered_metric_callback_1.reset();
  {
    MetricsCollectorThread new_collector{
        this,
        grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
        kIterations,
        [&](const absl::flat_hash_map<
            std::string,
            std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                data) { return false; }};
  }
  EXPECT_EQ(report_count_1, kIterations);      // No change since previous
  EXPECT_EQ(report_count_2, 2 * kIterations);  // Gets another kIterations
  // Remove the other callback as well
  registered_metric_callback_2.reset();
  MetricsCollectorThread new_new_collector{
      this,
      grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
      kIterations,
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return false; }};
  // We shouldn't get any new callbacks
  EXPECT_THAT(new_new_collector.Stop(), ::testing::IsEmpty());
  EXPECT_EQ(report_count_1, kIterations);
  EXPECT_EQ(report_count_2, 2 * kIterations);
  // Reset stats plugins as well
  grpc_core::GlobalStatsPluginRegistryTestPeer::
      ResetGlobalStatsPluginRegistry();
  registered_metric_callback_2.reset();
  MetricsCollectorThread new_new_new_collector{
      this,
      grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
      kIterations,
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return false; }};
  // Still no new callbacks
  EXPECT_THAT(new_new_new_collector.Stop(), ::testing::IsEmpty());
  EXPECT_EQ(report_count_1, kIterations);
  EXPECT_EQ(report_count_2, 2 * kIterations);
}

TEST_F(OpenTelemetryPluginCallbackMetricsTest,
       ReportDifferentGaugeThanRegisteredWontCrash) {
  constexpr absl::string_view kInt64CallbackGaugeMetric =
      "yet_another_int64_callback_gauge";
  constexpr absl::string_view kDoubleCallbackGaugeMetric =
      "yet_another_double_callback_gauge";
  auto integer_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackInt64Gauge(
          kInt64CallbackGaugeMetric, "An int64 callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Build();
  auto double_gauge_handle =
      grpc_core::GlobalInstrumentsRegistry::RegisterCallbackDoubleGauge(
          kDoubleCallbackGaugeMetric, "A double callback gauge.", "unit",
          /*enable_by_default=*/true)
          .Build();
  Init(std::move(Options().set_metric_names(
      {kInt64CallbackGaugeMetric, kDoubleCallbackGaugeMetric})));
  auto stats_plugins =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope(
              "dns:///localhost:8080", "", endpoint_config_));
  // Registers integer_gauge_handle but reports double_gauge_handle.
  int report_count_1 = 0;
  double double_value_1 = 0.5;
  auto registered_metric_callback_1 = stats_plugins->RegisterCallback(
      [&](grpc_core::CallbackMetricReporter& reporter) {
        ++report_count_1;
        reporter.Report(double_gauge_handle, double_value_1++, {}, {});
      },
      grpc_core::Duration::Milliseconds(50) * grpc_test_slowdown_factor(),
      integer_gauge_handle);
  constexpr int kIterations = 50;
  {
    MetricsCollectorThread collector{
        this,
        grpc_core::Duration::Milliseconds(100) * grpc_test_slowdown_factor(),
        kIterations,
        [&](const absl::flat_hash_map<
            std::string,
            std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
                data) { return false; }};
  }
  // Verify that callbacks are invoked
  EXPECT_EQ(report_count_1, kIterations);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
