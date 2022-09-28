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

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_internal.h"

#define MAX_DEPTH 2

#define EXECUTOR_TRACE(format, ...)                       \
  do {                                                    \
    if (GRPC_TRACE_FLAG_ENABLED(executor_trace)) {        \
      gpr_log(GPR_INFO, "EXECUTOR " format, __VA_ARGS__); \
    }                                                     \
  } while (0)

#define EXECUTOR_TRACE0(str)                       \
  do {                                             \
    if (GRPC_TRACE_FLAG_ENABLED(executor_trace)) { \
      gpr_log(GPR_INFO, "EXECUTOR " str);          \
    }                                              \
  } while (0)

namespace grpc_core {
namespace {

thread_local ThreadState* g_this_thread_state;

Executor* executors[static_cast<size_t>(ExecutorType::NUM_EXECUTORS)];

void default_enqueue_short(grpc_closure* closure, grpc_error_handle error) {
  executors[static_cast<size_t>(ExecutorType::DEFAULT)]->Enqueue(
      closure, error, true /* is_short */);
}

void default_enqueue_long(grpc_closure* closure, grpc_error_handle error) {
  executors[static_cast<size_t>(ExecutorType::DEFAULT)]->Enqueue(
      closure, error, false /* is_short */);
}

void resolver_enqueue_short(grpc_closure* closure, grpc_error_handle error) {
  executors[static_cast<size_t>(ExecutorType::RESOLVER)]->Enqueue(
      closure, error, true /* is_short */);
}

void resolver_enqueue_long(grpc_closure* closure, grpc_error_handle error) {
  executors[static_cast<size_t>(ExecutorType::RESOLVER)]->Enqueue(
      closure, error, false /* is_short */);
}

using EnqueueFunc = void (*)(grpc_closure* closure, grpc_error_handle error);

const EnqueueFunc
    executor_enqueue_fns_[static_cast<size_t>(ExecutorType::NUM_EXECUTORS)]
                         [static_cast<size_t>(ExecutorJobType::NUM_JOB_TYPES)] =
                             {{default_enqueue_short, default_enqueue_long},
                              {resolver_enqueue_short, resolver_enqueue_long}};

}  // namespace

TraceFlag executor_trace(false, "executor");

Executor::Executor(const char* name) : name_(name) {
  adding_thread_lock_ = GPR_SPINLOCK_STATIC_INITIALIZER;
  gpr_atm_rel_store(&num_threads_, 0);
  max_threads_ = std::max(1u, 2 * gpr_cpu_num_cores());
}

void Executor::Init() { SetThreading(true); }

size_t Executor::RunClosures(const char* executor_name,
                             grpc_closure_list list) {
  size_t n = 0;

  // In the executor, the ExecCtx for the thread is declared in the executor
  // thread itself, but this is the point where we could start seeing
  // application-level callbacks. No need to create a new ExecCtx, though,
  // since there already is one and it is flushed (but not destructed) in this
  // function itself. The ApplicationCallbackExecCtx will have its callbacks
  // invoked on its destruction, which will be after completing any closures in
  // the executor's closure list (which were explicitly scheduled onto the
  // executor).
  ApplicationCallbackExecCtx callback_exec_ctx(
      GRPC_APP_CALLBACK_EXEC_CTX_FLAG_IS_INTERNAL_THREAD);

  grpc_closure* c = list.head;
  while (c != nullptr) {
    grpc_closure* next = c->next_data.next;
#ifndef NDEBUG
    EXECUTOR_TRACE("(%s) run %p [created by %s:%d]", executor_name, c,
                   c->file_created, c->line_created);
    c->scheduled = false;
#else
    EXECUTOR_TRACE("(%s) run %p", executor_name, c);
#endif
    grpc_error_handle error =
        internal::StatusMoveFromHeapPtr(c->error_data.error);
    c->error_data.error = 0;
    c->cb(c->cb_arg, std::move(error));
    c = next;
    n++;
    ExecCtx::Get()->Flush();
  }

  return n;
}

bool Executor::IsThreaded() const {
  return gpr_atm_acq_load(&num_threads_) > 0;
}

void Executor::SetThreading(bool threading) {
  gpr_atm curr_num_threads = gpr_atm_acq_load(&num_threads_);
  EXECUTOR_TRACE("(%s) SetThreading(%d) begin", name_, threading);

  if (threading) {
    if (curr_num_threads > 0) {
      EXECUTOR_TRACE("(%s) SetThreading(true). curr_num_threads > 0", name_);
      return;
    }

    GPR_ASSERT(num_threads_ == 0);
    gpr_atm_rel_store(&num_threads_, 1);
    thd_state_ = static_cast<ThreadState*>(
        gpr_zalloc(sizeof(ThreadState) * max_threads_));

    for (size_t i = 0; i < max_threads_; i++) {
      gpr_mu_init(&thd_state_[i].mu);
      gpr_cv_init(&thd_state_[i].cv);
      thd_state_[i].id = i;
      thd_state_[i].name = name_;
      thd_state_[i].thd = Thread();
      thd_state_[i].elems = GRPC_CLOSURE_LIST_INIT;
    }

    thd_state_[0].thd = Thread(name_, &Executor::ThreadMain, &thd_state_[0]);
    thd_state_[0].thd.Start();
  } else {  // !threading
    if (curr_num_threads == 0) {
      EXECUTOR_TRACE("(%s) SetThreading(false). curr_num_threads == 0", name_);
      return;
    }

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
      EXECUTOR_TRACE("(%s) Thread %" PRIdPTR " of %" PRIdPTR " joined", name_,
                     i + 1, curr_num_threads);
    }

    gpr_atm_rel_store(&num_threads_, 0);
    for (size_t i = 0; i < max_threads_; i++) {
      gpr_mu_destroy(&thd_state_[i].mu);
      gpr_cv_destroy(&thd_state_[i].cv);
      RunClosures(thd_state_[i].name, thd_state_[i].elems);
    }

    gpr_free(thd_state_);

    // grpc_iomgr_shutdown_background_closure() will close all the registered
    // fds in the background poller, and wait for all pending closures to
    // finish. Thus, never call Executor::SetThreading(false) in the middle of
    // an application.
    // TODO(guantaol): create another method to finish all the pending closures
    // registered in the background poller by Executor.
    grpc_iomgr_platform_shutdown_background_closure();
  }

