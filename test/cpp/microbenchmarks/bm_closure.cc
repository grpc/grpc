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

/* Test various closure related operations */

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>
#include <sstream>

extern "C" {
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/support/spinlock.h"
}

#include "test/cpp/microbenchmarks/helpers.h"

auto& force_library_initialization = Library::get();

static void BM_NoOpExecCtx(benchmark::State& state) {
  TrackCounters track_counters;
  while (state.KeepRunning()) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_exec_ctx_finish(&exec_ctx);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_NoOpExecCtx);

static void BM_WellFlushed(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_WellFlushed);

static void DoNothing(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {}

static void BM_ClosureInitAgainstExecCtx(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_closure c;
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(
        grpc_closure_init(&c, DoNothing, NULL, grpc_schedule_on_exec_ctx));
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureInitAgainstExecCtx);

static void BM_ClosureInitAgainstCombiner(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_combiner* combiner = grpc_combiner_create(NULL);
  grpc_closure c;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(grpc_closure_init(
        &c, DoNothing, NULL, grpc_combiner_scheduler(combiner, false)));
  }
  GRPC_COMBINER_UNREF(&exec_ctx, combiner, "finished");
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureInitAgainstCombiner);

static void BM_ClosureRunOnExecCtx(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_closure c;
  grpc_closure_init(&c, DoNothing, NULL, grpc_schedule_on_exec_ctx);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_closure_run(&exec_ctx, &c, GRPC_ERROR_NONE);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureRunOnExecCtx);

static void BM_ClosureCreateAndRun(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_closure_run(&exec_ctx, grpc_closure_create(DoNothing, NULL,
                                                    grpc_schedule_on_exec_ctx),
                     GRPC_ERROR_NONE);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureCreateAndRun);

static void BM_ClosureInitAndRun(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_closure c;
  while (state.KeepRunning()) {
    grpc_closure_run(&exec_ctx, grpc_closure_init(&c, DoNothing, NULL,
                                                  grpc_schedule_on_exec_ctx),
                     GRPC_ERROR_NONE);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureInitAndRun);

static void BM_ClosureSchedOnExecCtx(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_closure c;
  grpc_closure_init(&c, DoNothing, NULL, grpc_schedule_on_exec_ctx);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_closure_sched(&exec_ctx, &c, GRPC_ERROR_NONE);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureSchedOnExecCtx);

static void BM_ClosureSched2OnExecCtx(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_closure c1;
  grpc_closure c2;
  grpc_closure_init(&c1, DoNothing, NULL, grpc_schedule_on_exec_ctx);
  grpc_closure_init(&c2, DoNothing, NULL, grpc_schedule_on_exec_ctx);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_closure_sched(&exec_ctx, &c1, GRPC_ERROR_NONE);
    grpc_closure_sched(&exec_ctx, &c2, GRPC_ERROR_NONE);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureSched2OnExecCtx);

static void BM_ClosureSched3OnExecCtx(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_closure c1;
  grpc_closure c2;
  grpc_closure c3;
  grpc_closure_init(&c1, DoNothing, NULL, grpc_schedule_on_exec_ctx);
  grpc_closure_init(&c2, DoNothing, NULL, grpc_schedule_on_exec_ctx);
  grpc_closure_init(&c3, DoNothing, NULL, grpc_schedule_on_exec_ctx);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_closure_sched(&exec_ctx, &c1, GRPC_ERROR_NONE);
    grpc_closure_sched(&exec_ctx, &c2, GRPC_ERROR_NONE);
    grpc_closure_sched(&exec_ctx, &c3, GRPC_ERROR_NONE);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureSched3OnExecCtx);

static void BM_AcquireMutex(benchmark::State& state) {
  TrackCounters track_counters;
  // for comparison with the combiner stuff below
  gpr_mu mu;
  gpr_mu_init(&mu);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    gpr_mu_lock(&mu);
    DoNothing(&exec_ctx, NULL, GRPC_ERROR_NONE);
    gpr_mu_unlock(&mu);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_AcquireMutex);

static void BM_TryAcquireMutex(benchmark::State& state) {
  TrackCounters track_counters;
  // for comparison with the combiner stuff below
  gpr_mu mu;
  gpr_mu_init(&mu);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    if (gpr_mu_trylock(&mu)) {
      DoNothing(&exec_ctx, NULL, GRPC_ERROR_NONE);
      gpr_mu_unlock(&mu);
    } else {
      abort();
    }
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_TryAcquireMutex);

static void BM_AcquireSpinlock(benchmark::State& state) {
  TrackCounters track_counters;
  // for comparison with the combiner stuff below
  gpr_spinlock mu = GPR_SPINLOCK_INITIALIZER;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    gpr_spinlock_lock(&mu);
    DoNothing(&exec_ctx, NULL, GRPC_ERROR_NONE);
    gpr_spinlock_unlock(&mu);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_AcquireSpinlock);

static void BM_TryAcquireSpinlock(benchmark::State& state) {
  TrackCounters track_counters;
  // for comparison with the combiner stuff below
  gpr_spinlock mu = GPR_SPINLOCK_INITIALIZER;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    if (gpr_spinlock_trylock(&mu)) {
      DoNothing(&exec_ctx, NULL, GRPC_ERROR_NONE);
      gpr_spinlock_unlock(&mu);
    } else {
      abort();
    }
  }
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_TryAcquireSpinlock);

