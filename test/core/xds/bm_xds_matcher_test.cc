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

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "src/core/util/matchers.h"
#include "src/core/xds/grpc/xds_matcher.h"


namespace grpc_core {
namespace {

const int kSizeLow = 1;
const int kSizeHigh = 512;
const int kRangeMultiplier = 4;

// A concrete implementation of MatchContext for testing purposes.
class TestMatchContext : public XdsMatcher::MatchContext {
 public:
  explicit TestMatchContext(absl::string_view path) : path_(path) {}

  UniqueTypeName type() const override {
    return GRPC_UNIQUE_TYPE_NAME_HERE("TestMatchContext");
  }

  absl::string_view path() const { return path_; }

 private:
  absl::string_view path_;
};

// A concrete implementation of InputValue for testing.
class TestPathInput : public XdsMatcher::InputValue<absl::string_view> {
 public:
  UniqueTypeName context_type() const override {
    return GRPC_UNIQUE_TYPE_NAME_HERE("TestMatchContext");
  }

  std::optional<absl::string_view> GetValue(
      const XdsMatcher::MatchContext& context) const override {
    const auto* test_context = static_cast<const TestMatchContext*>(&context);
    return test_context->path();
  }
};

// A concrete implementation of Action for testing.
class TestAction : public XdsMatcher::Action {
 public:
  explicit TestAction(absl::string_view name) : name_(name) {}
  absl::string_view type_url() const override { return "test.TestAction"; }
  absl::string_view name() const { return name_; }

 private:
  std::string name_;
};

// =================================================================
// XdsMatcherList Benchmarks
// =================================================================

static void BM_XdsMatcherList(benchmark::State& state,
                              TestMatchContext context) {
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
        std::make_unique<XdsMatcher::OnMatch>(
            std::make_unique<TestAction>("match"), false));
  }
  XdsMatcherList matcher_list(std::move(matchers), nullptr);

  for (auto _ : state) {
    XdsMatcher::Result result;
    bool found = matcher_list.FindMatches(context, result);
    benchmark::DoNotOptimize(found);
  }
  state.SetItemsProcessed(state.iterations());
}

static void BM_XdsMatcherList_FirstMatch(benchmark::State& state) {
  // Scenario: Match the first rule (best case).
  TestMatchContext first_match_context("/rule/0");
  BM_XdsMatcherList(state, first_match_context);
}
BENCHMARK(BM_XdsMatcherList_FirstMatch)
    ->RangeMultiplier(kRangeMultiplier)
    ->Range(kSizeLow, kSizeHigh);

static void BM_XdsMatcherList_LastMatch(benchmark::State& state) {
  // Scenario: Match the last rule (worst case).
  const int num_rules = state.range(0);
  TestMatchContext last_match_context(absl::StrCat("/rule/", num_rules - 1));
  BM_XdsMatcherList(state, last_match_context);
}
BENCHMARK(BM_XdsMatcherList_LastMatch)
    ->RangeMultiplier(kRangeMultiplier)
    ->Range(kSizeLow, kSizeHigh);

static void BM_XdsMatcherList_NoMatch(benchmark::State& state) {
  // Scenario: No match.
  TestMatchContext no_match_context("/no_match");
  BM_XdsMatcherList(state, no_match_context);
}
BENCHMARK(BM_XdsMatcherList_NoMatch)
    ->RangeMultiplier(kRangeMultiplier)
    ->Range(kSizeLow, kSizeHigh);

// =================================================================
// XdsMatcherExactMap Benchmark
// =================================================================

static void BM_XdsMatcherExactMap(benchmark::State& state) {
  // Argument 0: The number of items in the map.
  const int map_size = state.range(0);
  // Argument 1: The scenario type (0 for Match, 1 for No-Match).
  const int scenario_type = state.range(1);
  absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>> map;
  for (int i = 0; i < map_size; ++i) {
    map[absl::StrCat("/exact/", i)] = std::make_unique<XdsMatcher::OnMatch>(
        std::make_unique<TestAction>("match"), false);
  }
  XdsMatcherExactMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                             nullptr);
  // Use state.range(1) to select the context for the benchmark run.
  std::unique_ptr<TestMatchContext> context;
  if (scenario_type == 0) {
    state.SetLabel("Match");  // Label for a successful lookup (hit)
    context = std::make_unique<TestMatchContext>(
        absl::StrCat("/exact/", map_size / 2));
  } else {
    state.SetLabel("NoMatch");  // Label for a failed lookup (miss)
    context = std::make_unique<TestMatchContext>("/no_match");
  }
  // The core benchmark loop runs with the selected context.
  for (auto _ : state) {
    XdsMatcher::Result result;
    bool found = matcher.FindMatches(*context, result);
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

// =================================================================
// XdsMatcherPrefixMap Benchmarks
// =================================================================

static void BM_XdsMatcherPrefixMap(benchmark::State& state) {
  // Argument 0: The number of prefixes in the map.
  const int map_size = state.range(0);
  // Argument 1: The scenario type (0 for Match, 1 for No-Match).
  const int scenario_type = state.range(1);
  absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>> map;
  for (int i = 0; i < map_size; ++i) {
    map[absl::StrCat("/prefix/", i, "/")] =
        std::make_unique<XdsMatcher::OnMatch>(
            std::make_unique<TestAction>("match"), false);
  }
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              nullptr);
  std::unique_ptr<TestMatchContext> context;
  if (scenario_type == 0) {
    state.SetLabel("Match");  // Set a descriptive label for the output
    context = std::make_unique<TestMatchContext>(
        absl::StrCat("/prefix/", map_size / 2, "/subpath/resource"));
  } else {
    state.SetLabel("NoMatch");  // Set a descriptive label for the output
    context = std::make_unique<TestMatchContext>("/nonexistent/path");
  }
  for (auto _ : state) {
    XdsMatcher::Result result;
    bool found = matcher.FindMatches(*context, result);
    benchmark::DoNotOptimize(found);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_XdsMatcherPrefixMap)
    ->RangeMultiplier(kRangeMultiplier)
    ->Ranges({{kSizeLow, kSizeHigh}, {0, 1}});

}  // namespace
}  // namespace grpc_core

namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

// The main function that runs the benchmarks
int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}