  EXECUTOR_TRACE("(%s) SetThreading(%d) done", name_, threading);
}

void Executor::Shutdown() { SetThreading(false); }

void Executor::ThreadMain(void* arg) {
  ThreadState* ts = static_cast<ThreadState*>(arg);
  g_this_thread_state = ts;

  ExecCtx exec_ctx(GRPC_EXEC_CTX_FLAG_IS_INTERNAL_THREAD);

  size_t subtract_depth = 0;
  for (;;) {
    EXECUTOR_TRACE("(%s) [%" PRIdPTR "]: step (sub_depth=%" PRIdPTR ")",
                   ts->name, ts->id, subtract_depth);

    gpr_mu_lock(&ts->mu);
    ts->depth -= subtract_depth;
    // Wait for closures to be enqueued or for the executor to be shutdown
    while (grpc_closure_list_empty(ts->elems) && !ts->shutdown) {
      ts->queued_long_job = false;
      gpr_cv_wait(&ts->cv, &ts->mu, gpr_inf_future(GPR_CLOCK_MONOTONIC));
    }

    if (ts->shutdown) {
      EXECUTOR_TRACE("(%s) [%" PRIdPTR "]: shutdown", ts->name, ts->id);
      gpr_mu_unlock(&ts->mu);
      break;
    }

    grpc_closure_list closures = ts->elems;
    ts->elems = GRPC_CLOSURE_LIST_INIT;
    gpr_mu_unlock(&ts->mu);

    EXECUTOR_TRACE("(%s) [%" PRIdPTR "]: execute", ts->name, ts->id);

    ExecCtx::Get()->InvalidateNow();
    subtract_depth = RunClosures(ts->name, closures);
  }

  g_this_thread_state = nullptr;
}

void Executor::Enqueue(grpc_closure* closure, grpc_error_handle error,
                       bool is_short) {
  bool retry_push;

  do {
    retry_push = false;
    size_t cur_thread_count =
        static_cast<size_t>(gpr_atm_acq_load(&num_threads_));

    // If the number of threads is zero(i.e either the executor is not threaded
    // or already shutdown), then queue the closure on the exec context itself
    if (cur_thread_count == 0) {
#ifndef NDEBUG
      EXECUTOR_TRACE("(%s) schedule %p (created %s:%d) inline", name_, closure,
                     closure->file_created, closure->line_created);
#else
      EXECUTOR_TRACE("(%s) schedule %p inline", name_, closure);
#endif
      grpc_closure_list_append(ExecCtx::Get()->closure_list(), closure, error);
      return;
    }

    if (grpc_iomgr_platform_add_closure_to_background_poller(closure, error)) {
      return;
    }

    ThreadState* ts = g_this_thread_state;
    if (ts == nullptr) {
      ts = &thd_state_[HashPointer(ExecCtx::Get(), cur_thread_count)];
    }

    ThreadState* orig_ts = ts;
    bool try_new_thread = false;

    for (;;) {
#ifndef NDEBUG
      EXECUTOR_TRACE(
          "(%s) try to schedule %p (%s) (created %s:%d) to thread "
          "%" PRIdPTR,
          name_, closure, is_short ? "short" : "long", closure->file_created,
          closure->line_created, ts->id);
#else
      EXECUTOR_TRACE("(%s) try to schedule %p (%s) to thread %" PRIdPTR, name_,
                     closure, is_short ? "short" : "long", ts->id);
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
          // We cycled through all the threads. Retry enqueue again by creating
          // a new thread
          //
          // TODO (sreek): There is a potential issue here. We are
          // unconditionally setting try_new_thread to true here. What if the
          // executor is shutdown OR if cur_thread_count is already equal to
          // max_threads ?
          // (Fortunately, this is not an issue yet (as of july 2018) because
          // there is only one instance of long job in gRPC and hence we will
          // not hit this code path)
          retry_push = true;
          try_new_thread = true;
          break;
        }

        continue;  // Try the next thread-state
      }

      // == Found the thread state (i.e thread) to enqueue this closure! ==

      // Also, if this thread has been waiting for closures, wake it up.
      // - If grpc_closure_list_empty() is true and the Executor is not
      //   shutdown, it means that the thread must be waiting in ThreadMain()
      // - Note that gpr_cv_signal() won't immediately wakeup the thread. That
      //   happens after we release the mutex &ts->mu a few lines below
      if (grpc_closure_list_empty(ts->elems) && !ts->shutdown) {
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
      cur_thread_count = static_cast<size_t>(gpr_atm_acq_load(&num_threads_));
      if (cur_thread_count < max_threads_) {
        // Increment num_threads (safe to do a store instead of a cas because we
        // always increment num_threads under the 'adding_thread_lock')
        gpr_atm_rel_store(&num_threads_, cur_thread_count + 1);

        thd_state_[cur_thread_count].thd =
            Thread(name_, &Executor::ThreadMain, &thd_state_[cur_thread_count]);
        thd_state_[cur_thread_count].thd.Start();
      }
      gpr_spinlock_unlock(&adding_thread_lock_);
    }
  } while (retry_push);
}

