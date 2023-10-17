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

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/posix_engine/timer_manager.h"

#include <memory>
#include <utility>

#include "absl/time/time.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/thd.h"

namespace grpc_event_engine {
namespace experimental {

grpc_core::DebugOnlyTraceFlag grpc_event_engine_timer_trace(false, "timer");

void TimerManager::RunSomeTimers(
    std::vector<experimental::EventEngine::Closure*> timers) {
  class BenchmarkingClosure final : public EventEngine::Closure {
   public:
    explicit BenchmarkingClosure(EventEngine::Closure* inner) : inner_(inner) {}
    void Run() override {
      auto delay = grpc_core::Timestamp::Now() - creation_;
      if (delay > grpc_core::Duration::Seconds(1)) {
        gpr_log(GPR_ERROR, "BenchmarkingClosure: delay=%s",
                delay.ToString().c_str());
      }
      inner_->Run();
      delete this;
    }

   private:
    grpc_core::Timestamp creation_ = grpc_core::Timestamp::Now();
    EventEngine::Closure* const inner_;
  };
  for (auto* timer : timers) {
    thread_pool_->Run(new BenchmarkingClosure(timer));
  }
}

// wait until 'next' (or forever if there is already a timed waiter in the pool)
// returns true if the thread should continue executing (false if it should
// shutdown)
bool TimerManager::WaitUntil(grpc_core::Timestamp next) {
  grpc_core::MutexLock lock(&mu_);
  if (shutdown_) return false;
  // If kicked_ is true at this point, it means there was a kick from the timer
  // system that the timer-manager threads here missed. We cannot trust 'next'
  // here any longer (since there might be an earlier deadline). So if kicked_
  // is true at this point, we should quickly exit this and get the next
  // deadline from the timer system
  if (!kicked_) {
    cv_wait_.WaitWithTimeout(&mu_,
                             absl::Milliseconds((next - host_.Now()).millis()));
    ++wakeups_;
  }
  auto delay = grpc_core::Timestamp::Now() - next;
  if (delay > grpc_core::Duration::Milliseconds(10)) {
    gpr_log(GPR_ERROR, "WaitUntil: delay=%s", delay.ToString().c_str());
  }
  kicked_ = false;
  return true;
}

void TimerManager::Main() {
  GPR_ASSERT(!grpc_core::Timestamp::NowComesFromCache());
  grpc_core::Timestamp next = grpc_core::Timestamp::InfFuture();
  absl::optional<std::vector<experimental::EventEngine::Closure*>>
      check_result = timer_list_->TimerCheck(&next);
  GPR_ASSERT(check_result.has_value() &&
             "ERROR: More than one MainLoop is running.");
  thread_pool_->Run([this, next]() {
    if (!WaitUntil(next)) {
      main_loop_exit_signal_->Notify();
      return;
    }
    Main();
  });
  if (!check_result->empty()) {
    RunSomeTimers(std::move(*check_result));
  }
}

bool TimerManager::IsTimerManagerThread() { return false; }

void TimerManager::StartMainLoopThread() {
  thread_pool_->Run([this]() { Main(); });
}

TimerManager::TimerManager(
    std::shared_ptr<grpc_event_engine::experimental::ThreadPool> thread_pool)
    : host_(this), thread_pool_(std::move(thread_pool)) {
  timer_list_ = std::make_unique<TimerList>(&host_);
  main_loop_exit_signal_.emplace();
  StartMainLoopThread();
}

grpc_core::Timestamp TimerManager::Host::Now() {
  return grpc_core::Timestamp::FromTimespecRoundDown(
      gpr_now(GPR_CLOCK_MONOTONIC));
}

void TimerManager::TimerInit(Timer* timer, grpc_core::Timestamp deadline,
                             experimental::EventEngine::Closure* closure) {
  if (grpc_event_engine_timer_trace.enabled()) {
    grpc_core::MutexLock lock(&mu_);
    if (shutdown_) {
      gpr_log(GPR_ERROR,
              "WARNING: TimerManager::%p: scheduling Closure::%p after "
              "TimerManager has been shut down.",
              this, closure);
    }
  }
  timer_list_->TimerInit(timer, deadline, closure);
}

bool TimerManager::TimerCancel(Timer* timer) {
  return timer_list_->TimerCancel(timer);
}

void TimerManager::Shutdown() {
  {
    grpc_core::MutexLock lock(&mu_);
    if (shutdown_) return;
    if (grpc_event_engine_timer_trace.enabled()) {
      gpr_log(GPR_DEBUG, "TimerManager::%p shutting down", this);
    }
    shutdown_ = true;
    // Wait on the main loop to exit.
    cv_wait_.Signal();
  }
  main_loop_exit_signal_->WaitForNotification();
  if (grpc_event_engine_timer_trace.enabled()) {
    gpr_log(GPR_DEBUG, "TimerManager::%p shutdown complete", this);
  }
}

TimerManager::~TimerManager() { Shutdown(); }

void TimerManager::Host::Kick() { timer_manager_->Kick(); }

void TimerManager::Kick() {
  grpc_core::MutexLock lock(&mu_);
  kicked_ = true;
  cv_wait_.Signal();
}

void TimerManager::RestartPostFork() {
  grpc_core::MutexLock lock(&mu_);
  GPR_ASSERT(GPR_LIKELY(shutdown_));
  if (grpc_event_engine_timer_trace.enabled()) {
    gpr_log(GPR_DEBUG, "TimerManager::%p restarting after shutdown", this);
  }
  shutdown_ = false;
  main_loop_exit_signal_.emplace();
  StartMainLoopThread();
}

void TimerManager::PrepareFork() { Shutdown(); }
void TimerManager::PostforkParent() { RestartPostFork(); }
void TimerManager::PostforkChild() { RestartPostFork(); }

}  // namespace experimental
}  // namespace grpc_event_engine
