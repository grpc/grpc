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

// Test various closure related operations

#include <sstream>

#include <benchmark/benchmark.h>

#include <grpc/grpc.h>

#include "src/core/lib/gpr/spinlock.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

static void BM_NoOpExecCtx(benchmark::State& state) {
  for (auto _ : state) {
    grpc_core::ExecCtx exec_ctx;
  }
}
BENCHMARK(BM_NoOpExecCtx);

static void BM_WellFlushed(benchmark::State& state) {
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    grpc_core::ExecCtx::Get()->Flush();
  }
}
BENCHMARK(BM_WellFlushed);

static void DoNothing(void* /*arg*/, grpc_error_handle /*error*/) {}

static void BM_ClosureInitAgainstExecCtx(benchmark::State& state) {
  grpc_closure c;
  for (auto _ : state) {
    benchmark::DoNotOptimize(
        GRPC_CLOSURE_INIT(&c, DoNothing, nullptr, grpc_schedule_on_exec_ctx));
  }
}
BENCHMARK(BM_ClosureInitAgainstExecCtx);

static void BM_ClosureInitAgainstCombiner(benchmark::State& state) {
  grpc_core::Combiner* combiner = grpc_combiner_create(
      grpc_event_engine::experimental::CreateEventEngine());
  grpc_closure c;
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    benchmark::DoNotOptimize(
        GRPC_CLOSURE_INIT(&c, DoNothing, nullptr, nullptr));
  }
  GRPC_COMBINER_UNREF(combiner, "finished");
}
BENCHMARK(BM_ClosureInitAgainstCombiner);

static void BM_ClosureRun(benchmark::State& state) {
  grpc_closure c;
  GRPC_CLOSURE_INIT(&c, DoNothing, nullptr, grpc_schedule_on_exec_ctx);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    grpc_core::Closure::Run(DEBUG_LOCATION, &c, absl::OkStatus());
  }
}
BENCHMARK(BM_ClosureRun);

static void BM_ClosureCreateAndRun(benchmark::State& state) {
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    grpc_core::Closure::Run(
        DEBUG_LOCATION,
        GRPC_CLOSURE_CREATE(DoNothing, nullptr, grpc_schedule_on_exec_ctx),
        absl::OkStatus());
  }
}
BENCHMARK(BM_ClosureCreateAndRun);

static void BM_ClosureInitAndRun(benchmark::State& state) {
  grpc_core::ExecCtx exec_ctx;
  grpc_closure c;
  for (auto _ : state) {
    grpc_core::Closure::Run(
        DEBUG_LOCATION,
        GRPC_CLOSURE_INIT(&c, DoNothing, nullptr, grpc_schedule_on_exec_ctx),
        absl::OkStatus());
  }
}
BENCHMARK(BM_ClosureInitAndRun);

static void BM_ClosureSchedOnExecCtx(benchmark::State& state) {
  grpc_closure c;
  GRPC_CLOSURE_INIT(&c, DoNothing, nullptr, grpc_schedule_on_exec_ctx);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &c, absl::OkStatus());
    grpc_core::ExecCtx::Get()->Flush();
  }
}
BENCHMARK(BM_ClosureSchedOnExecCtx);

static void BM_ClosureSched2OnExecCtx(benchmark::State& state) {
  grpc_closure c1;
  grpc_closure c2;
  GRPC_CLOSURE_INIT(&c1, DoNothing, nullptr, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&c2, DoNothing, nullptr, grpc_schedule_on_exec_ctx);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &c1, absl::OkStatus());
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &c2, absl::OkStatus());
    grpc_core::ExecCtx::Get()->Flush();
  }
}
BENCHMARK(BM_ClosureSched2OnExecCtx);

