/*
 *
 * Copyright 2015 gRPC authors.
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

/* This benchmark exists to ensure that the benchmark integration is
 * working */

#include <benchmark/benchmark.h>
#include <grpc++/completion_queue.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include "test/cpp/microbenchmarks/helpers.h"

#include "src/core/lib/surface/completion_queue.h"

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
    grpc_completion_queue* core_cq =
        grpc_completion_queue_create_for_next(nullptr);
    CompletionQueue cq(core_cq);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_CreateDestroyCpp2);

static void BM_CreateDestroyCore(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    // TODO: sreek Templatize this benchmark and pass completion type and
    // polling type as parameters
    grpc_completion_queue_destroy(
        grpc_completion_queue_create_for_next(nullptr));
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_CreateDestroyCore);

static void DoneWithCompletionOnStack(grpc_exec_ctx* exec_ctx, void* arg,
                                      grpc_cq_completion* completion) {}

class DummyTag final : public internal::CompletionQueueTag {
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
    GPR_ASSERT(grpc_cq_begin_op(c_cq, &dummy_tag));
    grpc_cq_end_op(&exec_ctx, c_cq, &dummy_tag, GRPC_ERROR_NONE,
                   DoneWithCompletionOnStack, nullptr, &completion);
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
  // TODO: sreek Templatize this benchmark and pass polling_type as a param
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  while (state.KeepRunning()) {
    grpc_cq_completion completion;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    GPR_ASSERT(grpc_cq_begin_op(cq, nullptr));
    grpc_cq_end_op(&exec_ctx, cq, nullptr, GRPC_ERROR_NONE,
                   DoneWithCompletionOnStack, nullptr, &completion);
    grpc_exec_ctx_finish(&exec_ctx);
    grpc_completion_queue_next(cq, deadline, nullptr);
  }
  grpc_completion_queue_destroy(cq);
  track_counters.Finish(state);
}
BENCHMARK(BM_Pass1Core);

static void BM_Pluck1Core(benchmark::State& state) {
  TrackCounters track_counters;
  // TODO: sreek Templatize this benchmark and pass polling_type as a param
  grpc_completion_queue* cq = grpc_completion_queue_create_for_pluck(nullptr);
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  while (state.KeepRunning()) {
    grpc_cq_completion completion;
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    GPR_ASSERT(grpc_cq_begin_op(cq, nullptr));
    grpc_cq_end_op(&exec_ctx, cq, nullptr, GRPC_ERROR_NONE,
                   DoneWithCompletionOnStack, nullptr, &completion);
    grpc_exec_ctx_finish(&exec_ctx);
    grpc_completion_queue_pluck(cq, nullptr, deadline, nullptr);
  }
  grpc_completion_queue_destroy(cq);
  track_counters.Finish(state);
}
BENCHMARK(BM_Pluck1Core);

static void BM_EmptyCore(benchmark::State& state) {
  TrackCounters track_counters;
  // TODO: sreek Templatize this benchmark and pass polling_type as a param
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  gpr_timespec deadline = gpr_inf_past(GPR_CLOCK_MONOTONIC);
  while (state.KeepRunning()) {
    grpc_completion_queue_next(cq, deadline, nullptr);
  }
  grpc_completion_queue_destroy(cq);
  track_counters.Finish(state);
}
BENCHMARK(BM_EmptyCore);

}  // namespace testing
}  // namespace grpc

BENCHMARK_MAIN();
