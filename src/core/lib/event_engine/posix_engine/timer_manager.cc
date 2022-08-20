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

#include "src/core/lib/event_engine/posix_engine/timer_manager.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"

#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/thd.h"

namespace grpc_event_engine {
namespace posix_engine {

namespace {
class ThreadCollector {
 public:
  ThreadCollector() = default;
  ~ThreadCollector();

  void Collect(std::vector<grpc_core::Thread> threads) {
    GPR_ASSERT(threads_.empty());
    threads_ = std::move(threads);
  }

 private:
  std::vector<grpc_core::Thread> threads_;
};

ThreadCollector::~ThreadCollector() {
  for (auto& t : threads_) t.Join();
}
}  // namespace

void TimerManager::StartThread() {
  ++waiter_count_;
  ++thread_count_;
  auto* thread = new RunThreadArgs();
  thread->self = this;
  thread->thread = grpc_core::Thread(
      "timer_manager", &TimerManager::RunThread, thread, nullptr,
      grpc_core::Thread::Options().set_tracked(false));
  thread->thread.Start();
}

void TimerManager::RunSomeTimers(
    std::vector<experimental::EventEngine::Closure*> timers) {
  // if there's something to execute...
  ThreadCollector collector;
  {
    grpc_core::MutexLock lock(&mu_);
    if (shutdown_ || forking_) return;
    // remove a waiter from the pool, and start another thread if necessary
    --waiter_count_;
    if (waiter_count_ == 0) {
      // The number of timer threads is always increasing until all the threads
      // are stopped, with the exception that all threads are shut down on fork
      // events. In rare cases, if a large number of timers fire simultaneously,
      // we may end up using a large number of threads.
      // TODO(ctiller): We could avoid this by exiting threads in WaitUntil().
      StartThread();
    } else {
      // if there's no thread waiting with a timeout, kick an existing untimed
      // waiter so that the next deadline is not missed
      if (!has_timed_waiter_) {
        cv_.Signal();
      }
    }
  }
  for (auto* timer : timers) {
    timer->Run();
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

  if (shutdown_) return false;
  if (forking_) return false;

  // TODO(ctiller): if there are too many waiting threads, this would be a good
  // place to exit the current thread.

  // If kicked_ is true at this point, it means there was a kick from the timer
  // system that the timer-manager threads here missed. We cannot trust 'next'
  // here any longer (since there might be an earlier deadline). So if kicked_
  // is true at this point, we should quickly exit this and get the next
  // deadline from the timer system

  if (!kicked_) {
    // if there's no timed waiter, we should become one: that waiter waits
    // only until the next timer should expire. All other timers wait forever
    //
    // 'timed_waiter_generation_' is a global generation counter. The idea here
    // is that the thread becoming a timed-waiter increments and stores this
    // global counter locally in 'my_timed_waiter_generation' before going to
    // sleep. After waking up, if my_timed_waiter_generation ==
    // timed_waiter_generation_, it can be sure that it was the timed_waiter
    // thread (and that no other thread took over while this was asleep)
    //
    // Initialize my_timed_waiter_generation to some value that is NOT equal to
    // timed_waiter_generation_
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
      } else {  // timed_waiter_ == true && next >= timed_waiter_deadline_
        next = grpc_core::Timestamp::InfFuture();
      }
    }

    cv_.WaitWithTimeout(&mu_,
                        absl::Milliseconds((next - host_.Now()).millis()));

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
    absl::optional<std::vector<experimental::EventEngine::Closure*>>
        check_result = timer_list_->TimerCheck(&next);
    if (check_result.has_value()) {
      if (!check_result->empty()) {
        RunSomeTimers(std::move(*check_result));
        continue;
      }
    } else {
      /* This case only happens under contention, meaning more than one timer
         manager thread checked timers concurrently.

         If that happens, we're guaranteed that some other thread has just
         checked timers, and this will avalanche into some other thread seeing
         empty timers and doing a timed sleep.

         Consequently, we can just sleep forever here and be happy at some
         saved wakeup cycles. */
      next = grpc_core::Timestamp::InfFuture();
    }
    if (!WaitUntil(next)) return;
  }
}

void TimerManager::RunThread(void* arg) {
  std::unique_ptr<RunThreadArgs> thread(static_cast<RunThreadArgs*>(arg));
  thread->self->MainLoop();
  {
    grpc_core::MutexLock lock(&thread->self->mu_);
    thread->self->thread_count_--;
    thread->self->completed_threads_.push_back(std::move(thread->thread));
  }
  thread->self->cv_.Signal();
}

TimerManager::TimerManager() : host_(this) {
  timer_list_ = absl::make_unique<TimerList>(&host_);
  grpc_core::MutexLock lock(&mu_);
  StartThread();
}

grpc_core::Timestamp TimerManager::Host::Now() {
  return grpc_core::Timestamp::FromTimespecRoundDown(
      gpr_now(GPR_CLOCK_MONOTONIC));
}

void TimerManager::TimerInit(Timer* timer, grpc_core::Timestamp deadline,
                             experimental::EventEngine::Closure* closure) {
  timer_list_->TimerInit(timer, deadline, closure);
}

bool TimerManager::TimerCancel(Timer* timer) {
  return timer_list_->TimerCancel(timer);
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
    collector.Collect(std::move(completed_threads_));
    if (thread_count_ == 0) break;
    cv_.Wait(&mu_);
  }
}

void TimerManager::Host::Kick() { timer_manager_->Kick(); }

void TimerManager::Kick() {
  grpc_core::MutexLock lock(&mu_);
  has_timed_waiter_ = false;
  timed_waiter_deadline_ = grpc_core::Timestamp::InfFuture();
  ++timed_waiter_generation_;
  kicked_ = true;
  cv_.Signal();
}

void TimerManager::PrepareFork() {
  {
    grpc_core::MutexLock lock(&mu_);
    forking_ = true;
    prefork_thread_count_ = thread_count_;
    cv_.SignalAll();
  }
  while (true) {
    grpc_core::MutexLock lock(&mu_);
    ThreadCollector collector;
    collector.Collect(std::move(completed_threads_));
    if (thread_count_ == 0) break;
    cv_.Wait(&mu_);
  }
}

void TimerManager::PostforkParent() {
  grpc_core::MutexLock lock(&mu_);
  for (int i = 0; i < prefork_thread_count_; i++) {
    StartThread();
  }
  prefork_thread_count_ = 0;
  forking_ = false;
}

void TimerManager::PostforkChild() {
  grpc_core::MutexLock lock(&mu_);
  for (int i = 0; i < prefork_thread_count_; i++) {
    StartThread();
  }
  prefork_thread_count_ = 0;
  forking_ = false;
}

}  // namespace posix_engine
}  // namespace grpc_event_engine