static void BM_ClosureSched3OnExecCtx(benchmark::State& state) {
  grpc_closure c1;
  grpc_closure c2;
  grpc_closure c3;
  GRPC_CLOSURE_INIT(&c1, DoNothing, nullptr, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&c2, DoNothing, nullptr, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&c3, DoNothing, nullptr, grpc_schedule_on_exec_ctx);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &c1, absl::OkStatus());
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &c2, absl::OkStatus());
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &c3, absl::OkStatus());
    grpc_core::ExecCtx::Get()->Flush();
  }
}
BENCHMARK(BM_ClosureSched3OnExecCtx);

static void BM_AcquireMutex(benchmark::State& state) {
  // for comparison with the combiner stuff below
  gpr_mu mu;
  gpr_mu_init(&mu);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    gpr_mu_lock(&mu);
    DoNothing(nullptr, absl::OkStatus());
    gpr_mu_unlock(&mu);
  }
  gpr_mu_destroy(&mu);
}
BENCHMARK(BM_AcquireMutex);

static void BM_TryAcquireMutex(benchmark::State& state) {
  // for comparison with the combiner stuff below
  gpr_mu mu;
  gpr_mu_init(&mu);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    if (gpr_mu_trylock(&mu)) {
      DoNothing(nullptr, absl::OkStatus());
      gpr_mu_unlock(&mu);
    } else {
      abort();
    }
  }
  gpr_mu_destroy(&mu);
}
BENCHMARK(BM_TryAcquireMutex);

static void BM_AcquireSpinlock(benchmark::State& state) {
  // for comparison with the combiner stuff below
  gpr_spinlock mu = GPR_SPINLOCK_INITIALIZER;
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    gpr_spinlock_lock(&mu);
    DoNothing(nullptr, absl::OkStatus());
    gpr_spinlock_unlock(&mu);
  }
}
BENCHMARK(BM_AcquireSpinlock);

static void BM_TryAcquireSpinlock(benchmark::State& state) {
  // for comparison with the combiner stuff below
  gpr_spinlock mu = GPR_SPINLOCK_INITIALIZER;
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    if (gpr_spinlock_trylock(&mu)) {
      DoNothing(nullptr, absl::OkStatus());
      gpr_spinlock_unlock(&mu);
    } else {
      abort();
    }
  }
}
BENCHMARK(BM_TryAcquireSpinlock);

static void BM_ClosureSchedOnCombiner(benchmark::State& state) {
  grpc_core::Combiner* combiner = grpc_combiner_create(
      grpc_event_engine::experimental::CreateEventEngine());
  grpc_closure c;
  GRPC_CLOSURE_INIT(&c, DoNothing, nullptr, nullptr);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    combiner->Run(&c, absl::OkStatus());
    grpc_core::ExecCtx::Get()->Flush();
  }
  GRPC_COMBINER_UNREF(combiner, "finished");
}
BENCHMARK(BM_ClosureSchedOnCombiner);

static void BM_ClosureSched2OnCombiner(benchmark::State& state) {
  grpc_core::Combiner* combiner = grpc_combiner_create(
      grpc_event_engine::experimental::CreateEventEngine());
  grpc_closure c1;
  grpc_closure c2;
  GRPC_CLOSURE_INIT(&c1, DoNothing, nullptr, nullptr);
  GRPC_CLOSURE_INIT(&c2, DoNothing, nullptr, nullptr);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    combiner->Run(&c1, absl::OkStatus());
    combiner->Run(&c2, absl::OkStatus());
    grpc_core::ExecCtx::Get()->Flush();
  }
  GRPC_COMBINER_UNREF(combiner, "finished");
}
BENCHMARK(BM_ClosureSched2OnCombiner);

static void BM_ClosureSched3OnCombiner(benchmark::State& state) {
  grpc_core::Combiner* combiner = grpc_combiner_create(
      grpc_event_engine::experimental::CreateEventEngine());
  grpc_closure c1;
  grpc_closure c2;
  grpc_closure c3;
  GRPC_CLOSURE_INIT(&c1, DoNothing, nullptr, nullptr);
  GRPC_CLOSURE_INIT(&c2, DoNothing, nullptr, nullptr);
  GRPC_CLOSURE_INIT(&c3, DoNothing, nullptr, nullptr);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    combiner->Run(&c1, absl::OkStatus());
    combiner->Run(&c2, absl::OkStatus());
    combiner->Run(&c3, absl::OkStatus());
    grpc_core::ExecCtx::Get()->Flush();
  }
  GRPC_COMBINER_UNREF(combiner, "finished");
}
BENCHMARK(BM_ClosureSched3OnCombiner);

