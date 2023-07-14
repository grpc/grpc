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

static thread_local bool g_timer_thread;

namespace grpc_event_engine {
namespace experimental {

grpc_core::DebugOnlyTraceFlag grpc_event_engine_timer_trace(false, "timer");

void TimerManager::RunSomeTimers(
    std::vector<experimental::EventEngine::Closure*> timers) {
  for (auto* timer : timers) {
    thread_pool_->Run(timer);
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
  kicked_ = false;
  return true;
}

void TimerManager::MainLoop() {
  for (;;) {
    grpc_core::Timestamp next = grpc_core::Timestamp::InfFuture();
    absl::optional<std::vector<experimental::EventEngine::Closure*>>
        check_result = timer_list_->TimerCheck(&next);
    GPR_ASSERT(check_result.has_value() &&
               "ERROR: More than one MainLoop is running.");
    if (!check_result->empty()) {
      RunSomeTimers(std::move(*check_result));
      continue;
    }
    if (!WaitUntil(next)) break;
  }
  main_loop_exit_signal_->Notify();
}

bool TimerManager::IsTimerManagerThread() { return g_timer_thread; }

void TimerManager::StartMainLoopThread() {
  main_thread_ = grpc_core::Thread(
      "timer_manager",
      [](void* arg) {
        auto self = static_cast<TimerManager*>(arg);
        self->MainLoop();
      },
      this, nullptr,
      grpc_core::Thread::Options().set_tracked(false).set_joinable(false));
  main_thread_.Start();
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

void TimerManager::PrepareFork() {
  GRPC_FORK_TRACE_LOG("TimerManager::%p PrepareFork", this);
  Shutdown();
}
void TimerManager::PostforkParent() {
  GRPC_FORK_TRACE_LOG("TimerManager::%p PostforkParent", this);
  RestartPostFork();
}
void TimerManager::PostforkChild() {
  GRPC_FORK_TRACE_LOG("TimerManager::%p PostforkChild", this);
  RestartPostFork();
}

}  // namespace experimental
}  // namespace grpc_event_engine
