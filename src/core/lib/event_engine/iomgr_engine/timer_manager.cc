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

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/iomgr_engine/timer_manager.h"

#include <inttypes.h>

#include "timer.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/timer.h"

namespace grpc_event_engine {
namespace iomgr_engine {

TimerManager::ThreadCollector::~ThreadCollector() {
  for (auto& t : threads_) t.Join();
}

void TimerManager::StartThread() {
  ++waiter_count_;
  ++thread_count_;
  auto* thread = new RunThreadArgs();
  thread->self = this;
  thread->thread =
      grpc_core::Thread("timer_manager", &TimerManager::RunThread, thread);
  thread->thread.Start();
}

void TimerManager::RunSomeTimers() {
  // if there's something to execute...
  ThreadCollector collector;
  {
    grpc_core::MutexLock lock(&mu_);
    // remove a waiter from the pool, and start another thread if necessary
    --waiter_count_;
    if (waiter_count_ == 0) {
      // The number of timer threads is always increasing until all the threads
      // are stopped. In rare cases, if a large number of timers fire
      // simultaneously, we may end up using a large number of threads.
      StartThread();
    } else {
      // if there's no thread waiting with a timeout, kick an existing untimed
      // waiter so that the next deadline is not missed
      if (!has_timed_waiter_) {
        cv_.Signal();
      }
    }
  }
  {
    grpc_core::MutexLock lock(&mu_);
    collector.Collect(std::move(completed_threads_));
    // get ready to wait again
    ++waiter_count_;
  }
}

// wait until 'next' (or forever if there is already a timed waiter in the pool)
// returns true if the thread should continue executing (false if it should
// shutdown)
bool TimerManager::WaitUntil(grpc_core::Timestamp next) {
  grpc_core::MutexLock lock(&mu_);

  if (shutdown_) {
    return false;
  }

  // If g_kicked is true at this point, it means there was a kick from the timer
  // system that the timer-manager threads here missed. We cannot trust 'next'
  // here any longer (since there might be an earlier deadline). So if g_kicked
  // is true at this point, we should quickly exit this and get the next
  // deadline from the timer system

  if (!kicked_) {
    // if there's no timed waiter, we should become one: that waiter waits
    // only until the next timer should expire. All other timers wait forever
    //
    // 'g_timed_waiter_generation' is a global generation counter. The idea here
    // is that the thread becoming a timed-waiter increments and stores this
    // global counter locally in 'my_timed_waiter_generation' before going to
    // sleep. After waking up, if my_timed_waiter_generation ==
    // g_timed_waiter_generation, it can be sure that it was the timed_waiter
    // thread (and that no other thread took over while this was asleep)
    //
    // Initialize my_timed_waiter_generation to some value that is NOT equal to
    // g_timed_waiter_generation
    uint64_t my_timed_waiter_generation = timed_waiter_generation_ - 1;

    /* If there's no timed waiter, we should become one: that waiter waits only
       until the next timer should expire. All other timer threads wait forever
       unless their 'next' is earlier than the current timed-waiter's deadline
       (in which case the thread with earlier 'next' takes over as the new timed
       waiter) */
    if (next != grpc_core::Timestamp::InfFuture()) {
      if (!has_timed_waiter_ || (next < timed_waiter_deadline_)) {
        my_timed_waiter_generation = ++timed_waiter_generation_;
        has_timed_waiter_ = true;
        timed_waiter_deadline_ = next;
      } else {  // g_timed_waiter == true && next >= g_timed_waiter_deadline
        next = grpc_core::Timestamp::InfFuture();
      }
    }

    cv_.WaitWithTimeout(&mu_, absl::Milliseconds((next - Now()).millis()));

    // if this was the timed waiter, then we need to check timers, and flag
    // that there's now no timed waiter... we'll look for a replacement if
    // there's work to do after checking timers (code above)
    if (my_timed_waiter_generation == timed_waiter_generation_) {
      ++wakeups_;
      has_timed_waiter_ = false;
      timed_waiter_deadline_ = grpc_core::Timestamp::InfFuture();
    }
  }

  kicked_ = false;

  return true;
}

void TimerManager::MainLoop() {
  for (;;) {
    grpc_core::Timestamp next = grpc_core::Timestamp::InfFuture();

    // check timer state, updates next to the next time to run a check
    switch (TimerCheck(&next)) {
      case TimerCheckResult::kFired:
        RunSomeTimers();
        break;
      case TimerCheckResult::kNotChecked:
        /* This case only happens under contention, meaning more than one timer
           manager thread checked timers concurrently.

           If that happens, we're guaranteed that some other thread has just
           checked timers, and this will avalanche into some other thread seeing
           empty timers and doing a timed sleep.

           Consequently, we can just sleep forever here and be happy at some
           saved wakeup cycles. */
        next = grpc_core::Timestamp::InfFuture();
        ABSL_FALLTHROUGH_INTENDED;
      case TimerCheckResult::kCheckedAndEmpty:
        if (!WaitUntil(next)) {
          return;
        }
        break;
    }
  }
}

static void timer_thread_cleanup(completed_thread* ct) {
  gpr_mu_lock(&g_mu);
  // terminate the thread: drop the waiter count, thread count, and let whomever
  // stopped the threading stuff know that we're done
  --g_waiter_count;
  --g_thread_count;
  if (0 == g_thread_count) {
    gpr_cv_signal(&g_cv_shutdown);
  }
  ct->next = g_completed_threads;
  g_completed_threads = ct;
  gpr_mu_unlock(&g_mu);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_timer_check_trace)) {
    gpr_log(GPR_INFO, "End timer thread");
  }
}

static void timer_thread(void* completed_thread_ptr) {
  // this threads exec_ctx: we try to run things through to completion here
  // since it's easy to spin up new threads
  grpc_core::ExecCtx exec_ctx(GRPC_EXEC_CTX_FLAG_IS_INTERNAL_THREAD);
  timer_main_loop();

  timer_thread_cleanup(static_cast<completed_thread*>(completed_thread_ptr));
}

TimerManager::TimerManager() {
  grpc_core::MutexLock lock(&mu_);
  StartThread();
}

TimerManager::~TimerManager() {
  {
    grpc_core::MutexLock lock(&mu_);
    shutdown_ = true;
    cv_.SignalAll();
  }
  while (true) {
    ThreadCollector collector;
    grpc_core::MutexLock lock(&mu_);
    collector.Collect(std::move(threads));
    if (thread_count_ == 0) break;
    cv_.Wait(&mu_);
  }
}

void TimerManager::Kick() {
  grpc_core::MutexLock lock(&mu_);
  has_timed_waiter_ = false;
  timed_waiter_deadline_ = grpc_core::Timestamp::InfFuture();
  ++timed_waiter_generation_;
  kicked_ = true;
  cv_.Signal();
}

uint64_t grpc_timer_manager_get_wakeups_testonly(void) { return g_wakeups; }

}  // namespace iomgr_engine
}  // namespace grpc_event_engine
