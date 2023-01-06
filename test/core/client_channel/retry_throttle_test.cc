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

#include "src/core/ext/filters/client_channel/retry_throttle.h"

#include "gtest/gtest.h"

#include "test/core/util/test_config.h"

namespace grpc_core {
namespace internal {
namespace {

TEST(ServerRetryThrottleData, Basic) {
  // Max token count is 4, so threshold for retrying is 2.
  // Token count starts at 4.
  // Each failure decrements by 1.  Each success increments by 1.6.
  auto throttle_data =
      MakeRefCounted<ServerRetryThrottleData>(4000, 1600, nullptr);
  // Failure: token_count=3.  Above threshold.
  EXPECT_TRUE(throttle_data->RecordFailure());
  // Success: token_count=4.  Not incremented beyond max.
  throttle_data->RecordSuccess();
  // Failure: token_count=3.  Above threshold.
  EXPECT_TRUE(throttle_data->RecordFailure());
  // Failure: token_count=2.  At threshold, so no retries.
  EXPECT_FALSE(throttle_data->RecordFailure());
  // Failure: token_count=1.  Below threshold, so no retries.
  EXPECT_FALSE(throttle_data->RecordFailure());
  // Failure: token_count=0.  Below threshold, so no retries.
  EXPECT_FALSE(throttle_data->RecordFailure());
  // Failure: token_count=0.  Below threshold, so no retries.  Not
  // decremented below min.
  EXPECT_FALSE(throttle_data->RecordFailure());
  // Success: token_count=1.6.
  throttle_data->RecordSuccess();
  // Success: token_count=3.2.
  throttle_data->RecordSuccess();
  // Failure: token_count=2.2.  Above threshold.
  EXPECT_TRUE(throttle_data->RecordFailure());
  // Failure: token_count=1.2.  Below threshold, so no retries.
  EXPECT_FALSE(throttle_data->RecordFailure());
  // Success: token_count=2.8.
  throttle_data->RecordSuccess();
  // Failure: token_count=1.8.  Below threshold, so no retries.
  EXPECT_FALSE(throttle_data->RecordFailure());
  // Success: token_count=3.4.
  throttle_data->RecordSuccess();
  // Failure: token_count=2.4.  Above threshold.
  EXPECT_TRUE(throttle_data->RecordFailure());
}

TEST(ServerRetryThrottleData, Replacement) {
  // Create old throttle data.
  // Max token count is 4, so threshold for retrying is 2.
  // Token count starts at 4.
  // Each failure decrements by 1.  Each success increments by 1.
  auto old_throttle_data =
      MakeRefCounted<ServerRetryThrottleData>(4000, 1000, nullptr);
  // Failure: token_count=3.  Above threshold.
  EXPECT_TRUE(old_throttle_data->RecordFailure());
  // Create new throttle data.
  // Max token count is 10, so threshold for retrying is 5.
  // Token count starts at 7.5 (ratio inherited from old_throttle_data).
  // Each failure decrements by 1.  Each success increments by 3.
  auto throttle_data = MakeRefCounted<ServerRetryThrottleData>(
      10000, 3000, old_throttle_data.get());
  // Failure via old_throttle_data: token_count=6.5.
  EXPECT_TRUE(old_throttle_data->RecordFailure());
  // Failure: token_count=5.5.
  EXPECT_TRUE(old_throttle_data->RecordFailure());
  // Failure via old_throttle_data: token_count=4.5.  Below threshold.
  EXPECT_FALSE(old_throttle_data->RecordFailure());
  // Failure: token_count=3.5.  Below threshold.
  EXPECT_FALSE(throttle_data->RecordFailure());
  // Success: token_count=6.5.
  throttle_data->RecordSuccess();
  // Failure via old_throttle_data: token_count=5.5.  Above threshold.
  EXPECT_TRUE(old_throttle_data->RecordFailure());
  // Failure: token_count=4.5.  Below threshold.
  EXPECT_FALSE(throttle_data->RecordFailure());
}

TEST(ServerRetryThrottleMap, Replacement) {
  const std::string kServerName = "server_name";
  // Create old throttle data.
  // Max token count is 4, so threshold for retrying is 2.
  // Token count starts at 4.
  // Each failure decrements by 1.  Each success increments by 1.
  auto old_throttle_data =
      ServerRetryThrottleMap::Get()->GetDataForServer(kServerName, 4000, 1000);
  // Failure: token_count=3.  Above threshold.
  EXPECT_TRUE(old_throttle_data->RecordFailure());
  // Create new throttle data.
  // Max token count is 10, so threshold for retrying is 5.
  // Token count starts at 7.5 (ratio inherited from old_throttle_data).
  // Each failure decrements by 1.  Each success increments by 3.
  auto throttle_data =
      ServerRetryThrottleMap::Get()->GetDataForServer(kServerName, 10000, 3000);
  // Failure via old_throttle_data: token_count=6.5.
  EXPECT_TRUE(old_throttle_data->RecordFailure());
  // Failure: token_count=5.5.
  EXPECT_TRUE(old_throttle_data->RecordFailure());
  // Failure via old_throttle_data: token_count=4.5.  Below threshold.
  EXPECT_FALSE(old_throttle_data->RecordFailure());
  // Failure: token_count=3.5.  Below threshold.
  EXPECT_FALSE(throttle_data->RecordFailure());
  // Success: token_count=6.5.
  throttle_data->RecordSuccess();
  // Failure via old_throttle_data: token_count=5.5.  Above threshold.
  EXPECT_TRUE(old_throttle_data->RecordFailure());
  // Failure: token_count=4.5.  Below threshold.
  EXPECT_FALSE(throttle_data->RecordFailure());
}

}  // namespace
}  // namespace internal
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
