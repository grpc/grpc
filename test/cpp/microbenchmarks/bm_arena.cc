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

#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

static void BM_Arena_NoOp(benchmark::State& state) {
  auto factory = grpc_core::SimpleArenaAllocator();
  for (auto _ : state) {
    factory->MakeArena();
  }
}
BENCHMARK(BM_Arena_NoOp)->Range(1, 1024 * 1024);

static void BM_Arena_ManyAlloc(benchmark::State& state) {
  auto allocator = grpc_core::SimpleArenaAllocator(state.range(0));
  auto a = allocator->MakeArena();
  const size_t realloc_after =
      1024 * 1024 * 1024 / ((state.range(1) + 15) & 0xffffff0u);
  while (state.KeepRunning()) {
    a->Alloc(state.range(1));
    // periodically recreate arena to avoid OOM
    if (state.iterations() % realloc_after == 0) {
      a = allocator->MakeArena();
    }
  }
}
BENCHMARK(BM_Arena_ManyAlloc)->Ranges({{1, 1024 * 1024}, {1, 32 * 1024}});

static void BM_Arena_Batch(benchmark::State& state) {
  auto allocator = grpc_core::SimpleArenaAllocator(state.range(0));
  for (auto _ : state) {
    auto a = allocator->MakeArena();
    for (int i = 0; i < state.range(1); i++) {
      a->Alloc(state.range(2));
    }
  }
}
BENCHMARK(BM_Arena_Batch)->Ranges({{1, 64 * 1024}, {1, 64}, {1, 1024}});

struct TestThingToAllocate {
  int a;
  int b;
  int c;
  int d;
};

static void BM_Arena_MakePooled_Small(benchmark::State& state) {
  auto a = grpc_core::SimpleArenaAllocator()->MakeArena();
  for (auto _ : state) {
    a->MakePooled<TestThingToAllocate>();
  }
}
BENCHMARK(BM_Arena_MakePooled_Small);

static void BM_Arena_MakePooled3_Small(benchmark::State& state) {
  auto a = grpc_core::SimpleArenaAllocator()->MakeArena();
  for (auto _ : state) {
    auto x = a->MakePooled<TestThingToAllocate>();
    auto y = a->MakePooled<TestThingToAllocate>();
    auto z = a->MakePooled<TestThingToAllocate>();
  }
}
BENCHMARK(BM_Arena_MakePooled3_Small);

static void BM_Arena_NewDeleteComparison_Small(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::make_unique<TestThingToAllocate>());
  }
}
BENCHMARK(BM_Arena_NewDeleteComparison_Small);

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::benchmark::Initialize(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
