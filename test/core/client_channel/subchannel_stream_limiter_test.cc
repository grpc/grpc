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

#include "src/core/client_channel/subchannel_stream_limiter.h"

#include "gtest/gtest.h"

namespace grpc_core {
namespace {

TEST(SubchannelStreamLimiterTest, Basic) {
  SubchannelStreamLimiter limiter(/*max_concurrent_streams=*/2);
  // Get quota for first RPC.  Should succeed.
  EXPECT_TRUE(limiter.GetQuotaForRpc());
  // Get quota for second RPC.  Should succeed.
  EXPECT_TRUE(limiter.GetQuotaForRpc());
  // Get quota for third RPC.  Should fail, because we're at the limit.
  EXPECT_FALSE(limiter.GetQuotaForRpc());
  // Return quota.  Should return true because we are now below the limit.
  EXPECT_TRUE(limiter.ReturnQuotaForRpc());
  // Return quota.  Should return false because we were already below the limit.
  EXPECT_FALSE(limiter.ReturnQuotaForRpc());
  // Getting quota for another RPC should succeed now.
  EXPECT_TRUE(limiter.GetQuotaForRpc());
}

TEST(SubchannelStreamLimiterTest, IncreaseLimit) {
  SubchannelStreamLimiter limiter(/*max_concurrent_streams=*/1);
  // Get quota.  Should succeed.
  EXPECT_TRUE(limiter.GetQuotaForRpc());
  // Getting quota again should fail, because we're at the limit.
  EXPECT_FALSE(limiter.GetQuotaForRpc());
  // Increase limit to 2.  Should return true because we now have more
  // quota to give out.
  EXPECT_TRUE(limiter.SetMaxConcurrentStreams(2));
  // We can now get that quota.
  EXPECT_TRUE(limiter.GetQuotaForRpc());
  // But no more than what's been added.
  EXPECT_FALSE(limiter.GetQuotaForRpc());
}

TEST(SubchannelStreamLimiterTest, DecreaseLimit) {
  SubchannelStreamLimiter limiter(/*max_concurrent_streams=*/2);
  // Allocate all quota.
  EXPECT_TRUE(limiter.GetQuotaForRpc());
  EXPECT_TRUE(limiter.GetQuotaForRpc());
  // Decrease limit to 1.  This puts us under water: the limit is 1 but
  // there are 2 RPCs currently in flight.
  // Returns false because there is no quota to start any more RPCs.
  EXPECT_FALSE(limiter.SetMaxConcurrentStreams(1));
  // Return quota for one RPC.  This puts us back to the limit (limit 1,
  // and 1 RPC in flight), but there's still no quota to start any more RPCs,
  // so it returns false.
  EXPECT_FALSE(limiter.ReturnQuotaForRpc());
  // Return quota for the last RPC.  Now we're under the limit, so it
  // returns true.
  EXPECT_TRUE(limiter.ReturnQuotaForRpc());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
