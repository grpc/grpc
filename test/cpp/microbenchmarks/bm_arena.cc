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

extern "C" {
#include "src/core/lib/support/arena.h"
}
#include "test/cpp/microbenchmarks/helpers.h"
#include "third_party/benchmark/include/benchmark/benchmark.h"

static void BM_Arena_NoOp(benchmark::State& state) {
  while (state.KeepRunning()) {
    gpr_arena_destroy(gpr_arena_create(state.range(0)));
  }
}
BENCHMARK(BM_Arena_NoOp)->Range(1, 1024 * 1024);

static void BM_Arena_ManyAlloc(benchmark::State& state) {
  gpr_arena* a = gpr_arena_create(state.range(0));
  const size_t realloc_after =
      1024 * 1024 * 1024 / ((state.range(1) + 15) & 0xffffff0u);
  while (state.KeepRunning()) {
    gpr_arena_alloc(a, state.range(1));
    // periodically recreate arena to avoid OOM
    if (state.iterations() % realloc_after == 0) {
      gpr_arena_destroy(a);
      a = gpr_arena_create(state.range(0));
    }
  }
  gpr_arena_destroy(a);
}
BENCHMARK(BM_Arena_ManyAlloc)->Ranges({{1, 1024 * 1024}, {1, 32 * 1024}});

static void BM_Arena_Batch(benchmark::State& state) {
  while (state.KeepRunning()) {
    gpr_arena* a = gpr_arena_create(state.range(0));
    for (int i = 0; i < state.range(1); i++) {
      gpr_arena_alloc(a, state.range(2));
    }
    gpr_arena_destroy(a);
  }
}
BENCHMARK(BM_Arena_Batch)->Ranges({{1, 64 * 1024}, {1, 64}, {1, 1024}});

BENCHMARK_MAIN();
