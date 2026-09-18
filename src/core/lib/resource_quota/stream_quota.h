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

#ifndef GRPC_SRC_CORE_LIB_RESOURCE_QUOTA_STREAM_QUOTA_H
#define GRPC_SRC_CORE_LIB_RESOURCE_QUOTA_STREAM_QUOTA_H

#include <grpc/support/port_platform.h>

#include <atomic>
#include <cstdint>
#include <limits>

#include "src/core/lib/resource_quota/periodic_update.h"
#include "src/core/util/per_cpu.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {

// Tracks the amount of streams in a resource quota.
class StreamQuota : public RefCounted<StreamQuota> {
 public:
  StreamQuota() = default;
  ~StreamQuota() override = default;

  StreamQuota(const StreamQuota&) = delete;
  StreamQuota& operator=(const StreamQuota&) = delete;

  void IncrementOutstandingRequests() {
    stats_.this_cpu().outstanding_requests.fetch_add(1,
                                                     std::memory_order_relaxed);
  }

  void DecrementOutstandingRequests() {
    stats_.this_cpu().outstanding_requests.fetch_sub(1,
                                                     std::memory_order_relaxed);
  }

  void IncrementOpenChannels() {
    limiter_.open_channels.fetch_add(1, std::memory_order_relaxed);
  }

  void DecrementOpenChannels() {
    limiter_.open_channels.fetch_sub(1, std::memory_order_relaxed);
  }

  uint32_t GetConnectionMaxConcurrentRequests(uint32_t current_open_requests) {
    return GetPerConnectionMaxConcurrentRequests(current_open_requests);
  }

  void SetMaxOutstandingStreams(uint32_t new_max_outstanding_streams);

  void UpdatePerConnectionLimitsForAllTestOnly();

 private:
  uint32_t GetPerConnectionMaxConcurrentRequests(
      uint32_t current_open_requests);

  struct alignas(GPR_CACHELINE_SIZE) Statistics {
    std::atomic<int64_t> outstanding_requests{0};
  };
  PerCpu<Statistics> stats_{PerCpuOptions()};

  struct alignas(GPR_CACHELINE_SIZE) Limiter {
    PeriodicUpdate periodic_update{Duration::Seconds(1)};
    std::atomic<uint64_t> allowed_requests_per_channel{
        std::numeric_limits<uint32_t>::max()};
    std::atomic<uint64_t> target_mean_requests_per_channel{
        std::numeric_limits<uint32_t>::max()};
    std::atomic<uint64_t> max_outstanding_requests{
        std::numeric_limits<uint32_t>::max()};
    std::atomic<uint64_t> open_channels{0};
  };
  Limiter limiter_;

  void UpdatePerConnectionLimits();
};

using StreamQuotaRefPtr = RefCountedPtr<StreamQuota>;

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_RESOURCE_QUOTA_STREAM_QUOTA_H