static void BM_ClosureSchedOnCombiner(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_combiner* combiner = grpc_combiner_create(NULL);
  grpc_closure c;
  grpc_closure_init(&c, DoNothing, NULL,
                    grpc_combiner_scheduler(combiner, false));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_closure_sched(&exec_ctx, &c, GRPC_ERROR_NONE);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  GRPC_COMBINER_UNREF(&exec_ctx, combiner, "finished");
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureSchedOnCombiner);

static void BM_ClosureSched2OnCombiner(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_combiner* combiner = grpc_combiner_create(NULL);
  grpc_closure c1;
  grpc_closure c2;
  grpc_closure_init(&c1, DoNothing, NULL,
                    grpc_combiner_scheduler(combiner, false));
  grpc_closure_init(&c2, DoNothing, NULL,
                    grpc_combiner_scheduler(combiner, false));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_closure_sched(&exec_ctx, &c1, GRPC_ERROR_NONE);
    grpc_closure_sched(&exec_ctx, &c2, GRPC_ERROR_NONE);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  GRPC_COMBINER_UNREF(&exec_ctx, combiner, "finished");
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureSched2OnCombiner);

static void BM_ClosureSched3OnCombiner(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_combiner* combiner = grpc_combiner_create(NULL);
  grpc_closure c1;
  grpc_closure c2;
  grpc_closure c3;
  grpc_closure_init(&c1, DoNothing, NULL,
                    grpc_combiner_scheduler(combiner, false));
  grpc_closure_init(&c2, DoNothing, NULL,
                    grpc_combiner_scheduler(combiner, false));
  grpc_closure_init(&c3, DoNothing, NULL,
                    grpc_combiner_scheduler(combiner, false));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_closure_sched(&exec_ctx, &c1, GRPC_ERROR_NONE);
    grpc_closure_sched(&exec_ctx, &c2, GRPC_ERROR_NONE);
    grpc_closure_sched(&exec_ctx, &c3, GRPC_ERROR_NONE);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  GRPC_COMBINER_UNREF(&exec_ctx, combiner, "finished");
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureSched3OnCombiner);

static void BM_ClosureSched2OnTwoCombiners(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_combiner* combiner1 = grpc_combiner_create(NULL);
  grpc_combiner* combiner2 = grpc_combiner_create(NULL);
  grpc_closure c1;
  grpc_closure c2;
  grpc_closure_init(&c1, DoNothing, NULL,
                    grpc_combiner_scheduler(combiner1, false));
  grpc_closure_init(&c2, DoNothing, NULL,
                    grpc_combiner_scheduler(combiner2, false));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_closure_sched(&exec_ctx, &c1, GRPC_ERROR_NONE);
    grpc_closure_sched(&exec_ctx, &c2, GRPC_ERROR_NONE);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  GRPC_COMBINER_UNREF(&exec_ctx, combiner1, "finished");
  GRPC_COMBINER_UNREF(&exec_ctx, combiner2, "finished");
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureSched2OnTwoCombiners);

