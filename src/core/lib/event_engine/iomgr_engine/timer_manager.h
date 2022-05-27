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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_TIMER_MANAGER_H
#define GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_TIMER_MANAGER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/iomgr_engine/timer.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace iomgr_engine {

/* Timer Manager tries to keep only one thread waiting for the next timeout at
   all times, and thus effectively preventing the thundering herd problem. */
class TimerManager final : public TimerList {
 public:
  TimerManager();
  ~TimerManager();

  void Kick() override;

 private:
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

  struct RunThreadArgs {
    TimerManager* self;
    grpc_core::Thread thread;
  };

  void StartThread() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  static void RunThread(void* arg);
  void MainLoop();
  void RunSomeTimers(std::vector<experimental::EventEngine::Closure*> timers);
  bool WaitUntil(grpc_core::Timestamp next);

  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
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
  // are we shutting down?
  bool kicked_ ABSL_GUARDED_BY(mu_) = false;
  // the deadline of the current timed waiter thread (only relevant if
  // g_has_timed_waiter is true)
  grpc_core::Timestamp timed_waiter_deadline_ ABSL_GUARDED_BY(mu_);
  // generation counter to track which thread is waiting for the next timer
  uint64_t timed_waiter_generation_ ABSL_GUARDED_BY(mu_) = 0;
  // number of timer wakeups
  uint64_t wakeups_ ABSL_GUARDED_BY(mu_) = 0;
};

}  // namespace iomgr_engine
}  // namespace grpc_event_engine

#endif /* GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_TIMER_MANAGER_H */
