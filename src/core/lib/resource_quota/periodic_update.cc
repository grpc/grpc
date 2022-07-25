// Copyright 2022 gRPC authors.
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

#include "src/core/lib/resource_quota/periodic_update.h"

#include <atomic>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

bool PeriodicUpdate::MaybeEndPeriod(absl::FunctionRef<void(Duration)> f) {
  if (period_start_ == Timestamp::ProcessEpoch()) {
    period_start_ = ExecCtx::Get()->Now();
    updates_remaining_.store(1, std::memory_order_release);
    return false;
  }
  // updates_remaining_ just reached 0 and the thread calling this function was
  // the decrementer that got us there.
  // We can now safely mutate any non-atomic mutable variables (we've got a
  // guarantee that no other thread will), and by the time this function returns
  // we must store a postive number into updates_remaining_.
  auto now = ExecCtx::Get()->Now();
  Duration time_so_far = now - period_start_;
  if (time_so_far < period_) {
    // At most double the number of updates remaining until the next period.
    // At least try to estimate when we'll reach it.
    int64_t better_guess;
    if (time_so_far.millis() == 0) {
      better_guess = expected_updates_per_period_ * 2;
    } else {
      // Determine a scaling factor that would have gotten us to the next
      // period, but clamp between 1.01 (at least 1% increase in guesses)
      // and 2.0 (at most doubling) - to avoid running completely out of
      // control.
      const double scale =
          Clamp(period_.seconds() / time_so_far.seconds(), 1.01, 2.0);
      better_guess = expected_updates_per_period_ * scale;
      if (better_guess <= expected_updates_per_period_) {
        better_guess = expected_updates_per_period_ + 1;
      }
    }
    // Store the remainder left. Note that updates_remaining_ may have been
    // decremented by another thread whilst we performed the above calculations:
    // we simply discard those decrements.
    updates_remaining_.store(better_guess - expected_updates_per_period_,
                             std::memory_order_release);
    // Not quite done, return, try for longer.
    return false;
  }
  // Finished period, start a new one and return true.
  // We try to predict how many update periods we'd need to cover the full time
  // span, and we increase that by 1% to attempt to tend to not enter the above
  // stanza.
  expected_updates_per_period_ =
      period_.seconds() * expected_updates_per_period_ / time_so_far.seconds();
  if (expected_updates_per_period_ < 1) expected_updates_per_period_ = 1;
  period_start_ = now;
  f(time_so_far);
  updates_remaining_.store(expected_updates_per_period_,
                           std::memory_order_release);
  return true;
}

}  // namespace grpc_core
