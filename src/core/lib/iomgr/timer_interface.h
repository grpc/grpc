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

#ifndef GRPC_CORE_LIB_IOMGR_TIMER_H
#define GRPC_CORE_LIB_IOMGR_TIMER_H

#include "src/core/lib/iomgr/port.h"

#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include <type_traits>
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr.h"

namespace grpc_core {

// Storage type for Timer: see comment on Timer declaration below
static constexpr const size_t kMaxTimerSize = 6 * sizeof(void*);
typedef std::aligned_storage<kMaxTimerSize>::type TimerStorage;

// Abstract singleton interface for implementing timer engines
class TimerEngine {
 public:
  // There's a single global TimerEngine
  static TimerEngine* Get();

  // Initialize *timer. When expired or canceled, closure will be called with
  // error set to indicate if it expired (GRPC_ERROR_NONE) or was canceled
  // (GRPC_ERROR_CANCELLED). timer_cb is guaranteed to be called exactly once,
  // and application code should check the error to determine how it was
  // invoked. The application callback is also responsible for maintaining
  // information about when to free up any user-level state
  virtual void Init(grpc_exec_ctx* exec_ctx, TimerStorage* timer,
                    grpc_millis deadline, grpc_closure* on_complete) = 0;

  // Note that there is no timer destroy function. This is because the
  // timer is a one-time occurrence with a guarantee that the callback will
  // be called exactly once, either at expiration or cancellation. Thus, all
  // the internal timer event management state is destroyed just before
  // that callback is invoked. If the user has additional state associated with
  // the timer, the user is responsible for determining when it is safe to
  // destroy that state.

  // Cancel a timer.
  // There are three cases:
  // 1. We normally cancel the timer
  // 2. The timer has already run
  // 3. We can't cancel the timer because it is "in flight".
  //
  // In all of these cases, the cancellation is still considered successful.
  // They are essentially distinguished in that the timer_cb will be run
  // exactly once from either the cancellation (with error GRPC_ERROR_CANCELLED)
  // or from the activation (with error GRPC_ERROR_NONE).
  //
  // Note carefully that the callback function MAY occur in the same callstack
  // as grpc_timer_cancel. It's expected that most timers will be cancelled
  // (their primary use is to implement deadlines), and so this code is
  // optimized such that cancellation costs as little as possible. Making
  // callbacks run inline matches this aim.
  //
  // Requires: cancel() must happen after init() on a given timer
  virtual void Cancel(grpc_exec_ctx* exec_ctx, TimerStorage* timer) = 0;

  // Check for timers to be run, and run them.
  // Return FIRED if timer callbacks were executed, CHECKED_AND_EMPTY if timers
  // were checked but none were ready, and NOT_CHECKED if checks were skipped.
  // If next is non-null, TRY to update *next with the next running timer IF
  // that timer occurs before *next current value. *next is never guaranteed to
  // be updated on any given execution; however, with high probability at least
  // one thread in the system will see an update at any time slice.
  enum class CheckResult { NOT_CHECKED, CHECKED_AND_EMPTY, FIRED };
  virtual void CheckTimers(grpc_exec_ctx* exec_ctx, grpc_millis* next) = 0;

  // Consume a kick received by TimerManager::Kick
  virtual void ConsumeKick() = 0;
};

// Concrete Timer instance class. To allow this type to avoid memory allocation
// even though the TimerEngine is abstract, we pre-size some storage memory and
// just pass that through (via inlined functions) to the TimerEngine
// implementation that's in use.
class Timer {
 public:
  Timer(grpc_exec_ctx* exec_ctx, grpc_millis deadline,
        grpc_closure* on_complete) {
    TimerEngine::Get()->Init(exec_ctx, &storage_, deadline, on_complete);
  }

  ~Timer() {}

  void Cancel(grpc_exec_ctx* exec_ctx) {
    TimerEngine::Get()->Cancel(exec_ctx, &storage_);
  }

 private:
  TimerStorage storage_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_TIMER_H */
