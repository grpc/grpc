// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/channel_idle/idle_filter_state.h"

#include <assert.h>

namespace grpc_core {

IdleFilterState::IdleFilterState(bool start_timer)
    : state_(start_timer ? kTimerStarted : 0) {}

bool IdleFilterState::IncreaseCallCount() {
  uintptr_t state = state_.load(std::memory_order_relaxed);
  uintptr_t new_state;
  do {
    if (state & kExpiredTimer) return false;
    // Increment the counter, and flag that there's been activity.
    new_state = state;
    new_state |= kCallsStartedSinceLastTimerCheck;
    new_state += kCallIncrement;
  } while (!state_.compare_exchange_weak(
      state, new_state, std::memory_order_acq_rel, std::memory_order_relaxed));
  return true;
}

bool IdleFilterState::DecreaseCallCount() {
  uintptr_t state = state_.load(std::memory_order_relaxed);
  uintptr_t new_state;
  bool start_timer;
  do {
    start_timer = false;
    new_state = state;
    // Decrement call count (and assert there's at least one call outstanding!)
    assert(new_state >= kCallIncrement);
    new_state -= kCallIncrement;
    // If that decrement reaches a call count of zero and we have not started a
    // timer
    if ((new_state >> kCallsInProgressShift) == 0 &&
        (new_state & kTimerStarted) == 0) {
      // Flag that we will start a timer, and mark it started so nobody else
      // does.
      start_timer = true;
      new_state |= kTimerStarted;
      new_state &= ~(1 << kCallsInProgressShift);
    }
  } while (!state_.compare_exchange_weak(
      state, new_state, std::memory_order_acq_rel, std::memory_order_relaxed));
  return start_timer;
}

bool IdleFilterState::CheckTimer() {
  uintptr_t state = state_.load(std::memory_order_relaxed);
  uintptr_t new_state;
  bool start_timer;
  do {
    if ((state >> kCallsInProgressShift) != 0) {
      // Still calls in progress: nothing needs updating, just return
      // and keep the timer going!
      return true;
    }
    new_state = state;
    bool is_active = false;
    if (new_state & kCallsStartedSinceLastTimerCheck) {
      // If any calls started since the last time we checked, then consider the
      // channel still active and try again.
      is_active = true;
      new_state &= ~kCallsStartedSinceLastTimerCheck;
    }
    if (is_active) {
      // If we are still active, we should signal that the timer should start
      // again.
      start_timer = true;
    } else {
      // Otherwise, we should not start the timer again, and we should signal
      // that in the updated state.
      start_timer = false;
      new_state &= ~kTimerStarted;
      new_state |= kExpiredTimer;
    }
  } while (!state_.compare_exchange_weak(
      state, new_state, std::memory_order_acq_rel, std::memory_order_relaxed));
  return start_timer;
}

}  // namespace grpc_core
