// Copyright 2024 The gRPC Authors
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

#ifndef GRPC_SRC_CORE_UTIL_WAIT_FOR_SINGLE_OWNER_H
#define GRPC_SRC_CORE_UTIL_WAIT_FOR_SINGLE_OWNER_H

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "src/core/util/crash.h"
#include "src/core/util/time.h"

namespace grpc_core {

constexpr Duration kWaitForSingleOwnerStallCheckFrequency =
    Duration::Seconds(10);

// Provide a function that WaitForSingleOwner will call when it appears to have
// stalled.
void SetWaitForSingleOwnerStalledCallback(absl::AnyInvocable<void()> cb);

// INTERNAL: Call the stall callback.
void WaitForSingleOwnerStalled();

// Waits until the use_count of the shared_ptr has reached 1 and returns,
// destroying the object.
//
// Callers must first give up their ref, or this method will block forever.
// Usage: WaitForSingleOwner(std::move(obj))
template <typename T>
void WaitForSingleOwner(std::shared_ptr<T> obj) {
  WaitForSingleOwnerWithTimeout(std::move(obj), Duration::Hours(24));
}

// Waits until the use_count of the shared_ptr has reached 1 and returns,
// destroying the object.
//
// This version will CRASH after the given timeout.
// Usage: WaitForSingleOwnerWithTimeout(std::move(obj), Duration::Seconds(30));
template <typename T>
void WaitForSingleOwnerWithTimeout(std::shared_ptr<T> obj, Duration timeout) {
  auto start = Timestamp::Now();
  size_t last_check_period = 0;
  bool reported_stall = false;
  while (obj.use_count() > 1) {
    auto elapsed = Timestamp::Now() - start;
    if (size_t current_check_period =
            elapsed.seconds() /
            kWaitForSingleOwnerStallCheckFrequency.seconds();
        current_check_period > last_check_period) {
      ++last_check_period;
      if (!reported_stall) {
        LOG(INFO) << "Investigating stall...";
        WaitForSingleOwnerStalled();
        reported_stall = true;
      }
    }
    auto remaining = timeout - elapsed;
    if (remaining < Duration::Zero()) {
      Crash("Timed out waiting for a single shared_ptr owner");
    }
    // To avoid log spam, wait a few seconds to begin logging the wait time.
    if (elapsed >= Duration::Seconds(2)) {
      LOG_EVERY_N_SEC(INFO, 2)
          << "obj.use_count() = " << obj.use_count() << " timeout_remaining = "
          << absl::FormatDuration(absl::Milliseconds(remaining.millis()));
    }
    absl::SleepFor(absl::Milliseconds(100));
  }
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_UTIL_WAIT_FOR_SINGLE_OWNER_H
