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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/executor.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/tls.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/exec_ctx.h"

#define MAX_DEPTH 2

#define EXECUTOR_TRACE(format, ...)                     \
  if (executor_trace.enabled()) {                       \
    gpr_log(GPR_INFO, "EXECUTOR " format, __VA_ARGS__); \
  }

grpc_core::TraceFlag executor_trace(false, "executor");

GPR_TLS_DECL(g_this_thread_state);

GrpcExecutor::GrpcExecutor(const char* executor_name) : name_(executor_name) {
  adding_thread_lock_ = GPR_SPINLOCK_STATIC_INITIALIZER;
  gpr_atm_no_barrier_store(&num_threads_, 0);
  max_threads_ = GPR_MAX(1, 2 * gpr_cpu_num_cores());
}

void GrpcExecutor::Init() { SetThreading(true); }

size_t GrpcExecutor::RunClosures(grpc_closure_list list) {
  size_t n = 0;

  grpc_closure* c = list.head;
  while (c != nullptr) {
    grpc_closure* next = c->next_data.next;
    grpc_error* error = c->error_data.error;
#ifndef NDEBUG
    EXECUTOR_TRACE("run %p [created by %s:%d]", c, c->file_created,
                   c->line_created);
    c->scheduled = false;
#else
    EXECUTOR_TRACE("run %p", c);
#endif
    c->cb(c->cb_arg, error);
    GRPC_ERROR_UNREF(error);
    c = next;
    n++;
    grpc_core::ExecCtx::Get()->Flush();
  }

  return n;
}

bool GrpcExecutor::IsThreaded() const {
  return gpr_atm_no_barrier_load(&num_threads_) > 0;
}

void GrpcExecutor::SetThreading(bool threading) {
  gpr_atm curr_num_threads = gpr_atm_no_barrier_load(&num_threads_);

  if (threading) {
    if (curr_num_threads > 0) return;

    GPR_ASSERT(num_threads_ == 0);
    gpr_atm_no_barrier_store(&num_threads_, 1);
    gpr_tls_init(&g_this_thread_state);
    thd_state_ = static_cast<ThreadState*>(
        gpr_zalloc(sizeof(ThreadState) * max_threads_));

    for (size_t i = 0; i < max_threads_; i++) {
      gpr_mu_init(&thd_state_[i].mu);
      gpr_cv_init(&thd_state_[i].cv);
      thd_state_[i].id = i;
      thd_state_[i].thd = grpc_core::Thread();
      thd_state_[i].elems = GRPC_CLOSURE_LIST_INIT;
    }

    thd_state_[0].thd =
        grpc_core::Thread(name_, &GrpcExecutor::ThreadMain, &thd_state_[0]);
    thd_state_[0].thd.Start();
  } else {  // !threading
    if (curr_num_threads == 0) return;

    for (size_t i = 0; i < max_threads_; i++) {
      gpr_mu_lock(&thd_state_[i].mu);
      thd_state_[i].shutdown = true;
      gpr_cv_signal(&thd_state_[i].cv);
      gpr_mu_unlock(&thd_state_[i].mu);
    }

    /* Ensure no thread is adding a new thread. Once this is past, then no
     * thread will try to add a new one either (since shutdown is true) */
    gpr_spinlock_lock(&adding_thread_lock_);
    gpr_spinlock_unlock(&adding_thread_lock_);

    curr_num_threads = gpr_atm_no_barrier_load(&num_threads_);
    for (gpr_atm i = 0; i < curr_num_threads; i++) {
      thd_state_[i].thd.Join();
      EXECUTOR_TRACE(" Thread %" PRIdPTR " of %" PRIdPTR " joined", i,
                     curr_num_threads);
    }

    gpr_atm_no_barrier_store(&num_threads_, 0);
    for (size_t i = 0; i < max_threads_; i++) {
      gpr_mu_destroy(&thd_state_[i].mu);
      gpr_cv_destroy(&thd_state_[i].cv);
      RunClosures(thd_state_[i].elems);
    }

    gpr_free(thd_state_);
    gpr_tls_destroy(&g_this_thread_state);
  }
}

void GrpcExecutor::Shutdown() { SetThreading(false); }

