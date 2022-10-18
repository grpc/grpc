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

#include <atomic>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/event_engine/thread_pool.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace posix_engine {

// Timer Manager tries to keep only one thread waiting for the next timeout at
// all times, and thus effectively preventing the thundering herd problem.
// TODO(ctiller): consider unifying this thread pool and the one in
// thread_pool.{h,cc}.
class TimerManager final {
 public:
  explicit TimerManager(
      std::shared_ptr<grpc_event_engine::experimental::ThreadPool> thread_pool);
  ~TimerManager();

  grpc_core::Timestamp Now() { return host_.Now(); }

  void TimerInit(Timer* timer, grpc_core::Timestamp deadline,
                 experimental::EventEngine::Closure* closure);
  bool TimerCancel(Timer* timer);

  static bool IsTimerManagerThread();

 private:
  class Host final : public TimerListHost {
   public:
    explicit Host(TimerManager* timer_manager)
        : timer_manager_(timer_manager) {}

    void Kick() override;
    grpc_core::Timestamp Now() override;

   private:
    TimerManager* const timer_manager_;
  };

  void MainLoop();
  void RunSomeTimers(std::vector<experimental::EventEngine::Closure*> timers);
  bool WaitUntil(grpc_core::Timestamp next);
  void Kick();

  grpc_core::Mutex mu_;
  // Condvar associated with the main thread waiting to wakeup and work.
  // Threads wait on this until either a timeout is reached or the timer manager
  // is kicked. On shutdown we Signal against this to wake up all threads and
  // have them finish. On kick we Signal against this to wake up the main
  // thread.
  grpc_core::CondVar cv_wait_;
  Host host_;
  // are we shutting down?
  std::atomic_bool shutdown_{false};
  // are we shutting down?
  std::atomic_bool kicked_{false};
  // number of timer wakeups
  std::atomic_uint64_t wakeups_{0};
  // actual timer implementation
  std::unique_ptr<TimerList> timer_list_;
  std::shared_ptr<grpc_event_engine::experimental::ThreadPool> thread_pool_;
  grpc_core::Notification main_loop_exit_signal_;
};

}  // namespace posix_engine
}  // namespace grpc_event_engine

#endif /* GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TIMER_MANAGER_H */
