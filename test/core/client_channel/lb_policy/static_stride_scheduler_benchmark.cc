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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "absl/algorithm/container.h"
#include "absl/random/random.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/load_balancing/weighted_round_robin/static_stride_scheduler.h"

namespace grpc_core {
namespace {

const int kNumWeightsLow = 10;
const int kNumWeightsHigh = 10000;
const int kRangeMultiplier = 10;

// Returns a randomly ordered list of weights equally distributed between 0.6
// and 1.0.
const std::vector<float>& Weights() {
  static const NoDestruct<std::vector<float>> kWeights([] {
    static NoDestruct<absl::BitGen> bit_gen;
    std::vector<float> weights;
    weights.reserve(kNumWeightsHigh);
    for (int i = 0; i < 40; ++i) {
      for (int j = 0; j < kNumWeightsHigh / 40; ++j) {
        weights.push_back(0.6 + (0.01 * i));
      }
    }
    absl::c_shuffle(weights, *bit_gen);
    return weights;
  }());
  return *kWeights;
}

void BM_StaticStrideSchedulerPickNonAtomic(benchmark::State& state) {
  uint32_t sequence = 0;
  const absl::optional<StaticStrideScheduler> scheduler =
      StaticStrideScheduler::Make(
          absl::MakeSpan(Weights()).subspan(0, state.range(0)),
          [&] { return sequence++; });
  GPR_ASSERT(scheduler.has_value());
  for (auto s : state) {
    benchmark::DoNotOptimize(scheduler->Pick());
  }
}
BENCHMARK(BM_StaticStrideSchedulerPickNonAtomic)
    ->RangeMultiplier(kRangeMultiplier)
    ->Range(kNumWeightsLow, kNumWeightsHigh);

void BM_StaticStrideSchedulerPickAtomic(benchmark::State& state) {
  std::atomic<uint32_t> sequence{0};
  const absl::optional<StaticStrideScheduler> scheduler =
      StaticStrideScheduler::Make(
          absl::MakeSpan(Weights()).subspan(0, state.range(0)),
          [&] { return sequence.fetch_add(1, std::memory_order_relaxed); });
  GPR_ASSERT(scheduler.has_value());
  for (auto s : state) {
    benchmark::DoNotOptimize(scheduler->Pick());
  }
}
BENCHMARK(BM_StaticStrideSchedulerPickAtomic)
    ->RangeMultiplier(kRangeMultiplier)
    ->Range(kNumWeightsLow, kNumWeightsHigh);

void BM_StaticStrideSchedulerMake(benchmark::State& state) {
  uint32_t sequence = 0;
  for (auto s : state) {
    const absl::optional<StaticStrideScheduler> scheduler =
        StaticStrideScheduler::Make(
            absl::MakeSpan(Weights()).subspan(0, state.range(0)),
            [&] { return sequence++; });
    GPR_ASSERT(scheduler.has_value());
  }
}
BENCHMARK(BM_StaticStrideSchedulerMake)
    ->RangeMultiplier(kRangeMultiplier)
    ->Range(kNumWeightsLow, kNumWeightsHigh);

}  // namespace
}  // namespace grpc_core

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
