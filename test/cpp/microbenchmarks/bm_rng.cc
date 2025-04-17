//
//
// Copyright 2017 gRPC authors.
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

// Benchmark arenas

#include <benchmark/benchmark.h>

#include "absl/random/random.h"
#include "src/core/util/sync.h"

static void BM_OneRngFromFreshBitSet(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(absl::Uniform(absl::BitGen(), 0.0, 1.0));
  }
}
BENCHMARK(BM_OneRngFromFreshBitSet);

static void BM_OneRngFromReusedBitSet(benchmark::State& state) {
  absl::BitGen bitgen;
  for (auto _ : state) {
    benchmark::DoNotOptimize(absl::Uniform(bitgen, 0.0, 1.0));
  }
}
BENCHMARK(BM_OneRngFromReusedBitSet);

static void BM_OneRngFromReusedBitSetWithMutex(benchmark::State& state) {
  struct Data {
    grpc_core::Mutex mu;
    absl::BitGen bitgen ABSL_GUARDED_BY(mu);
  };
  Data data;
  for (auto _ : state) {
    grpc_core::MutexLock lock(&data.mu);
    benchmark::DoNotOptimize(absl::Uniform(data.bitgen, 0.0, 1.0));
  }
}
BENCHMARK(BM_OneRngFromReusedBitSetWithMutex);

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
