#include "src/core/lib/resource_quota/stream_quota.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>

namespace grpc_core {

void StreamQuota::SetMaxOutstandingStreams(
    uint32_t new_max_outstanding_streams) {
  for (auto& limiter : limiters_) {
    limiter.max_outstanding_requests.store(
        new_max_outstanding_streams, std::memory_order_relaxed);
  }
}

uint32_t StreamQuota::GetPerConnectionMaxConcurrentRequests(
    uint32_t current_open_requests) {
  Limiter& limiter = limiters_.this_cpu();
  Statistics& stats = stats_.this_cpu();
  limiter.periodic_update.Tick([this, &limiter](Duration) {
    UpdatePerConnectionLimits(limiter);
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

  // If there is only one channel, we can allow the target mean.
  if (stats.open_channels.load(std::memory_order_relaxed) == 1) {
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

void StreamQuota::UpdatePerConnectionLimits(Limiter& limiter) {
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
}

void StreamQuota::UpdatePerConnectionLimitsForAllTestOnly() {
  for (auto& limiter : limiters_) {
    UpdatePerConnectionLimits(limiter);
  }
}

}  // namespace grpc_core
