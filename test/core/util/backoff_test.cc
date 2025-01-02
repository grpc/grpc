//
//
// Copyright 2016 gRPC authors.
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

#include "src/core/util/backoff.h"

#include <grpc/grpc.h>

#include <algorithm>
#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/util/time.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(BackOffTest, ConstantBackOff) {
  const auto kInitialBackoff = Duration::Milliseconds(200);
  const double kMultiplier = 1.0;
  const double kJitter = 0.0;
  const auto kMaxBackoff = Duration::Seconds(1);
  BackOff::Options options;
  options.set_initial_backoff(kInitialBackoff)
      .set_multiplier(kMultiplier)
      .set_jitter(kJitter)
      .set_max_backoff(kMaxBackoff);
  BackOff backoff(options);
  EXPECT_EQ(backoff.NextAttemptDelay(), kInitialBackoff);
  EXPECT_EQ(backoff.NextAttemptDelay(), kInitialBackoff);
  EXPECT_EQ(backoff.NextAttemptDelay(), kInitialBackoff);
  EXPECT_EQ(backoff.NextAttemptDelay(), kInitialBackoff);
  EXPECT_EQ(backoff.NextAttemptDelay(), kInitialBackoff);
}

TEST(BackOffTest, InitialBackoffCappedByMaxBackoff) {
  if (!IsBackoffCapInitialAtMaxEnabled()) {
    GTEST_SKIP() << "test requires backoff_cap_initial_at_max experiment";
  }
  const auto kInitialBackoff = Duration::Seconds(2);
  const auto kMaxBackoff = Duration::Seconds(1);
  const double kMultiplier = 1.0;
  const double kJitter = 0.0;
  BackOff::Options options;
  options.set_initial_backoff(kInitialBackoff)
      .set_multiplier(kMultiplier)
      .set_jitter(kJitter)
      .set_max_backoff(kMaxBackoff);
  BackOff backoff(options);
  EXPECT_EQ(backoff.NextAttemptDelay(), kMaxBackoff);
  EXPECT_EQ(backoff.NextAttemptDelay(), kMaxBackoff);
  EXPECT_EQ(backoff.NextAttemptDelay(), kMaxBackoff);
  EXPECT_EQ(backoff.NextAttemptDelay(), kMaxBackoff);
  EXPECT_EQ(backoff.NextAttemptDelay(), kMaxBackoff);
}

TEST(BackOffTest, NoJitterBackOff) {
  const auto kInitialBackoff = Duration::Milliseconds(2);
  const double kMultiplier = 2.0;
  const double kJitter = 0.0;
  const auto kMaxBackoff = Duration::Milliseconds(32);
  BackOff::Options options;
  options.set_initial_backoff(kInitialBackoff)
      .set_multiplier(kMultiplier)
      .set_jitter(kJitter)
      .set_max_backoff(kMaxBackoff);
  BackOff backoff(options);
  EXPECT_EQ(backoff.NextAttemptDelay(), Duration::Milliseconds(2));
  EXPECT_EQ(backoff.NextAttemptDelay(), Duration::Milliseconds(4));
  EXPECT_EQ(backoff.NextAttemptDelay(), Duration::Milliseconds(8));
  EXPECT_EQ(backoff.NextAttemptDelay(), Duration::Milliseconds(16));
  EXPECT_EQ(backoff.NextAttemptDelay(), Duration::Milliseconds(32));
  // No more increases after kMaxBackoff.
  EXPECT_EQ(backoff.NextAttemptDelay(), Duration::Milliseconds(32));
  EXPECT_EQ(backoff.NextAttemptDelay(), Duration::Milliseconds(32));
}

MATCHER_P2(InJitterRange, value, jitter, "") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(::testing::Ge(arg * (1 - jitter)), arg,
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(::testing::Le(arg * (1 + jitter)), arg,
                                      result_listener);
  return ok;
}

TEST(BackOffTest, JitterBackOff) {
  const auto kInitialBackoff = Duration::Milliseconds(2);
  const double kMultiplier = 2.0;
  const double kJitter = 0.1;
  const auto kMaxBackoff = Duration::Milliseconds(32);
  BackOff::Options options;
  options.set_initial_backoff(kInitialBackoff)
      .set_multiplier(kMultiplier)
      .set_jitter(kJitter)
      .set_max_backoff(kMaxBackoff);
  BackOff backoff(options);
  EXPECT_THAT(backoff.NextAttemptDelay(),
              InJitterRange(Duration::Milliseconds(2), kJitter));
  EXPECT_THAT(backoff.NextAttemptDelay(),
              InJitterRange(Duration::Milliseconds(4), kJitter));
  EXPECT_THAT(backoff.NextAttemptDelay(),
              InJitterRange(Duration::Milliseconds(8), kJitter));
  EXPECT_THAT(backoff.NextAttemptDelay(),
              InJitterRange(Duration::Milliseconds(16), kJitter));
  EXPECT_THAT(backoff.NextAttemptDelay(),
              InJitterRange(Duration::Milliseconds(32), kJitter));
  // No more increases after kMaxBackoff.
  EXPECT_THAT(backoff.NextAttemptDelay(),
              InJitterRange(Duration::Milliseconds(32), kJitter));
  EXPECT_THAT(backoff.NextAttemptDelay(),
              InJitterRange(Duration::Milliseconds(32), kJitter));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
