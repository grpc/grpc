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

#include <benchmark/benchmark.h>

#include "src/core/telemetry/histogram.h"
#include "absl/random/random.h"

namespace grpc_core {
namespace {

void BM_BucketForExponentialHistogram(benchmark::State& state) {
  constexpr int64_t kMax = 1000000;
  ExponentialHistogramShape shape(kMax, state.range(0));
  std::vector<int64_t> values;
  values.reserve(kMax);
  auto gen = absl::BitGen();
  for (int64_t i = 0; i < kMax; ++i) {
    values.push_back(absl::Uniform<int64_t>(gen, 0, kMax));
  }
  int64_t i = 0;
  for (auto _ : state) {
    const int64_t n = values[i % kMax];
    benchmark::DoNotOptimize(shape.BucketFor(n));
    ++i;
  }
}
BENCHMARK(BM_BucketForExponentialHistogram)->Range(2, 32768);

void BM_BucketForLinearHistogram(benchmark::State& state) {
  const int64_t kMax = state.range(0);
  LinearHistogramShape shape(0, kMax);
  std::vector<int64_t> values;
  values.reserve(kMax);
  auto gen = absl::BitGen();
  for (int64_t i = 0; i < kMax; ++i) {
    values.push_back(absl::Uniform<int64_t>(gen, 0, kMax));
  }
  int64_t i = 0;
  for (auto _ : state) {
    const int64_t n = values[i % kMax];
    benchmark::DoNotOptimize(shape.BucketFor(n));
    ++i;
  }
}
BENCHMARK(BM_BucketForLinearHistogram)->Range(2, 32768);

}  // namespace
}  // namespace grpc_core

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
