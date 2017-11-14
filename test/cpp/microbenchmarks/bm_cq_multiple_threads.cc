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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "test/cpp/microbenchmarks/helpers.h"

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/surface/completion_queue.h"

struct grpc_pollset {
  gpr_mu mu;
};

namespace grpc {
namespace testing {

auto& force_library_initialization = Library::get();

static void* g_tag = (void*)(intptr_t)10;  // Some random number
static grpc_completion_queue* g_cq;
static grpc_event_engine_vtable g_vtable;
static const grpc_event_engine_vtable* g_old_vtable;

static void pollset_shutdown(grpc_exec_ctx* exec_ctx, grpc_pollset* ps,
                             grpc_closure* closure) {
  GRPC_CLOSURE_SCHED(exec_ctx, closure, GRPC_ERROR_NONE);
}

static void pollset_init(grpc_pollset* ps, gpr_mu** mu) {
  gpr_mu_init(&ps->mu);
  *mu = &ps->mu;
}

static void pollset_destroy(grpc_exec_ctx* exec_ctx, grpc_pollset* ps) {
  gpr_mu_destroy(&ps->mu);
}

static grpc_error* pollset_kick(grpc_exec_ctx* exec_ctx, grpc_pollset* p,
                                grpc_pollset_worker* worker) {
  return GRPC_ERROR_NONE;
}

/* Callback when the tag is dequeued from the completion queue. Does nothing */
static void cq_done_cb(grpc_exec_ctx* exec_ctx, void* done_arg,
                       grpc_cq_completion* cq_completion) {
  gpr_free(cq_completion);
}

/* Queues a completion tag if deadline is > 0.
 * Does nothing if deadline is 0 (i.e gpr_time_0(GPR_CLOCK_MONOTONIC)) */
static grpc_error* pollset_work(grpc_exec_ctx* exec_ctx, grpc_pollset* ps,
                                grpc_pollset_worker** worker,
                                grpc_millis deadline) {
  if (deadline == 0) {
    gpr_log(GPR_DEBUG, "no-op");
    return GRPC_ERROR_NONE;
  }

  gpr_mu_unlock(&ps->mu);
  GPR_ASSERT(grpc_cq_begin_op(g_cq, g_tag));
  grpc_cq_end_op(exec_ctx, g_cq, g_tag, GRPC_ERROR_NONE, cq_done_cb, nullptr,
                 (grpc_cq_completion*)gpr_malloc(sizeof(grpc_cq_completion)));
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

  /* Override the event engine with our test event engine (g_vtable); but before
   * that, save the current event engine in g_old_vtable. We will have to set
   * g_old_vtable back before calling grpc_shutdown() */
  init_engine_vtable();
  g_old_vtable = grpc_get_event_engine_test_only();
  grpc_set_event_engine_test_only(&g_vtable);

  g_cq = grpc_completion_queue_create_for_next(nullptr);
}

static void teardown() {
  grpc_completion_queue_shutdown(g_cq);

  /* Drain any events */
  gpr_timespec deadline = gpr_time_0(GPR_CLOCK_MONOTONIC);
  while (grpc_completion_queue_next(g_cq, deadline, nullptr).type !=
         GRPC_QUEUE_SHUTDOWN) {
    /* Do nothing */
  }

  grpc_completion_queue_destroy(g_cq);

  /* Restore the old event engine before calling grpc_shutdown */
  grpc_set_event_engine_test_only(g_old_vtable);
  grpc_shutdown();
}

/* A few notes about Multi-threaded benchmarks:

 Setup:
  The benchmark framework ensures that none of the threads proceed beyond the
  state.KeepRunning() call unless all the threads have called state.keepRunning
  atleast once.  So it is safe to do the initialization in one of the threads
  before state.KeepRunning() is called.

 Teardown:
  The benchmark framework also ensures that no thread is running the benchmark
  code (i.e the code between two successive calls of state.KeepRunning()) if
  state.KeepRunning() returns false. So it is safe to do the teardown in one
  of the threads after state.keepRunning() returns false.
*/
static void BM_Cq_Throughput(benchmark::State& state) {
  TrackCounters track_counters;
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);

  if (state.thread_index == 0) {
    setup();
  }

  while (state.KeepRunning()) {
    GPR_ASSERT(grpc_completion_queue_next(g_cq, deadline, nullptr).type ==
               GRPC_OP_COMPLETE);
  }

  state.SetItemsProcessed(state.iterations());

  if (state.thread_index == 0) {
    teardown();
  }

  track_counters.Finish(state);
}

BENCHMARK(BM_Cq_Throughput)->ThreadRange(1, 16)->UseRealTime();

}  // namespace testing
}  // namespace grpc

BENCHMARK_MAIN();
