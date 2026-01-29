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

#include "src/core/lib/resource_quota/stream_quota.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>

namespace grpc_core {

void StreamQuota::SetMaxOutstandingStreams(
    uint32_t new_max_outstanding_streams) {
  limiter_.max_outstanding_requests.store(new_max_outstanding_streams,
                                          std::memory_order_relaxed);
  UpdatePerConnectionLimits();
}

uint32_t StreamQuota::GetPerConnectionMaxConcurrentRequests(
    uint32_t current_open_requests) {
  if (limiter_.max_outstanding_requests.load(std::memory_order_relaxed) ==
      std::numeric_limits<uint32_t>::max()) {
    return std::numeric_limits<uint32_t>::max();
  }

  limiter_.periodic_update.Tick(
      [this](Duration) { UpdatePerConnectionLimits(); });
  const uint64_t allowed_requests_per_channel =
      limiter_.allowed_requests_per_channel.load(std::memory_order_relaxed);
  const uint64_t target_mean_requests_per_channel =
      limiter_.target_mean_requests_per_channel.load(std::memory_order_relaxed);

  if (allowed_requests_per_channel == 0) {
    // If there are open requests on this channel, but we're past capacity
    // try to lower the number of requests here. This should slowly force
    // ramping down to numbers we can cope with.
    if (current_open_requests > 1) return current_open_requests - 1;
    return 1;
  }

  // If there is only one channel, we can allow the target mean.
  if (limiter_.open_channels.load(std::memory_order_relaxed) <= 1) {
    return target_mean_requests_per_channel;
  }

  auto clamp = [](uint64_t x) {
    return std::min<uint64_t>(x, std::numeric_limits<uint32_t>::max());
  };
  if (current_open_requests < target_mean_requests_per_channel) {
    return std::min(clamp(current_open_requests + allowed_requests_per_channel),
                    target_mean_requests_per_channel);
  } else if (current_open_requests < 2 * target_mean_requests_per_channel) {
    return clamp(current_open_requests + 1);
  } else {
    return clamp(2 * target_mean_requests_per_channel);
  }
}

void StreamQuota::UpdatePerConnectionLimits() {
  int64_t outstanding_requests = 0;
  int64_t open_channels =
      limiter_.open_channels.load(std::memory_order_relaxed);
  for (auto& stats : stats_) {
    outstanding_requests +=
        stats.outstanding_requests.load(std::memory_order_relaxed);
  }
  open_channels = std::max<int64_t>(1, open_channels);
  outstanding_requests = std::max<int64_t>(0, outstanding_requests);
  const int64_t max_outstanding_requests =
      limiter_.max_outstanding_requests.load(std::memory_order_relaxed);
  const int64_t allowed_requests_per_channel =
      (max_outstanding_requests - outstanding_requests) / open_channels;
  const uint64_t target_mean_requests_per_channel =
      max_outstanding_requests / open_channels;
  limiter_.allowed_requests_per_channel.store(
      std::max<int64_t>(0, allowed_requests_per_channel),
      std::memory_order_relaxed);
  limiter_.target_mean_requests_per_channel.store(
      target_mean_requests_per_channel, std::memory_order_relaxed);
}

void StreamQuota::UpdatePerConnectionLimitsForAllTestOnly() {
  UpdatePerConnectionLimits();
}

}  // namespace grpc_core
