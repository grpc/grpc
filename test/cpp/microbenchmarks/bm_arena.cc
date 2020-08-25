/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* Benchmark arenas */

#include <benchmark/benchmark.h>
#include "src/core/lib/gprpp/arena.h"
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

using grpc_core::Arena;

static void BM_Arena_NoOp(benchmark::State& state) {
  for (auto _ : state) {
    Arena::Create(state.range(0))->Destroy();
  }
}
BENCHMARK(BM_Arena_NoOp)->Range(1, 1024 * 1024);

static void BM_Arena_ManyAlloc(benchmark::State& state) {
  Arena* a = Arena::Create(state.range(0));
  const size_t realloc_after =
      1024 * 1024 * 1024 / ((state.range(1) + 15) & 0xffffff0u);
  while (state.KeepRunning()) {
    a->Alloc(state.range(1));
    // periodically recreate arena to avoid OOM
    if (state.iterations() % realloc_after == 0) {
      a->Destroy();
      a = Arena::Create(state.range(0));
    }
  }
  a->Destroy();
}
BENCHMARK(BM_Arena_ManyAlloc)->Ranges({{1, 1024 * 1024}, {1, 32 * 1024}});

static void BM_Arena_Batch(benchmark::State& state) {
  for (auto _ : state) {
    Arena* a = Arena::Create(state.range(0));
    for (int i = 0; i < state.range(1); i++) {
      a->Alloc(state.range(2));
    }
    a->Destroy();
  }
}
BENCHMARK(BM_Arena_Batch)->Ranges({{1, 64 * 1024}, {1, 64}, {1, 1024}});

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
