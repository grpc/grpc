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
#include <thread>

#include "src/core/util/latent_see.h"

namespace grpc_core {

static void BM_EmptyDisabledScoped(benchmark::State& state) {
  for (auto _ : state) {
    GRPC_LATENT_SEE_ALWAYS_ON_SCOPE("EmptyScoped");
  }
}
BENCHMARK(BM_EmptyDisabledScoped);

static void BM_EmptyEnabledScoped(benchmark::State& state) {
  Notification n;
  std::thread collector([&n]() {
    latent_see::DiscardOutput output;
    latent_see::Collect(&n, absl::Hours(24), 1024 * 1024 * 1024, &output);
  });
  for (auto _ : state) {
    GRPC_LATENT_SEE_ALWAYS_ON_SCOPE("EmptyScoped");
  }
  n.Notify();
  collector.join();
}
BENCHMARK(BM_EmptyEnabledScoped)->MinWarmUpTime(0.5);

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
