// Copyright 2026 gRPC authors.
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

// Measures the cost of recording the gRFC A92 ext_authz metrics, which are
// per-RPC counters recorded through the non-per-call metrics framework, on
// the same ExtAuthzFilter code path that RPCs use.  Every benchmark runs with
// 1 to 64 threads sharing one filter, as RPCs on one channel or server do.  The
// filters are intentionally leaked, since they live for the whole run.

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>
#include <grpc/support/metrics.h>

#include <memory>
#include <utility>

#include "src/core/client_channel/client_channel_args.h"
#include "src/core/filter/ext_authz/ext_authz_filter.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/test_config.h"
#include "absl/log/check.h"

namespace grpc_core {
namespace {

constexpr char kServerUri[] = "dns:///server.example.com";

std::unique_ptr<ExtAuthzFilter> CreateFilter(const ChannelArgs& args) {
  auto filter = ExtAuthzFilter::Create(
      args,
      ChannelFilter::Args(nullptr, MakeRefCounted<ExtAuthzFilter::Config>()));
  CHECK(filter.ok()) << filter.status();
  return std::move(*filter);
}

// A client-side filter without a stats plugin for its channel, so recording a
// metric does nothing.
const ExtAuthzFilter& FilterWithoutStatsPlugin() {
  static const ExtAuthzFilter* filter =
      CreateFilter(ChannelArgs().Set(GRPC_ARG_SERVER_URI, kServerUri))
          .release();
  return *filter;
}

// A client-side filter whose channel has a stats plugin, so the metrics are
// recorded with the "grpc.target" label.
const ExtAuthzFilter& ClientFilter() {
  static const ExtAuthzFilter* filter = []() {
    ChannelArgs args = ChannelArgs().Set(GRPC_ARG_SERVER_URI, kServerUri);
    auto stats_plugin_group =
        GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
            experimental::StatsPluginChannelScope(
                kServerUri, /*default_authority=*/"",
                grpc_event_engine::experimental::ChannelArgsEndpointConfig(
                    args)));
    return CreateFilter(args.SetObject(std::move(stats_plugin_group)))
        .release();
  }();
  return *filter;
}

// A server-side filter whose server has a stats plugin, so the metrics are
// recorded without labels.
const ExtAuthzFilter& ServerFilter() {
  static const ExtAuthzFilter* filter = []() {
    ChannelArgs args = ChannelArgs().Set(GRPC_ARG_IS_SERVER_FILTER_STACK, true);
    auto stats_plugin_group =
        GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
    return CreateFilter(args.SetObject(std::move(stats_plugin_group)))
        .release();
  }();
  return *filter;
}

// An allowed RPC records one counter.
void BM_ExtAuthzAllowedRpcWithoutStatsPlugin(benchmark::State& state) {
  const ExtAuthzFilter& filter = FilterWithoutStatsPlugin();
  for (auto _ : state) filter.RecordAllowedRpc();
}
BENCHMARK(BM_ExtAuthzAllowedRpcWithoutStatsPlugin)->ThreadRange(1, 64);

void BM_ExtAuthzAllowedRpcClient(benchmark::State& state) {
  const ExtAuthzFilter& filter = ClientFilter();
  for (auto _ : state) filter.RecordAllowedRpc();
}
BENCHMARK(BM_ExtAuthzAllowedRpcClient)->ThreadRange(1, 64);

void BM_ExtAuthzAllowedRpcServer(benchmark::State& state) {
  const ExtAuthzFilter& filter = ServerFilter();
  for (auto _ : state) filter.RecordAllowedRpc();
}
BENCHMARK(BM_ExtAuthzAllowedRpcServer)->ThreadRange(1, 64);

// The most an RPC records is two counters, e.g. failed and denied when the
// ext_authz call fails without failure_mode_allow.
void BM_ExtAuthzFailedRpcClient(benchmark::State& state) {
  const ExtAuthzFilter& filter = ClientFilter();
  for (auto _ : state) {
    filter.RecordFailedRpc();
    filter.RecordDeniedRpc();
  }
}
BENCHMARK(BM_ExtAuthzFailedRpcClient)->ThreadRange(1, 64);

}  // namespace
}  // namespace grpc_core

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::benchmark::Initialize(&argc, argv);
  grpc_init();
  // The ext_authz metrics are disabled by default.
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  benchmark::RunTheBenchmarksNamespaced();
  grpc_shutdown();
  return 0;
}
