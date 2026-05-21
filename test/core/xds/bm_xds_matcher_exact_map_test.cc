//
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
//

#include <memory>
#include <optional>
#include <string>

#include "benchmark/benchmark.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "test/core/xds/bm_xds_matcher_common.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {
namespace {

void BM_XdsMatcherExactMap(benchmark::State& state) {
  // Argument 0: The number of items in the map.
  const int map_size = state.range(0);
  // Argument 1: The scenario type (0 for Match, 1 for No-Match).
  const int scenario_type = state.range(1);
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  for (int i = 0; i < map_size; ++i) {
    map.emplace(
        absl::StrCat("/exact/", i),
        XdsMatcher::OnMatch(std::make_unique<TestAction>("match"), false));
  }
  XdsMatcherExactMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                             std::nullopt);
  // Use state.range(1) to select the context for the benchmark run.
  std::string path;
  if (scenario_type == 0) {
    state.SetLabel("Match");  // Label for a successful lookup (hit)
    path = absl::StrCat("/exact/", map_size / 2);
  } else {
    state.SetLabel("NoMatch");  // Label for a failed lookup (miss)
    path = "/no_match";
  }
  TestMatchContext context(path);
  // The core benchmark loop runs with the selected context.
  for (auto _ : state) {
    XdsMatcher::Result result;
    bool found = matcher.FindMatches(context, result);
    benchmark::DoNotOptimize(found);
  }
  state.SetItemsProcessed(state.iterations());
}
// Register the benchmark with two ranges:
// 1. The map size
// 2. The scenario type, 0 (Match) and 1 (NoMatch).
BENCHMARK(BM_XdsMatcherExactMap)
    ->RangeMultiplier(kRangeMultiplier)
    ->Ranges({{kSizeLow, kSizeHigh}, {0, 1}});

}  // namespace
}  // namespace grpc_core

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

// The main function that runs the benchmarks
int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
