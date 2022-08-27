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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TIMER_MANAGER_H
#define GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TIMER_MANAGER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/forkable.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace posix_engine {

// Timer Manager tries to keep only one thread waiting for the next timeout at
// all times, and thus effectively preventing the thundering herd problem.
// TODO(ctiller): consider unifying this thread pool and the one in
// thread_pool.{h,cc}.
class TimerManager final : public grpc_event_engine::experimental::Forkable {
 public:
  TimerManager();
  ~TimerManager() override;

  grpc_core::Timestamp Now() { return host_.Now(); }

  void TimerInit(Timer* timer, grpc_core::Timestamp deadline,
                 experimental::EventEngine::Closure* closure);
  bool TimerCancel(Timer* timer);

  // Forkable
  void PrepareFork() override;
  void PostforkParent() override;
  void PostforkChild() override;

  static bool IsTimerManagerThread();

 private:
  struct RunThreadArgs {
    TimerManager* self;
    grpc_core::Thread thread;
  };

  class Host final : public TimerListHost {
   public:
    explicit Host(TimerManager* timer_manager)
        : timer_manager_(timer_manager) {}

    void Kick() override;
    grpc_core::Timestamp Now() override;

   private:
    TimerManager* const timer_manager_;
  };

  void StartThread() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  static void RunThread(void* arg);
  void Run(grpc_core::Thread thread);
  void MainLoop();
  void RunSomeTimers(std::vector<experimental::EventEngine::Closure*> timers);
  bool WaitUntil(grpc_core::Timestamp next);
  void Kick();

  grpc_core::Mutex mu_;
  // Condvar associated with decrementing the thread count.
  // Threads will signal this when thread count reaches zero, and the forking
  // code *or* the destructor will wait upon it.
  grpc_core::CondVar cv_threadcount_;
  // Condvar associated with threads waiting to wakeup and work.
  // Threads wait on this until either a timeout is reached or another thread is
  // needed to wait for a timeout.
  // On shutdown we SignalAll against this to wake up all threads and have them
  // finish.
  // On kick we Signal against this to wake up at least one thread (but not
  // all)! Similarly when we note that no thread is watching timers.
  //
  // This is a different condvar than cv_threadcount_!
  // If this were the same:
  // - thread exits would require a SignalAll to ensure that the specific thread
  //   we want to wake is woken up.
  // - kicks would need to signal all threads to avoid having the kick absorbed
  //   by a shutdown thread and cause a deadlock, leading to thundering herd
  //   problems in the common case.
  grpc_core::CondVar cv_wait_;
  Host host_;
  // number of threads in the system
  size_t thread_count_ ABSL_GUARDED_BY(mu_) = 0;
  // number of threads sitting around waiting
  size_t waiter_count_ ABSL_GUARDED_BY(mu_) = 0;
  // Threads waiting to be joined
  std::vector<grpc_core::Thread> completed_threads_ ABSL_GUARDED_BY(mu_);
  // is there a thread waiting until the next timer should fire?
  bool has_timed_waiter_ ABSL_GUARDED_BY(mu_) = false;
  // are we shutting down?
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;
  // are we forking?
  bool forking_ ABSL_GUARDED_BY(mu_) = false;
  // are we shutting down?
  bool kicked_ ABSL_GUARDED_BY(mu_) = false;
  // the deadline of the current timed waiter thread (only relevant if
  // has_timed_waiter_ is true)
  grpc_core::Timestamp timed_waiter_deadline_ ABSL_GUARDED_BY(mu_);
  // generation counter to track which thread is waiting for the next timer
  uint64_t timed_waiter_generation_ ABSL_GUARDED_BY(mu_) = 0;
  // number of timer wakeups
  uint64_t wakeups_ ABSL_GUARDED_BY(mu_) = 0;
  // actual timer implementation
  std::unique_ptr<TimerList> timer_list_;
  int prefork_thread_count_ = 0;
};

}  // namespace posix_engine
}  // namespace grpc_event_engine

#endif /* GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TIMER_MANAGER_H */