void GrpcExecutor::ThreadMain(void* arg) {
  ThreadState* ts = static_cast<ThreadState*>(arg);
  gpr_tls_set(&g_this_thread_state, reinterpret_cast<intptr_t>(ts));

  grpc_core::ExecCtx exec_ctx(GRPC_EXEC_CTX_FLAG_IS_INTERNAL_THREAD);

  size_t subtract_depth = 0;
  for (;;) {
    EXECUTOR_TRACE("[%" PRIdPTR "]: step (sub_depth=%" PRIdPTR ")", ts->id,
                   subtract_depth);

    gpr_mu_lock(&ts->mu);
    ts->depth -= subtract_depth;
    // Wait for closures to be enqueued or for the executor to be shutdown
    while (grpc_closure_list_empty(ts->elems) && !ts->shutdown) {
      ts->queued_long_job = false;
      gpr_cv_wait(&ts->cv, &ts->mu, gpr_inf_future(GPR_CLOCK_MONOTONIC));
    }

    if (ts->shutdown) {
      EXECUTOR_TRACE("[%" PRIdPTR "]: shutdown", ts->id);
      gpr_mu_unlock(&ts->mu);
      break;
    }

    GRPC_STATS_INC_EXECUTOR_QUEUE_DRAINED();
    grpc_closure_list closures = ts->elems;
    ts->elems = GRPC_CLOSURE_LIST_INIT;
    gpr_mu_unlock(&ts->mu);

    EXECUTOR_TRACE("[%" PRIdPTR "]: execute", ts->id);

    grpc_core::ExecCtx::Get()->InvalidateNow();
    subtract_depth = RunClosures(closures);
  }
}

void GrpcExecutor::Enqueue(grpc_closure* closure, grpc_error* error,
                           bool is_short) {
  bool retry_push;
  if (is_short) {
    GRPC_STATS_INC_EXECUTOR_SCHEDULED_SHORT_ITEMS();
  } else {
    GRPC_STATS_INC_EXECUTOR_SCHEDULED_LONG_ITEMS();
  }

  do {
    retry_push = false;
    size_t cur_thread_count =
        static_cast<size_t>(gpr_atm_no_barrier_load(&num_threads_));

    // If the number of threads is zero(i.e either the executor is not threaded
    // or already shutdown), then queue the closure on the exec context itself
    if (cur_thread_count == 0) {
#ifndef NDEBUG
      EXECUTOR_TRACE("schedule %p (created %s:%d) inline", closure,
                     closure->file_created, closure->line_created);
#else
      EXECUTOR_TRACE("schedule %p inline", closure);
#endif
      grpc_closure_list_append(grpc_core::ExecCtx::Get()->closure_list(),
                               closure, error);
      return;
    }

    ThreadState* ts = (ThreadState*)gpr_tls_get(&g_this_thread_state);
    if (ts == nullptr) {
      ts = &thd_state_[GPR_HASH_POINTER(grpc_core::ExecCtx::Get(),
                                        cur_thread_count)];
    } else {
      GRPC_STATS_INC_EXECUTOR_SCHEDULED_TO_SELF();
    }

    ThreadState* orig_ts = ts;

    bool try_new_thread = false;
    for (;;) {
#ifndef NDEBUG
      EXECUTOR_TRACE(
          "try to schedule %p (%s) (created %s:%d) to thread "
          "%" PRIdPTR,
          closure, is_short ? "short" : "long", closure->file_created,
          closure->line_created, ts->id);
#else
      EXECUTOR_TRACE("try to schedule %p (%s) to thread %" PRIdPTR, closure,
                     is_short ? "short" : "long", ts->id);
#endif

      gpr_mu_lock(&ts->mu);
      if (ts->queued_long_job) {
        // if there's a long job queued, we never queue anything else to this
        // queue (since long jobs can take 'infinite' time and we need to
        // guarantee no starvation). Spin through queues and try again
        gpr_mu_unlock(&ts->mu);
        size_t idx = ts->id;
        ts = &thd_state_[(idx + 1) % cur_thread_count];
        if (ts == orig_ts) {
          // We cycled through all the threads. Retry enqueue again (by creating
          // a new thread)
          retry_push = true;
          // TODO (sreek): What if the executor is shutdown OR if
          // cur_thread_count is already equal to max_threads ? (currently - as
          // of July 2018, we do not run in to this issue because there is only
          // one instance of long job in gRPC. This has to be fixed soon)
          try_new_thread = true;
          break;
        }

        continue;
      }

      // == Found the thread state (i.e thread) to enqueue this closure! ==

      // Also, if this thread has been waiting for closures, wake it up.
      // - If grpc_closure_list_empty() is true and the Executor is not
      //   shutdown, it means that the thread must be waiting in ThreadMain()
      // - Note that gpr_cv_signal() won't immediately wakeup the thread. That
      //   happens after we release the mutex &ts->mu a few lines below
      if (grpc_closure_list_empty(ts->elems) && !ts->shutdown) {
        GRPC_STATS_INC_EXECUTOR_WAKEUP_INITIATED();
        gpr_cv_signal(&ts->cv);
      }

      grpc_closure_list_append(&ts->elems, closure, error);

      // If we already queued more than MAX_DEPTH number of closures on this
      // thread, use this as a hint to create more threads
      ts->depth++;
      try_new_thread = ts->depth > MAX_DEPTH &&
                       cur_thread_count < max_threads_ && !ts->shutdown;

      ts->queued_long_job = !is_short;

      gpr_mu_unlock(&ts->mu);
      break;
    }

    if (try_new_thread && gpr_spinlock_trylock(&adding_thread_lock_)) {
      cur_thread_count =
          static_cast<size_t>(gpr_atm_no_barrier_load(&num_threads_));
      if (cur_thread_count < max_threads_) {
        // Increment num_threads (Safe to do a no_barrier_store instead of a
        // cas because we always increment num_threads under the
        // 'adding_thread_lock')
        gpr_atm_no_barrier_store(&num_threads_, cur_thread_count + 1);

        thd_state_[cur_thread_count].thd = grpc_core::Thread(
            name_, &GrpcExecutor::ThreadMain, &thd_state_[cur_thread_count]);
        thd_state_[cur_thread_count].thd.Start();
      }
      gpr_spinlock_unlock(&adding_thread_lock_);
    }

    if (retry_push) {
      GRPC_STATS_INC_EXECUTOR_PUSH_RETRIES();
    }
  } while (retry_push);
}

