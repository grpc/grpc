//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_SUBCHANNEL_STREAM_LIMITER_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_SUBCHANNEL_STREAM_LIMITER_H

#include <atomic>
#include <cstdint>

namespace grpc_core {

class SubchannelStreamLimiter {
 public:
  explicit SubchannelStreamLimiter(uint32_t max_concurrent_streams);

  // Sets the maximum number of concurrent streams.
  // Returns true if the current number of RPCs in flight is less than the new
  // maximum.
  bool SetMaxConcurrentStreams(uint32_t max_concurrent_streams);

  // Attempts to get quota for a new RPC.
  // Returns true if quota was acquired, false otherwise.
  bool GetQuotaForRpc();

  // Returns quota for a completed RPC.
  // Returns true if the connection is no longer above its quota.
  bool ReturnQuotaForRpc();

 private:
  // First 32 bits are the MAX_CONCURRENT_STREAMS value reported by
  // the transport.
  // Last 32 bits are the current number of RPCs in flight on the connection.
  std::atomic<uint64_t> stream_counts_{0};
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_SUBCHANNEL_STREAM_LIMITER_H
