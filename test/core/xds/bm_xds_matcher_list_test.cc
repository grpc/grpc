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
#include <vector>

#include "benchmark/benchmark.h"
#include "src/core/util/matchers.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "test/core/xds/bm_xds_matcher_common.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {
namespace {

void BM_XdsMatcherList(benchmark::State& state,
                       const TestMatchContext& context) {
  const int num_rules = state.range(0);
  std::vector<XdsMatcherList::FieldMatcher> matchers;
  for (int i = 0; i < num_rules; ++i) {
    matchers.emplace_back(
        XdsMatcherList::CreateSinglePredicate(
            std::make_unique<TestPathInput>(),
            std::make_unique<XdsMatcherList::StringInputMatcher>(
                StringMatcher::Create(StringMatcher::Type::kExact,
                                      absl::StrCat("/rule/", i))
                    .value())),
        XdsMatcher::OnMatch(std::make_unique<TestAction>("match"), false));
  }
  XdsMatcherList matcher_list(std::move(matchers), std::nullopt);

  for (auto _ : state) {
    XdsMatcher::Result result;
    bool found = matcher_list.FindMatches(context, result);
    benchmark::DoNotOptimize(found);
  }
  state.SetItemsProcessed(state.iterations());
}

void BM_XdsMatcherList_FirstMatch(benchmark::State& state) {
  // Scenario: Match the first rule (best case).
  TestMatchContext first_match_context("/rule/0");
  BM_XdsMatcherList(state, first_match_context);
}
BENCHMARK(BM_XdsMatcherList_FirstMatch)
    ->RangeMultiplier(kRangeMultiplier)
    ->Range(kSizeLow, kSizeHigh);

void BM_XdsMatcherList_LastMatch(benchmark::State& state) {
  // Scenario: Match the last rule (worst case).
  const int num_rules = state.range(0);
  const std::string path = absl::StrCat("/rule/", num_rules - 1);
  TestMatchContext last_match_context(path);
  BM_XdsMatcherList(state, last_match_context);
}
BENCHMARK(BM_XdsMatcherList_LastMatch)
    ->RangeMultiplier(kRangeMultiplier)
    ->Range(kSizeLow, kSizeHigh);

void BM_XdsMatcherList_NoMatch(benchmark::State& state) {
  // Scenario: No match.
  TestMatchContext no_match_context("/no_match");
  BM_XdsMatcherList(state, no_match_context);
}
BENCHMARK(BM_XdsMatcherList_NoMatch)
    ->RangeMultiplier(kRangeMultiplier)
    ->Range(kSizeLow, kSizeHigh);

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