static void BM_ClosureSched4OnTwoCombiners(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_combiner* combiner1 = grpc_combiner_create(NULL);
  grpc_combiner* combiner2 = grpc_combiner_create(NULL);
  grpc_closure c1;
  grpc_closure c2;
  grpc_closure c3;
  grpc_closure c4;
  grpc_closure_init(&c1, DoNothing, NULL,
                    grpc_combiner_scheduler(combiner1, false));
  grpc_closure_init(&c2, DoNothing, NULL,
                    grpc_combiner_scheduler(combiner2, false));
  grpc_closure_init(&c3, DoNothing, NULL,
                    grpc_combiner_scheduler(combiner1, false));
  grpc_closure_init(&c4, DoNothing, NULL,
                    grpc_combiner_scheduler(combiner2, false));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (state.KeepRunning()) {
    grpc_closure_sched(&exec_ctx, &c1, GRPC_ERROR_NONE);
    grpc_closure_sched(&exec_ctx, &c2, GRPC_ERROR_NONE);
    grpc_closure_sched(&exec_ctx, &c3, GRPC_ERROR_NONE);
    grpc_closure_sched(&exec_ctx, &c4, GRPC_ERROR_NONE);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  GRPC_COMBINER_UNREF(&exec_ctx, combiner1, "finished");
  GRPC_COMBINER_UNREF(&exec_ctx, combiner2, "finished");
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureSched4OnTwoCombiners);

// Helper that continuously reschedules the same closure against something until
// the benchmark is complete
class Rescheduler {
 public:
  Rescheduler(benchmark::State& state, grpc_closure_scheduler* scheduler)
      : state_(state) {
    grpc_closure_init(&closure_, Step, this, scheduler);
  }

  void ScheduleFirst(grpc_exec_ctx* exec_ctx) {
    grpc_closure_sched(exec_ctx, &closure_, GRPC_ERROR_NONE);
  }

  void ScheduleFirstAgainstDifferentScheduler(
      grpc_exec_ctx* exec_ctx, grpc_closure_scheduler* scheduler) {
    grpc_closure_sched(exec_ctx, grpc_closure_create(Step, this, scheduler),
                       GRPC_ERROR_NONE);
  }

 private:
  benchmark::State& state_;
  grpc_closure closure_;

  static void Step(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {
    Rescheduler* self = static_cast<Rescheduler*>(arg);
    if (self->state_.KeepRunning()) {
      grpc_closure_sched(exec_ctx, &self->closure_, GRPC_ERROR_NONE);
    }
  }
};

static void BM_ClosureReschedOnExecCtx(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  Rescheduler r(state, grpc_schedule_on_exec_ctx);
  r.ScheduleFirst(&exec_ctx);
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureReschedOnExecCtx);

static void BM_ClosureReschedOnCombiner(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_combiner* combiner = grpc_combiner_create(NULL);
  Rescheduler r(state, grpc_combiner_scheduler(combiner, false));
  r.ScheduleFirst(&exec_ctx);
  grpc_exec_ctx_flush(&exec_ctx);
  GRPC_COMBINER_UNREF(&exec_ctx, combiner, "finished");
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureReschedOnCombiner);

static void BM_ClosureReschedOnCombinerFinally(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_combiner* combiner = grpc_combiner_create(NULL);
  Rescheduler r(state, grpc_combiner_finally_scheduler(combiner, false));
  r.ScheduleFirstAgainstDifferentScheduler(
      &exec_ctx, grpc_combiner_scheduler(combiner, false));
  grpc_exec_ctx_flush(&exec_ctx);
  GRPC_COMBINER_UNREF(&exec_ctx, combiner, "finished");
  grpc_exec_ctx_finish(&exec_ctx);
  track_counters.Finish(state);
}
BENCHMARK(BM_ClosureReschedOnCombinerFinally);

BENCHMARK_MAIN();
