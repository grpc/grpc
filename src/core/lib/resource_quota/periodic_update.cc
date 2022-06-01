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

namespace grpc_core {

bool PeriodicUpdate::MaybeEndPeriod() {
  auto now = ExecCtx::Get()->Now();
  Duration time_so_far = now - period_start_;
  if (time_so_far < period_) {
    // Double the number of updates remaining until the next period.
    int64_t better_guess;
    if (time_so_far.millis() == 0) {
      better_guess = expected_updates_per_period_ * 2;
    } else {
      const double scale = period_.seconds() / time_so_far.seconds();
      if (scale > 2) {
        better_guess = expected_updates_per_period_ * 2;
      } else {
        better_guess = expected_updates_per_period_ * scale;
        if (better_guess <= expected_updates_per_period_) {
          better_guess = expected_updates_per_period_ + 1;
        }
      }
    }
    const int64_t add = better_guess - expected_updates_per_period_;
    expected_updates_per_period_ = better_guess;
    const int64_t past_remaining =
        updates_remaining_.fetch_add(add, std::memory_order_release);
    // If adding more things still didn't get us back above 0 things remaining
    // then we should continue adding more things.
    if (add + past_remaining <= 0) return MaybeEndPeriod();
    // Not quite done, return false, try for longer.
    return false;
  }
  // Finished period, start a new one and return true.
  expected_updates_per_period_ =
      period_.seconds() * expected_updates_per_period_ / time_so_far.seconds();
  if (expected_updates_per_period_ < 1) expected_updates_per_period_ = 1;
  period_start_ = now;
  updates_remaining_.store(expected_updates_per_period_,
                           std::memory_order_release);
  return true;
}

}  // namespace grpc_core