static GrpcExecutor* global_executor;

void enqueue_long(grpc_closure* closure, grpc_error* error) {
  global_executor->Enqueue(closure, error, false /* is_short */);
}

void enqueue_short(grpc_closure* closure, grpc_error* error) {
  global_executor->Enqueue(closure, error, true /* is_short */);
}

// Short-Job executor scheduler
static const grpc_closure_scheduler_vtable global_executor_vtable_short = {
    enqueue_short, enqueue_short, "executor-short"};
static grpc_closure_scheduler global_scheduler_short = {
    &global_executor_vtable_short};

// Long-job executor scheduler
static const grpc_closure_scheduler_vtable global_executor_vtable_long = {
    enqueue_long, enqueue_long, "executor-long"};
static grpc_closure_scheduler global_scheduler_long = {
    &global_executor_vtable_long};

// grpc_executor_init() and grpc_executor_shutdown() functions are called in the
// the grpc_init() and grpc_shutdown() code paths which are protected by a
// global mutex. So it is okay to assume that these functions are thread-safe
void grpc_executor_init() {
  if (global_executor != nullptr) {
    // grpc_executor_init() already called once (and grpc_executor_shutdown()
    // wasn't called)
    return;
  }

  global_executor = grpc_core::New<GrpcExecutor>("global-executor");
  global_executor->Init();
}

void grpc_executor_shutdown() {
  // Shutdown already called
  if (global_executor == nullptr) {
    return;
  }

  global_executor->Shutdown();
  grpc_core::Delete<GrpcExecutor>(global_executor);
  global_executor = nullptr;
}

bool grpc_executor_is_threaded() { return global_executor->IsThreaded(); }

void grpc_executor_set_threading(bool enable) {
  global_executor->SetThreading(enable);
}

grpc_closure_scheduler* grpc_executor_scheduler(GrpcExecutorJobType job_type) {
  return job_type == GRPC_EXECUTOR_SHORT ? &global_scheduler_short
                                         : &global_scheduler_long;
}