// Executor::InitAll() and Executor::ShutdownAll() functions are called in the
// the grpc_init() and grpc_shutdown() code paths which are protected by a
// global mutex. So it is okay to assume that these functions are thread-safe
void Executor::InitAll() {
  EXECUTOR_TRACE0("Executor::InitAll() enter");

  // Return if Executor::InitAll() is already called earlier
  if (executors[static_cast<size_t>(ExecutorType::DEFAULT)] != nullptr) {
    GPR_ASSERT(executors[static_cast<size_t>(ExecutorType::RESOLVER)] !=
               nullptr);
    return;
  }

  executors[static_cast<size_t>(ExecutorType::DEFAULT)] =
      new Executor("default-executor");
  executors[static_cast<size_t>(ExecutorType::RESOLVER)] =
      new Executor("resolver-executor");

  executors[static_cast<size_t>(ExecutorType::DEFAULT)]->Init();
  executors[static_cast<size_t>(ExecutorType::RESOLVER)]->Init();

  EXECUTOR_TRACE0("Executor::InitAll() done");
}

void Executor::Run(grpc_closure* closure, grpc_error_handle error,
                   ExecutorType executor_type, ExecutorJobType job_type) {
  executor_enqueue_fns_[static_cast<size_t>(executor_type)]
                       [static_cast<size_t>(job_type)](closure, error);
}

void Executor::ShutdownAll() {
  EXECUTOR_TRACE0("Executor::ShutdownAll() enter");

  // Return if Executor:SshutdownAll() is already called earlier
  if (executors[static_cast<size_t>(ExecutorType::DEFAULT)] == nullptr) {
    GPR_ASSERT(executors[static_cast<size_t>(ExecutorType::RESOLVER)] ==
               nullptr);
    return;
  }

  executors[static_cast<size_t>(ExecutorType::DEFAULT)]->Shutdown();
  executors[static_cast<size_t>(ExecutorType::RESOLVER)]->Shutdown();

  // Delete the executor objects.
  //
  // NOTE: It is important to call Shutdown() on all executors first before
  // calling delete  because it is possible for one executor (that is not
  // shutdown yet) to call Enqueue() on a different executor which is already
  // shutdown. This is legal and in such cases, the Enqueue() operation
  // effectively "fails" and enqueues that closure on the calling thread's
  // exec_ctx.
  //
  // By ensuring that all executors are shutdown first, we are also ensuring
  // that no thread is active across all executors.

  delete executors[static_cast<size_t>(ExecutorType::DEFAULT)];
  delete executors[static_cast<size_t>(ExecutorType::RESOLVER)];
  executors[static_cast<size_t>(ExecutorType::DEFAULT)] = nullptr;
  executors[static_cast<size_t>(ExecutorType::RESOLVER)] = nullptr;

  EXECUTOR_TRACE0("Executor::ShutdownAll() done");
}

bool Executor::IsThreaded(ExecutorType executor_type) {
  GPR_ASSERT(executor_type < ExecutorType::NUM_EXECUTORS);
  return executors[static_cast<size_t>(executor_type)]->IsThreaded();
}

bool Executor::IsThreadedDefault() {
  return Executor::IsThreaded(ExecutorType::DEFAULT);
}

void Executor::SetThreadingAll(bool enable) {
  EXECUTOR_TRACE("Executor::SetThreadingAll(%d) called", enable);
  for (size_t i = 0; i < static_cast<size_t>(ExecutorType::NUM_EXECUTORS);
       i++) {
    executors[i]->SetThreading(enable);
  }
}

void Executor::SetThreadingDefault(bool enable) {
  EXECUTOR_TRACE("Executor::SetThreadingDefault(%d) called", enable);
  executors[static_cast<size_t>(ExecutorType::DEFAULT)]->SetThreading(enable);
}

}  // namespace grpc_core
