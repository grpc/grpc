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

/* Test out pollset latencies */

#include <grpc/grpc.h>

extern "C" {
#include "src/core/lib/iomgr/pollset.h"
}

#include "test/cpp/microbenchmarks/helpers.h"
#include "third_party/benchmark/include/benchmark/benchmark.h"

static void shutdown_ps_closure(grpc_exec_ctx* exec_ctx, void* ps,
                                grpc_error* error) {
  grpc_pollset_destroy(ps);
}

static void BM_CreateDestroyPollset(benchmark::State& state) {
  TrackCounters track_counters;
  size_t ps_sz = grpc_pollset_size();
  grpc_pollset* ps = gpr_malloc(ps_sz);
  gpr_mu* mu;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_closure shutdown_ps_closure;
  grpc_closure_init(&shutdown_ps_closure, shutdown_ps, ps,
                    grpc_schedule_on_exec_ctx);
  while (state.KeepRunning()) {
    memset(ps, 0, ps_sz);
    grpc_pollset_init(ps, &mu);
    grpc_pollset_shutdown(&exec_ctx, ps, shutdown_ps_closure);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(&state);
}
BENCHMARK(BM_CreateDestroyPollset);

BENCHMARK_MAIN();
