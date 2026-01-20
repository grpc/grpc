//
//
// Copyright 2018 gRPC authors.
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
//
//

#include <grpc/support/port_platform.h>

#include "src/core/client_channel/subchannel_stream_limiter.h"

namespace grpc_core {

namespace {

uint64_t MakeStreamCounts(uint32_t max_concurrent_streams,
                          uint32_t rpcs_in_flight) {
  return (static_cast<uint64_t>(max_concurrent_streams) << 32) +
         static_cast<int64_t>(rpcs_in_flight);
}

uint32_t GetMaxConcurrentStreams(uint64_t stream_counts) {
  return static_cast<uint32_t>(stream_counts >> 32);
}

uint32_t GetRpcsInFlight(uint64_t stream_counts) {
  return static_cast<uint32_t>(stream_counts & 0xffffffffu);
}

}  // namespace

bool SubchannelStreamLimiter::SetMaxConcurrentStreams(
    uint32_t max_concurrent_streams) {
  uint64_t prev_stream_counts = stream_counts_.load(std::memory_order_acquire);
  uint32_t rpcs_in_flight;
  do {
    rpcs_in_flight = GetRpcsInFlight(prev_stream_counts);
  } while (!stream_counts_.compare_exchange_weak(
      prev_stream_counts,
      MakeStreamCounts(max_concurrent_streams, rpcs_in_flight),
      std::memory_order_acq_rel, std::memory_order_acquire));
  return rpcs_in_flight < max_concurrent_streams;
}

bool SubchannelStreamLimiter::GetQuotaForRpc() {
  uint64_t prev_stream_counts = stream_counts_.load(std::memory_order_acquire);
  do {
    const uint32_t rpcs_in_flight = GetRpcsInFlight(prev_stream_counts);
    const uint32_t max_concurrent_streams =
        GetMaxConcurrentStreams(prev_stream_counts);
    if (rpcs_in_flight == max_concurrent_streams) return false;
  } while (!stream_counts_.compare_exchange_weak(
      prev_stream_counts, prev_stream_counts + MakeStreamCounts(0, 1),
      std::memory_order_acq_rel, std::memory_order_acquire));
  return true;
}

bool SubchannelStreamLimiter::ReturnQuotaForRpc() {
  const uint64_t prev_stream_counts =
      stream_counts_.fetch_sub(MakeStreamCounts(0, 1));
  return GetRpcsInFlight(prev_stream_counts) ==
         GetMaxConcurrentStreams(prev_stream_counts);
}

}  // namespace grpc_core
