/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
