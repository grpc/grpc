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
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/thread_pool/thread_count.h"

#include <inttypes.h>

#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/support/log.h>

namespace grpc_event_engine {
namespace experimental {

// -------- LivingThreadCount --------

void LivingThreadCount::BlockUntilThreadCount(size_t desired_threads,
                                              const char* why) {
  constexpr grpc_core::Duration log_rate = grpc_core::Duration::Seconds(3);
  while (true) {
    auto curr_threads = WaitForCountChange(desired_threads, log_rate);
    if (curr_threads == desired_threads) break;
    GRPC_LOG_EVERY_N_SEC_DELAYED(
        log_rate.seconds(), GPR_DEBUG,
        "Waiting for thread pool to idle before %s. (%" PRIdPTR " to %" PRIdPTR
        ")",
        why, curr_threads, desired_threads);
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
