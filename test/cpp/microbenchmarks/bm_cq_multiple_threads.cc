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

#include <benchmark/benchmark.h>
#include <string.h>
#include <atomic>

#include <grpc/grpc.h>
#include "test/cpp/microbenchmarks/helpers.h"

extern "C" {
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/surface/completion_queue.h"
}

struct grpc_pollset {
  gpr_mu mu;
};

namespace grpc {
namespace testing {

static void* make_tag(int i) { return (void*)(intptr_t)i; }
static grpc_completion_queue* g_cq;
static grpc_event_engine_vtable g_vtable;

static __thread int g_thread_idx;
static __thread grpc_cq_completion g_cq_completion;

static void pollset_shutdown(grpc_exec_ctx* exec_ctx, grpc_pollset* ps,
                             grpc_closure* closure) {
  grpc_closure_sched(exec_ctx, closure, GRPC_ERROR_NONE);
}

static void pollset_init(grpc_pollset* ps, gpr_mu** mu) {
  gpr_mu_init(&ps->mu);
  *mu = &ps->mu;
}

static void pollset_destroy(grpc_pollset* ps) { gpr_mu_destroy(&ps->mu); }

static grpc_error* pollset_kick(grpc_pollset* p, grpc_pollset_worker* worker) {
  return GRPC_ERROR_NONE;
}

/* Callback when the tag is dequeued from the completion queue. Does nothing */
static void cq_done_cb(grpc_exec_ctx* exec_ctx, void* done_arg,
                       grpc_cq_completion* cq_completion) {}

/* Queues a completion tag. ZERO polling overhead */
static grpc_error* pollset_work(grpc_exec_ctx* exec_ctx, grpc_pollset* ps,
                                grpc_pollset_worker** worker, gpr_timespec now,
                                gpr_timespec deadline) {
  gpr_mu_unlock(&ps->mu);
  grpc_cq_end_op(exec_ctx, g_cq, make_tag(g_thread_idx), GRPC_ERROR_NONE,
                 cq_done_cb, NULL, &g_cq_completion);
  grpc_exec_ctx_flush(exec_ctx);
  gpr_mu_lock(&ps->mu);
  return GRPC_ERROR_NONE;
}

static void init_engine_vtable() {
  memset(&g_vtable, 0, sizeof(g_vtable));

  g_vtable.pollset_size = sizeof(grpc_pollset);
  g_vtable.pollset_init = pollset_init;
  g_vtable.pollset_shutdown = pollset_shutdown;
  g_vtable.pollset_destroy = pollset_destroy;
  g_vtable.pollset_work = pollset_work;
  g_vtable.pollset_kick = pollset_kick;
}

static void setup() {
  grpc_init();
  init_engine_vtable();
  grpc_set_event_engine_test_only(&g_vtable);

  g_cq = grpc_completion_queue_create(NULL);
}

static void BM_Cq_Throughput(benchmark::State& state) {
  TrackCounters track_counters;
  gpr_timespec deadline = gpr_inf_past(GPR_CLOCK_MONOTONIC);

  if (state.thread_index == 0) {
    setup();
  }

  while (state.KeepRunning()) {
    g_thread_idx = state.thread_index;
    void* dummy_tag = make_tag(g_thread_idx);
    grpc_cq_begin_op(g_cq, dummy_tag);
    grpc_completion_queue_next(g_cq, deadline, NULL);
  }

  if (state.thread_index == 0) {
    grpc_completion_queue_shutdown(g_cq);
    grpc_completion_queue_destroy(g_cq);
  }

  track_counters.Finish(state);
}

BENCHMARK(BM_Cq_Throughput)->Threads(1)->UseRealTime();
BENCHMARK(BM_Cq_Throughput)->Threads(2)->UseRealTime();
BENCHMARK(BM_Cq_Throughput)->Threads(4)->UseRealTime();
BENCHMARK(BM_Cq_Throughput)->Threads(8)->UseRealTime();
BENCHMARK(BM_Cq_Throughput)->Threads(16)->UseRealTime();

}  // namespace testing
}  // namespace grpc

BENCHMARK_MAIN();
