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

#ifndef GRPC_CORE_LIB_RESOURCE_QUOTA_PERIODIC_UPDATE_H
#define GRPC_CORE_LIB_RESOURCE_QUOTA_PERIODIC_UPDATE_H

#include <grpc/support/port_platform.h>

#include <inttypes.h>

#include <atomic>

#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

class PeriodicUpdate {
 public:
  explicit PeriodicUpdate(Duration period) : period_(period) {}

  // Tick the update, return true if we think the period expired.
  GRPC_MUST_USE_RESULT bool Tick() {
    if (updates_remaining_.fetch_sub(1, std::memory_order_acquire) == 1) {
      return MaybeEndPeriod();
    }
    return false;
  }

 private:
  GRPC_MUST_USE_RESULT bool MaybeEndPeriod();

  const Duration period_;
  Timestamp period_start_ = ExecCtx::Get()->Now();
  int64_t expected_updates_per_period_ = 1;
  std::atomic<int64_t> updates_remaining_{1};
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_RESOURCE_QUOTA_PERIODIC_UPDATE_H