static void BM_ClosureSched2OnTwoCombiners(benchmark::State& state) {
  grpc_core::Combiner* combiner1 = grpc_combiner_create(
      grpc_event_engine::experimental::CreateEventEngine());
  grpc_core::Combiner* combiner2 = grpc_combiner_create(
      grpc_event_engine::experimental::CreateEventEngine());
  grpc_closure c1;
  grpc_closure c2;
  GRPC_CLOSURE_INIT(&c1, DoNothing, nullptr, nullptr);
  GRPC_CLOSURE_INIT(&c2, DoNothing, nullptr, nullptr);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    combiner1->Run(&c1, absl::OkStatus());
    combiner2->Run(&c2, absl::OkStatus());
    grpc_core::ExecCtx::Get()->Flush();
  }
  GRPC_COMBINER_UNREF(combiner1, "finished");
  GRPC_COMBINER_UNREF(combiner2, "finished");
}
BENCHMARK(BM_ClosureSched2OnTwoCombiners);

static void BM_ClosureSched4OnTwoCombiners(benchmark::State& state) {
  grpc_core::Combiner* combiner1 = grpc_combiner_create(
      grpc_event_engine::experimental::CreateEventEngine());
  grpc_core::Combiner* combiner2 = grpc_combiner_create(
      grpc_event_engine::experimental::CreateEventEngine());
  grpc_closure c1;
  grpc_closure c2;
  grpc_closure c3;
  grpc_closure c4;
  GRPC_CLOSURE_INIT(&c1, DoNothing, nullptr, nullptr);
  GRPC_CLOSURE_INIT(&c2, DoNothing, nullptr, nullptr);
  GRPC_CLOSURE_INIT(&c3, DoNothing, nullptr, nullptr);
  GRPC_CLOSURE_INIT(&c4, DoNothing, nullptr, nullptr);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    combiner1->Run(&c1, absl::OkStatus());
    combiner2->Run(&c2, absl::OkStatus());
    combiner1->Run(&c3, absl::OkStatus());
    combiner2->Run(&c4, absl::OkStatus());
    grpc_core::ExecCtx::Get()->Flush();
  }
  GRPC_COMBINER_UNREF(combiner1, "finished");
  GRPC_COMBINER_UNREF(combiner2, "finished");
}
BENCHMARK(BM_ClosureSched4OnTwoCombiners);

// Helper that continuously reschedules the same closure against something until
// the benchmark is complete
class Rescheduler {
 public:
  explicit Rescheduler(benchmark::State& state) : state_(state) {
    GRPC_CLOSURE_INIT(&closure_, Step, this, nullptr);
  }

  void ScheduleFirst() {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &closure_, absl::OkStatus());
  }

  void ScheduleFirstAgainstDifferentScheduler() {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                            GRPC_CLOSURE_CREATE(Step, this, nullptr),
                            absl::OkStatus());
  }

 private:
  benchmark::State& state_;
  grpc_closure closure_;

  static void Step(void* arg, grpc_error_handle /*error*/) {
    Rescheduler* self = static_cast<Rescheduler*>(arg);
    if (self->state_.KeepRunning()) {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, &self->closure_,
                              absl::OkStatus());
    }
  }
};

static void BM_ClosureReschedOnExecCtx(benchmark::State& state) {
  grpc_core::ExecCtx exec_ctx;
  Rescheduler r(state);
  r.ScheduleFirst();
  grpc_core::ExecCtx::Get()->Flush();
}
BENCHMARK(BM_ClosureReschedOnExecCtx);

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
