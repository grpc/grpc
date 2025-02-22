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

#include "src/core/server/max_concurrent_request_governer.h"

#include <atomic>
#include <limits>

namespace grpc_core {

uint32_t MaxConcurrentRequestGovernor::GetPerConnectionMaxConcurrentRequests(
    uint32_t current_open_requests) {
  Limiter& limiter = limiters_.this_cpu();
  limiter.periodic_update.Tick([this, &limiter](Duration) {
    int64_t outstanding_requests = 0;
    int64_t open_channels = 0;
    for (auto& stats : stats_) {
      outstanding_requests +=
          stats.outstanding_requests.load(std::memory_order_relaxed);
      open_channels += stats.open_channels.load(std::memory_order_relaxed);
    }
    open_channels = std::max<int64_t>(1, open_channels);
    outstanding_requests = std::max<int64_t>(0, outstanding_requests);
    const int64_t max_outstanding_requests =
        limiter.max_outstanding_requests.load(std::memory_order_relaxed);
    const int64_t allowed_requests_per_channel =
        (max_outstanding_requests - outstanding_requests) / open_channels;
    const uint64_t target_mean_requests_per_channel =
        max_outstanding_requests / open_channels;
    limiter.allowed_requests_per_channel.store(
        std::max<int64_t>(0, allowed_requests_per_channel),
        std::memory_order_relaxed);
    limiter.target_mean_requests_per_channel.store(
        target_mean_requests_per_channel, std::memory_order_relaxed);
  });
  const uint64_t allowed_requests_per_channel =
      limiter.allowed_requests_per_channel.load(std::memory_order_relaxed);
  const uint64_t target_mean_requests_per_channel =
      limiter.target_mean_requests_per_channel.load(std::memory_order_relaxed);
  if (allowed_requests_per_channel == 0) {
    // If there are open requests on this channel, but we're past capacity
    // try to lower the number of requests here. This should slowly force
    // ramping down to numbers we can cope with.
    if (current_open_requests > 1) return current_open_requests - 1;
    return 1;
  }
  auto clamp = [](uint64_t x) {
    return std::min<uint64_t>(x, std::numeric_limits<uint32_t>::max());
  };
  if (current_open_requests < target_mean_requests_per_channel) {
    return clamp(current_open_requests + allowed_requests_per_channel);
  } else if (current_open_requests < 2 * target_mean_requests_per_channel) {
    return clamp(current_open_requests + 1);
  } else {
    return clamp(2 * target_mean_requests_per_channel);
  }
}

}  // namespace grpc_core
