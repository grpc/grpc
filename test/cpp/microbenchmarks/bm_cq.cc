/*
 *
 * Copyright 2015, Google Inc.
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

/* This benchmark exists to ensure that the benchmark integration is
 * working */

#include <benchmark/benchmark.h>
#include <grpc++/completion_queue.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc/grpc.h>
#include "test/cpp/microbenchmarks/helpers.h"

extern "C" {
#include "src/core/lib/surface/completion_queue.h"
}

namespace grpc {
namespace testing {

auto& force_library_initialization = Library::get();

static void BM_CreateDestroyCpp(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    CompletionQueue cq;
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_CreateDestroyCpp);

/* Create cq using a different constructor */
static void BM_CreateDestroyCpp2(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    grpc_completion_queue* core_cq = grpc_completion_queue_create(NULL);
    CompletionQueue cq(core_cq);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_CreateDestroyCpp2);

static void BM_CreateDestroyCore(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    grpc_completion_queue_destroy(grpc_completion_queue_create(NULL));
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_CreateDestroyCore);

static void DoneWithCompletionOnStack(grpc_exec_ctx* exec_ctx, void* arg,
                                      grpc_cq_completion* completion) {}

class DummyTag final : public CompletionQueueTag {
 public:
  bool FinalizeResult(void** tag, bool* status) override { return true; }
};

static void BM_Pass1Cpp(benchmark::State& state) {
  TrackCounters track_counters;
  CompletionQueue cq;
  grpc_completion_queue* c_cq = cq.cq();
  while (state.KeepRunning()) {
    grpc_cq_completion completion;
    DummyTag dummy_tag;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_cq_begin_op(c_cq, &dummy_tag);
    grpc_cq_end_op(&exec_ctx, c_cq, &dummy_tag, GRPC_ERROR_NONE,
                   DoneWithCompletionOnStack, NULL, &completion);
    grpc_exec_ctx_finish(&exec_ctx);
    void* tag;
    bool ok;
    cq.Next(&tag, &ok);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_Pass1Cpp);

static void BM_Pass1Core(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_completion_queue* cq = grpc_completion_queue_create(NULL);
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  while (state.KeepRunning()) {
    grpc_cq_completion completion;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_cq_begin_op(cq, NULL);
    grpc_cq_end_op(&exec_ctx, cq, NULL, GRPC_ERROR_NONE,
                   DoneWithCompletionOnStack, NULL, &completion);
    grpc_exec_ctx_finish(&exec_ctx);
    grpc_completion_queue_next(cq, deadline, NULL);
  }
  grpc_completion_queue_destroy(cq);
  track_counters.Finish(state);
}
BENCHMARK(BM_Pass1Core);

static void BM_Pluck1Core(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_completion_queue* cq = grpc_completion_queue_create(NULL);
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  while (state.KeepRunning()) {
    grpc_cq_completion completion;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_cq_begin_op(cq, NULL);
    grpc_cq_end_op(&exec_ctx, cq, NULL, GRPC_ERROR_NONE,
                   DoneWithCompletionOnStack, NULL, &completion);
    grpc_exec_ctx_finish(&exec_ctx);
    grpc_completion_queue_pluck(cq, NULL, deadline, NULL);
  }
  grpc_completion_queue_destroy(cq);
  track_counters.Finish(state);
}
BENCHMARK(BM_Pluck1Core);

static void BM_EmptyCore(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_completion_queue* cq = grpc_completion_queue_create(NULL);
  gpr_timespec deadline = gpr_inf_past(GPR_CLOCK_MONOTONIC);
  while (state.KeepRunning()) {
    grpc_completion_queue_next(cq, deadline, NULL);
  }
  grpc_completion_queue_destroy(cq);
  track_counters.Finish(state);
}
BENCHMARK(BM_EmptyCore);

}  // namespace testing
}  // namespace grpc

BENCHMARK_MAIN();
