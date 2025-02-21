// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_SERVER_MAX_CONCURRENT_REQUEST_GOVERNER_H
#define GRPC_SRC_CORE_SERVER_MAX_CONCURRENT_REQUEST_GOVERNER_H

#include <cstdint>

#include "src/core/lib/resource_quota/periodic_update.h"
#include "src/core/util/per_cpu.h"

namespace grpc_core {

class MaxConcurrentRequestGovernor {
 public:
  MaxConcurrentRequestGovernor();

  void IncrementOutstandingRequests() {
    stats_.this_cpu().outstanding_requests.fetch_add(1,
                                                     std::memory_order_relaxed);
  }

  void DecrementOutstandingRequests() {
    stats_.this_cpu().outstanding_requests.fetch_sub(1,
                                                     std::memory_order_relaxed);
  }

  void IncrementOpenChannels() {
    stats_.this_cpu().open_channels.fetch_add(1, std::memory_order_relaxed);
  }

  void DecrementOpenChannels() {
    stats_.this_cpu().open_channels.fetch_sub(1, std::memory_order_relaxed);
  }

  uint32_t GetConnectionMaxConcurrentRequests(uint32_t current_open_requests);

  void SetMaxOutstandingRequests();

 private:
  struct alignas(GPR_CACHELINE_SIZE) Statistics {
    std::atomic<int64_t> outstanding_requests{0};
    std::atomic<int64_t> open_channels{0};
  };
  PerCpu<Statistics> stats_;

  struct alignas(GPR_CACHELINE_SIZE) Limiter {
    PeriodicUpdate periodic_update{Duration::Milliseconds(100)};
    std::atomic<uint64_t> allowed_requests_per_channel{0};
    std::atomic<uint64_t> target_mean_requests_per_channel{0};
    std::atomic<int64_t> max_outstanding_requests{0};
  };
  PerCpu<Limiter> limiters_;
};

}  // namespace grpc_core

#endif
