// Copyright 2024 gRPC authors.
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

#include <random>

#include "src/core/util/tdigest.h"

namespace grpc_core {

static void BM_AddWithCompression(benchmark::State& state) {
  // kNumValues is 512 with a 4k page.
  const size_t kNumValues = sysconf(_SC_PAGE_SIZE) / sizeof(double);
  std::vector<double> vals;
  vals.reserve(kNumValues);
  std::mt19937 gen(1234);
  std::exponential_distribution<double> exp_dist;

  for (int idx = 0; idx < kNumValues; idx++) {
    vals.push_back(exp_dist(gen));
  }

  TDigest tdigest(/*compression=*/state.range(0));

  while (state.KeepRunningBatch(kNumValues)) {
    for (double val : vals) {
      tdigest.Add(val);
    }
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddWithCompression)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

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
