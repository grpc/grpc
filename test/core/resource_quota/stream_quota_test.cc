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

#include "src/core/util/ref_counted_ptr.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {

TEST(StreamQuotaTest, Works) {
  auto q = MakeRefCounted<StreamQuota>();
  q->SetMaxOutstandingStreams(10);

  // Open 2 channels.
  q->IncrementOpenChannels();
  q->IncrementOpenChannels();

  // Update the limits.
  q->UpdatePerConnectionLimitsForAllTestOnly();

  // 5 streams per channel, till the open requests reaches 5.
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(0), 5);
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(1), 5);
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(2), 5);
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(3), 5);
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(4), 5);

  // Once we reach the mean target, we allow 1 more.
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(5), 6);
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(6), 7);
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(7), 8);
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(8), 9);
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(9), 10);

  // 2 * mean target is the max.
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(20), 10);

  // Now we add a request.
  q->IncrementOutstandingRequests();
  // Update the limits.
  q->UpdatePerConnectionLimitsForAllTestOnly();

  // While the target per channel is still 5, allowed requests per channel
  // should be 4.
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(0), 4);
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(1), 5);
  EXPECT_EQ(q->GetConnectionMaxConcurrentRequests(2), 5);
}

}  // namespace testing
}  // namespace grpc_core

// Hook needed to run ExecCtx outside of iomgr.
void grpc_set_default_iomgr_platform() {}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
