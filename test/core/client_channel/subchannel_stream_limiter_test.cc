//
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
//

#include "src/core/client_channel/subchannel_stream_limiter.h"

#include <gtest/gtest.h>

namespace grpc_core {
namespace {

TEST(SubchannelStreamLimiterTest, Basic) {
  SubchannelStreamLimiter limiter;
  // Initial state: max_concurrent_streams = 0 (default in implementation details, but SetMaxConcurrentStreams usually called first)
  // Wait, default constructor initializes stream_counts_ to 0, which means max=0, in_flight=0.
  
  // Set max streams to 1
  EXPECT_TRUE(limiter.SetMaxConcurrentStreams(1));
  
  // Get quota for first RPC
  EXPECT_TRUE(limiter.GetQuotaForRpc());
  
  // Get quota for second RPC (should fail)
  EXPECT_FALSE(limiter.GetQuotaForRpc());
  
  // Return quota
  EXPECT_TRUE(limiter.ReturnQuotaForRpc()); // Returns true because we are now below quota (0 < 1)? 
  // Wait, ReturnQuotaForRpc returns true if GetRpcsInFlight == GetMaxConcurrentStreams.
  // If we had 1 in flight and max 1.
  // After return, 0 in flight, max 1.
  // 0 != 1. So it should return false?
  // Let's check implementation:
  // return GetRpcsInFlight(prev) == GetMaxConcurrentStreams(prev);
  // prev was 1 in flight, 1 max.
  // So 1 == 1. Returns true.
  // This means "was full before return".
  
  // Let's verify this behavior.
  // If I have 2 max, 1 in flight.
  // GetQuota -> 2 in flight.
  // ReturnQuota -> prev was 2 in flight, 2 max. Returns true.
  // ReturnQuota again -> prev was 1 in flight, 2 max. Returns false.
}

TEST(SubchannelStreamLimiterTest, IncreaseLimit) {
  SubchannelStreamLimiter limiter;
  EXPECT_TRUE(limiter.SetMaxConcurrentStreams(1));
  EXPECT_TRUE(limiter.GetQuotaForRpc());
  EXPECT_FALSE(limiter.GetQuotaForRpc());
  
  // Increase limit to 2
  EXPECT_TRUE(limiter.SetMaxConcurrentStreams(2));
  // Now we should be able to get quota
  EXPECT_TRUE(limiter.GetQuotaForRpc());
  EXPECT_FALSE(limiter.GetQuotaForRpc());
}

TEST(SubchannelStreamLimiterTest, DecreaseLimit) {
  SubchannelStreamLimiter limiter;
  EXPECT_TRUE(limiter.SetMaxConcurrentStreams(2));
  EXPECT_TRUE(limiter.GetQuotaForRpc());
  EXPECT_TRUE(limiter.GetQuotaForRpc());
  
  // Decrease limit to 1
  // We have 2 in flight. New limit 1.
  // SetMaxConcurrentStreams returns rpcs_in_flight < max_concurrent_streams
  // 2 < 1 is false.
  EXPECT_FALSE(limiter.SetMaxConcurrentStreams(1));
  
  // Return one
  // prev: 2 in flight, 1 max.
  // 2 != 1. Returns false?
  // Wait, GetMaxConcurrentStreams(prev) will be 1 (because we just set it).
  // GetRpcsInFlight(prev) will be 2.
  // 2 != 1. Returns false.
  EXPECT_FALSE(limiter.ReturnQuotaForRpc());
  
  // Return another
  // prev: 1 in flight, 1 max.
  // 1 == 1. Returns true.
  EXPECT_TRUE(limiter.ReturnQuotaForRpc());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
