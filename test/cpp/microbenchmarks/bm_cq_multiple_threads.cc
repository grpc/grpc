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

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <string.h>

#include <atomic>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/util/crash.h"
#include "src/core/util/time.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

struct grpc_pollset {
  gpr_mu mu;
};

static gpr_mu g_mu;
static gpr_cv g_cv;
static int g_threads_active;
static bool g_active;

namespace grpc {
namespace testing {
static grpc_completion_queue* g_cq;

static void pollset_shutdown(grpc_pollset* /*ps*/, grpc_closure* closure) {
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, absl::OkStatus());
}

static void pollset_init(grpc_pollset* ps, gpr_mu** mu) {
  gpr_mu_init(&ps->mu);
  *mu = &ps->mu;
}

static void pollset_destroy(grpc_pollset* ps) { gpr_mu_destroy(&ps->mu); }

static grpc_error_handle pollset_kick(grpc_pollset* /*p*/,
                                      grpc_pollset_worker* /*worker*/) {
  return absl::OkStatus();
}

// Callback when the tag is dequeued from the completion queue. Does nothing
static void cq_done_cb(void* /*done_arg*/, grpc_cq_completion* cq_completion) {
  gpr_free(cq_completion);
}

// Queues a completion tag if deadline is > 0.
// Does nothing if deadline is 0 (i.e gpr_time_0(GPR_CLOCK_MONOTONIC))
static grpc_error_handle pollset_work(grpc_pollset* ps,
                                      grpc_pollset_worker** /*worker*/,
                                      grpc_core::Timestamp deadline) {
  if (deadline == grpc_core::Timestamp::ProcessEpoch()) {
    VLOG(2) << "no-op";
    return absl::OkStatus();
  }

  gpr_mu_unlock(&ps->mu);

  void* tag = reinterpret_cast<void*>(10);  // Some random number
  CHECK(grpc_cq_begin_op(g_cq, tag));
  grpc_cq_end_op(
      g_cq, tag, absl::OkStatus(), cq_done_cb, nullptr,
      static_cast<grpc_cq_completion*>(gpr_malloc(sizeof(grpc_cq_completion))));
  grpc_core::ExecCtx::Get()->Flush();
  gpr_mu_lock(&ps->mu);
  return absl::OkStatus();
}

static grpc_event_engine_vtable make_engine_vtable(const char* name) {
  grpc_event_engine_vtable vtable;
  memset(&vtable, 0, sizeof(vtable));

  vtable.pollset_size = sizeof(grpc_pollset);
  vtable.pollset_init = pollset_init;
  vtable.pollset_shutdown = pollset_shutdown;
  vtable.pollset_destroy = pollset_destroy;
  vtable.pollset_work = pollset_work;
  vtable.pollset_kick = pollset_kick;
  vtable.is_any_background_poller_thread = [] { return false; };
  vtable.add_closure_to_background_poller = [](grpc_closure* /*closure*/,
                                               grpc_error_handle /*error*/) {
    return false;
  };
  vtable.shutdown_background_closure = [] {};
  vtable.shutdown_engine = [] {};
  vtable.check_engine_available = [](bool) { return true; };
  vtable.init_engine = [] {};
  vtable.name = name;

  return vtable;
}

static void setup() {
  grpc_init();
  CHECK(strcmp(grpc_get_poll_strategy_name(), "none") == 0 ||
        strcmp(grpc_get_poll_strategy_name(), "bm_cq_multiple_threads") == 0);

  g_cq = grpc_completion_queue_create_for_next(nullptr);
}

static void teardown() {
  grpc_completion_queue_shutdown(g_cq);

  // Drain any events
  gpr_timespec deadline = gpr_time_0(GPR_CLOCK_MONOTONIC);
  while (grpc_completion_queue_next(g_cq, deadline, nullptr).type !=
         GRPC_QUEUE_SHUTDOWN) {
    // Do nothing
  }

  grpc_completion_queue_destroy(g_cq);
  grpc_shutdown();
}

// A few notes about Multi-threaded benchmarks:

// Setup:
// The benchmark framework ensures that none of the threads proceed beyond the
// state.KeepRunning() call unless all the threads have called state.keepRunning
// at least once.  So it is safe to do the initialization in one of the threads
// before state.KeepRunning() is called.

// Teardown:
// The benchmark framework also ensures that no thread is running the benchmark
// code (i.e the code between two successive calls of state.KeepRunning()) if
// state.KeepRunning() returns false. So it is safe to do the teardown in one
// of the threads after state.keepRunning() returns false.

// However, our use requires synchronization because we do additional work at
// each thread that requires specific ordering (TrackCounters must be
// constructed after grpc_init because it needs the number of cores, initialized
// by grpc, and its Finish call must take place before grpc_shutdown so that it
// can use grpc_stats).
//
static void BM_Cq_Throughput(benchmark::State& state) {
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  auto thd_idx = state.thread_index();

  gpr_mu_lock(&g_mu);
  g_threads_active++;
  if (thd_idx == 0) {
    setup();
    g_active = true;
    gpr_cv_broadcast(&g_cv);
  } else {
    while (!g_active) {
      gpr_cv_wait(&g_cv, &g_mu, deadline);
    }
  }
  gpr_mu_unlock(&g_mu);

  for (auto _ : state) {
    CHECK(grpc_completion_queue_next(g_cq, deadline, nullptr).type ==
          GRPC_OP_COMPLETE);
  }

  state.SetItemsProcessed(state.iterations());

  gpr_mu_lock(&g_mu);
  g_threads_active--;
  if (g_threads_active == 0) {
    gpr_cv_broadcast(&g_cv);
  } else {
    while (g_threads_active > 0) {
      gpr_cv_wait(&g_cv, &g_mu, deadline);
    }
  }
  gpr_mu_unlock(&g_mu);

  if (thd_idx == 0) {
    teardown();
    g_active = false;
  }
}

BENCHMARK(BM_Cq_Throughput)->ThreadRange(1, 16)->UseRealTime();

namespace {
const grpc_event_engine_vtable g_none_vtable =
    grpc::testing::make_engine_vtable("none");
const grpc_event_engine_vtable g_bm_vtable =
    grpc::testing::make_engine_vtable("bm_cq_multiple_threads");
}  // namespace

}  // namespace testing
}  // namespace grpc

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  // This test should only ever be run with a non or any polling engine
  // Override the polling engine for the non-polling engine
  // and add a custom polling engine
  grpc_register_event_engine_factory(&grpc::testing::g_none_vtable, false);
  grpc_register_event_engine_factory(&grpc::testing::g_bm_vtable, true);
  grpc::testing::TestEnvironment env(&argc, argv);
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_cv);
  ::benchmark::Initialize(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
