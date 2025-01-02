// Copyright 2023 The gRPC Authors
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
#include "src/core/lib/event_engine/thread_pool/thread_count.h"

#include <grpc/support/port_platform.h>
#include <inttypes.h>

#include <cstddef>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/core/util/time.h"

namespace grpc_event_engine {
namespace experimental {

// -------- LivingThreadCount --------

absl::Status LivingThreadCount::BlockUntilThreadCount(
    size_t desired_threads, const char* why,
    grpc_core::Duration stuck_timeout) {
  grpc_core::Timestamp timeout_baseline = grpc_core::Timestamp::Now();
  constexpr grpc_core::Duration log_rate = grpc_core::Duration::Seconds(5);
  size_t prev_thread_count = 0;
  while (true) {
    auto curr_threads = WaitForCountChange(desired_threads, log_rate / 2);
    if (curr_threads == desired_threads) return absl::OkStatus();
    auto elapsed = grpc_core::Timestamp::Now() - timeout_baseline;
    if (curr_threads == prev_thread_count) {
      if (elapsed > stuck_timeout) {
        return absl::DeadlineExceededError(absl::StrFormat(
            "Timed out after %f seconds", stuck_timeout.seconds()));
      }
    } else {
      // the thread count has changed. Reset the timeout clock
      prev_thread_count = curr_threads;
      timeout_baseline = grpc_core::Timestamp::Now();
    }
    GRPC_LOG_EVERY_N_SEC_DELAYED_DEBUG(
        log_rate.seconds(),
        "Waiting for thread pool to idle before %s. (%" PRIdPTR " to %" PRIdPTR
        "). Timing out in %0.f seconds.",
        why, curr_threads, desired_threads,
        (stuck_timeout - elapsed).seconds());
  }
}

size_t LivingThreadCount::WaitForCountChange(size_t desired_threads,
                                             grpc_core::Duration timeout) {
  size_t count;
  auto deadline = absl::Now() + absl::Milliseconds(timeout.millis());
  do {
    grpc_core::MutexLock lock(&mu_);
    count = CountLocked();
    if (count == desired_threads) break;
    cv_.WaitWithDeadline(&mu_, deadline);
  } while (absl::Now() < deadline);
  return count;
}

}  // namespace experimental
}  // namespace grpc_event_engine
