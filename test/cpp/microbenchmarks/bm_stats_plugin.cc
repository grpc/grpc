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

#include <benchmark/benchmark.h>
#include <grpcpp/ext/otel_plugin.h>

#include <memory>

#include "absl/log/absl_check.h"
#include "absl/strings/string_view.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/telemetry/metrics.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

namespace {

constexpr const absl::string_view kMetricName = "test.counter";
constexpr const absl::string_view kMetricNameWithLabels =
    "test.counter_with_labels";

const auto kCounterHandle =
    grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
        kMetricName, "A simple test counter", "{count}", true)
        .Build();

const auto kCounterWithLabelsHandle =
    grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
        kMetricNameWithLabels, "A simple test counter with labels", "{count}",
        true)
        .Labels("key1", "key2", "key3")
        .OptionalLabels("opt_key1", "opt_key2", "opt_key3")
        .Build();

void BM_AddCounterWithFakeStatsPlugin(benchmark::State& state) {
  grpc_core::GlobalStatsPluginRegistryTestPeer::
      ResetGlobalStatsPluginRegistry();
  grpc_core::FakeStatsPluginBuilder().BuildAndRegister();
  grpc_event_engine::experimental::ChannelArgsEndpointConfig endpoint_config;
  auto stats_plugin_group =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope("", "",
                                                           endpoint_config));
  for (auto _ : state) {
    stats_plugin_group->AddCounter(kCounterHandle, 1, {}, {});
  }
}
BENCHMARK(BM_AddCounterWithFakeStatsPlugin);

void BM_AddCounterWithLabelsWithFakeStatsPlugin(benchmark::State& state) {
  grpc_core::GlobalStatsPluginRegistryTestPeer::
      ResetGlobalStatsPluginRegistry();
  grpc_core::FakeStatsPluginBuilder().BuildAndRegister();
  grpc_event_engine::experimental::ChannelArgsEndpointConfig endpoint_config;
  auto stats_plugin_group =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope("", "",
                                                           endpoint_config));
  for (auto _ : state) {
    stats_plugin_group->AddCounter(kCounterWithLabelsHandle, 1,
                                   {"val1", "val2", "val3"},
                                   {"opt_val1", "opt_val2", "opt_val3"});
  }
}
BENCHMARK(BM_AddCounterWithLabelsWithFakeStatsPlugin);

void BM_AddCounterWithOTelPlugin(benchmark::State& state) {
  grpc_core::GlobalStatsPluginRegistryTestPeer::
      ResetGlobalStatsPluginRegistry();
  auto meter_provider =
      std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
  auto status = grpc::OpenTelemetryPluginBuilder()
                    .EnableMetrics({kMetricName})
                    .SetMeterProvider(std::move(meter_provider))
                    .BuildAndRegisterGlobal();
  CHECK(status.ok());
  grpc_event_engine::experimental::ChannelArgsEndpointConfig endpoint_config;
  auto stats_plugin_group =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope("", "",
                                                           endpoint_config));
  for (auto _ : state) {
    stats_plugin_group->AddCounter(kCounterHandle, 1, {}, {});
  }
}
BENCHMARK(BM_AddCounterWithOTelPlugin);

void BM_AddCounterWithLabelsWithOTelPlugin(benchmark::State& state) {
  grpc_core::GlobalStatsPluginRegistryTestPeer::
      ResetGlobalStatsPluginRegistry();
  auto meter_provider =
      std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
  auto status = grpc::OpenTelemetryPluginBuilder()
                    .EnableMetrics({kMetricName})
                    .SetMeterProvider(std::move(meter_provider))
                    .BuildAndRegisterGlobal();
  CHECK(status.ok());
  grpc_event_engine::experimental::ChannelArgsEndpointConfig endpoint_config;
  auto stats_plugin_group =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope("", "",
                                                           endpoint_config));
  for (auto _ : state) {
    stats_plugin_group->AddCounter(kCounterWithLabelsHandle, 1,
                                   {"val1", "val2", "val3"},
                                   {"opt_val1", "opt_val2", "opt_val3"});
  }
}
BENCHMARK(BM_AddCounterWithLabelsWithOTelPlugin);

void BM_AddCounterWithNoPlugin(benchmark::State& state) {
  grpc_core::GlobalStatsPluginRegistryTestPeer::
      ResetGlobalStatsPluginRegistry();
  grpc_event_engine::experimental::ChannelArgsEndpointConfig endpoint_config;
  auto stats_plugin_group =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope("", "",
                                                           endpoint_config));
  for (auto _ : state) {
    stats_plugin_group->AddCounter(kCounterHandle, 1, {}, {});
  }
}
BENCHMARK(BM_AddCounterWithNoPlugin);

void BM_AddCounterWithLabelsWithNoPlugin(benchmark::State& state) {
  grpc_core::GlobalStatsPluginRegistryTestPeer::
      ResetGlobalStatsPluginRegistry();
  grpc_event_engine::experimental::ChannelArgsEndpointConfig endpoint_config;
  auto stats_plugin_group =
      grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
          grpc_core::experimental::StatsPluginChannelScope("", "",
                                                           endpoint_config));
  for (auto _ : state) {
    stats_plugin_group->AddCounter(kCounterWithLabelsHandle, 1,
                                   {"val1", "val2", "val3"},
                                   {"opt_val1", "opt_val2", "opt_val3"});
  }
}
BENCHMARK(BM_AddCounterWithLabelsWithNoPlugin);

}  // namespace

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
