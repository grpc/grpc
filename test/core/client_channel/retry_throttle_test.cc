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

#include "src/core/client_channel/retry_throttle.h"

#include "gtest/gtest.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace internal {
namespace {

TEST(RetryThrottler, Basic) {
  // Max token count is 4, so threshold for retrying is 2.
  // Token count starts at 4.
  // Each failure decrements by 1.  Each success increments by 1.6.
  auto throttler = RetryThrottler::Create(4000, 1600, nullptr);
  // Failure: token_count=3.  Above threshold.
  EXPECT_TRUE(throttler->RecordFailure());
  // Success: token_count=4.  Not incremented beyond max.
  throttler->RecordSuccess();
  // Failure: token_count=3.  Above threshold.
  EXPECT_TRUE(throttler->RecordFailure());
  // Failure: token_count=2.  At threshold, so no retries.
  EXPECT_FALSE(throttler->RecordFailure());
  // Failure: token_count=1.  Below threshold, so no retries.
  EXPECT_FALSE(throttler->RecordFailure());
  // Failure: token_count=0.  Below threshold, so no retries.
  EXPECT_FALSE(throttler->RecordFailure());
  // Failure: token_count=0.  Below threshold, so no retries.  Not
  // decremented below min.
  EXPECT_FALSE(throttler->RecordFailure());
  // Success: token_count=1.6.
  throttler->RecordSuccess();
  // Success: token_count=3.2.
  throttler->RecordSuccess();
  // Failure: token_count=2.2.  Above threshold.
  EXPECT_TRUE(throttler->RecordFailure());
  // Failure: token_count=1.2.  Below threshold, so no retries.
  EXPECT_FALSE(throttler->RecordFailure());
  // Success: token_count=2.8.
  throttler->RecordSuccess();
  // Failure: token_count=1.8.  Below threshold, so no retries.
  EXPECT_FALSE(throttler->RecordFailure());
  // Success: token_count=3.4.
  throttler->RecordSuccess();
  // Failure: token_count=2.4.  Above threshold.
  EXPECT_TRUE(throttler->RecordFailure());
}

TEST(RetryThrottler, Replacement) {
  // Create throttler.
  // Max token count is 4, so threshold for retrying is 2.
  // Token count starts at 4.
  // Each failure decrements by 1.  Each success increments by 1.
  auto old_throttler = RetryThrottler::Create(4000, 1000, nullptr);
  EXPECT_EQ(old_throttler->max_milli_tokens(), 4000);
  EXPECT_EQ(old_throttler->milli_token_ratio(), 1000);
  EXPECT_EQ(old_throttler->milli_tokens(), 4000);
  // Failure: token_count=3.  Above threshold.
  EXPECT_TRUE(old_throttler->RecordFailure());
  // Create a new throttler with the same settings.  This should
  // return the same object.
  auto throttler = RetryThrottler::Create(4000, 1000, old_throttler);
  EXPECT_EQ(old_throttler, throttler);
  // Create a new throttler with different settings.  This should create
  // a new object.
  // Max token count is 10, so threshold for retrying is 5.
  // Token count starts at 7.5 (ratio inherited from old_throttler).
  // Each failure decrements by 1.  Each success increments by 3.
  throttler = RetryThrottler::Create(10000, 3000, old_throttler);
  EXPECT_NE(old_throttler, throttler);
  EXPECT_EQ(throttler->max_milli_tokens(), 10000);
  EXPECT_EQ(throttler->milli_token_ratio(), 3000);
  EXPECT_EQ(throttler->milli_tokens(), 7500);
  // Failure via old_throttler: token_count=6.5.
  EXPECT_TRUE(old_throttler->RecordFailure());
  // Failure: token_count=5.5.
  EXPECT_TRUE(old_throttler->RecordFailure());
  // Failure via old_throttler: token_count=4.5.  Below threshold.
  EXPECT_FALSE(old_throttler->RecordFailure());
  // Failure: token_count=3.5.  Below threshold.
  EXPECT_FALSE(throttler->RecordFailure());
  // Success: token_count=6.5.
  throttler->RecordSuccess();
  // Failure via old_throttler: token_count=5.5.  Above threshold.
  EXPECT_TRUE(old_throttler->RecordFailure());
  // Failure: token_count=4.5.  Below threshold.
  EXPECT_FALSE(throttler->RecordFailure());
}

}  // namespace
}  // namespace internal
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
