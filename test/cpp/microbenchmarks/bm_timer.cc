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

#include <benchmark/benchmark.h>
#include <string.h>
#include <atomic>
#include <vector>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/timer.h"
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

struct TimerClosure {
  grpc_timer timer;
  grpc_closure closure;
};

static void BM_InitCancelTimer(benchmark::State& state) {
  constexpr int kTimerCount = 1024;
  TrackCounters track_counters;
  grpc_core::ExecCtx exec_ctx;
  std::vector<TimerClosure> timer_closures(kTimerCount);
  int i = 0;
  for (auto _ : state) {
    TimerClosure* timer_closure = &timer_closures[i++ % kTimerCount];
    GRPC_CLOSURE_INIT(
        &timer_closure->closure,
        [](void* /*args*/, grpc_error_handle /*err*/) {}, nullptr,
        grpc_schedule_on_exec_ctx);
    grpc_timer_init(&timer_closure->timer, GRPC_MILLIS_INF_FUTURE,
                    &timer_closure->closure);
    grpc_timer_cancel(&timer_closure->timer);
    exec_ctx.Flush();
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_InitCancelTimer);

static void BM_TimerBatch(benchmark::State& state) {
  constexpr int kTimerCount = 1024;
  const bool check = state.range(0);
  const bool reverse = state.range(1);

  const grpc_millis start =
      reverse ? GRPC_MILLIS_INF_FUTURE : GRPC_MILLIS_INF_FUTURE - kTimerCount;
  const grpc_millis end =
      reverse ? GRPC_MILLIS_INF_FUTURE - kTimerCount : GRPC_MILLIS_INF_FUTURE;
  const grpc_millis increment = reverse ? -1 : 1;

  TrackCounters track_counters;
  grpc_core::ExecCtx exec_ctx;
  std::vector<TimerClosure> timer_closures(kTimerCount);
  for (auto _ : state) {
    for (grpc_millis deadline = start; deadline != end; deadline += increment) {
      TimerClosure* timer_closure = &timer_closures[deadline % kTimerCount];
      GRPC_CLOSURE_INIT(
          &timer_closure->closure,
          [](void* /*args*/, grpc_error_handle /*err*/) {}, nullptr,
          grpc_schedule_on_exec_ctx);

      grpc_timer_init(&timer_closure->timer, deadline, &timer_closure->closure);
    }
    if (check) {
      grpc_millis next = GRPC_MILLIS_INF_FUTURE;
      grpc_timer_check(&next);
    }
    for (grpc_millis deadline = start; deadline != end; deadline += increment) {
      TimerClosure* timer_closure = &timer_closures[deadline % kTimerCount];
      grpc_timer_cancel(&timer_closure->timer);
    }
    exec_ctx.Flush();
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_TimerBatch)
    ->Args({/*check=*/false, /*reverse=*/false})
    ->Args({/*check=*/false, /*reverse=*/true})
    ->Args({/*check=*/true, /*reverse=*/false})
    ->Args({/*check=*/true, /*reverse=*/true})
    ->ThreadRange(1, 128);

}  // namespace testing
}  // namespace grpc

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
