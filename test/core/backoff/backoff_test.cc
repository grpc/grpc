/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/backoff/backoff.h"

#include <algorithm>

#include <grpc/support/log.h>

#include <gtest/gtest.h>
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

using grpc_core::BackOff;

TEST(BackOffTest, ConstantBackOff) {
  const grpc_millis initial_backoff = 200;
  const double multiplier = 1.0;
  const double jitter = 0.0;
  const grpc_millis max_backoff = 1000;
  grpc_core::ExecCtx exec_ctx;
  BackOff::Options options;
  options.set_initial_backoff(initial_backoff)
      .set_multiplier(multiplier)
      .set_jitter(jitter)
      .set_max_backoff(max_backoff);
  BackOff backoff(options);

  grpc_millis next_attempt_start_time = backoff.NextAttemptTime();
  EXPECT_EQ(next_attempt_start_time - grpc_core::ExecCtx::Get()->Now(),
            initial_backoff);
  for (int i = 0; i < 10000; i++) {
    next_attempt_start_time = backoff.NextAttemptTime();
    EXPECT_EQ(next_attempt_start_time - grpc_core::ExecCtx::Get()->Now(),
              initial_backoff);
  }
}

TEST(BackOffTest, MinConnect) {
  const grpc_millis initial_backoff = 100;
  const double multiplier = 1.0;
  const double jitter = 0.0;
  const grpc_millis max_backoff = 1000;
  grpc_core::ExecCtx exec_ctx;
  BackOff::Options options;
  options.set_initial_backoff(initial_backoff)
      .set_multiplier(multiplier)
      .set_jitter(jitter)
      .set_max_backoff(max_backoff);
  BackOff backoff(options);
  grpc_millis next = backoff.NextAttemptTime();
  EXPECT_EQ(next - grpc_core::ExecCtx::Get()->Now(), initial_backoff);
}

TEST(BackOffTest, NoJitterBackOff) {
  const grpc_millis initial_backoff = 2;
  const double multiplier = 2.0;
  const double jitter = 0.0;
  const grpc_millis max_backoff = 513;
  BackOff::Options options;
  options.set_initial_backoff(initial_backoff)
      .set_multiplier(multiplier)
      .set_jitter(jitter)
      .set_max_backoff(max_backoff);
  BackOff backoff(options);
  // x_1 = 2
  // x_n = 2**i + x_{i-1} ( = 2**(n+1) - 2 )
  grpc_core::ExecCtx exec_ctx;
  grpc_core::ExecCtx::Get()->TestOnlySetNow(0);
  grpc_millis next = backoff.NextAttemptTime();
  EXPECT_EQ(next, 2);
  grpc_core::ExecCtx::Get()->TestOnlySetNow(next);
  next = backoff.NextAttemptTime();
  EXPECT_EQ(next, 6);
  grpc_core::ExecCtx::Get()->TestOnlySetNow(next);
  next = backoff.NextAttemptTime();
  EXPECT_EQ(next, 14);
  grpc_core::ExecCtx::Get()->TestOnlySetNow(next);
  next = backoff.NextAttemptTime();
  EXPECT_EQ(next, 30);
  grpc_core::ExecCtx::Get()->TestOnlySetNow(next);
  next = backoff.NextAttemptTime();
  EXPECT_EQ(next, 62);
  grpc_core::ExecCtx::Get()->TestOnlySetNow(next);
  next = backoff.NextAttemptTime();
  EXPECT_EQ(next, 126);
  grpc_core::ExecCtx::Get()->TestOnlySetNow(next);
  next = backoff.NextAttemptTime();
  EXPECT_EQ(next, 254);
  grpc_core::ExecCtx::Get()->TestOnlySetNow(next);
  next = backoff.NextAttemptTime();
  EXPECT_EQ(next, 510);
  grpc_core::ExecCtx::Get()->TestOnlySetNow(next);
  next = backoff.NextAttemptTime();
  EXPECT_EQ(next, 1022);
  grpc_core::ExecCtx::Get()->TestOnlySetNow(next);
  next = backoff.NextAttemptTime();
  // Hit the maximum timeout. From this point onwards, retries will increase
  // only by max timeout.
  EXPECT_EQ(next, 1535);
  grpc_core::ExecCtx::Get()->TestOnlySetNow(next);
  next = backoff.NextAttemptTime();
  EXPECT_EQ(next, 2048);
  grpc_core::ExecCtx::Get()->TestOnlySetNow(next);
  next = backoff.NextAttemptTime();
  EXPECT_EQ(next, 2561);
}

TEST(BackOffTest, JitterBackOff) {
  const grpc_millis initial_backoff = 500;
  grpc_millis current_backoff = initial_backoff;
  const grpc_millis max_backoff = 1000;
  const double multiplier = 1.0;
  const double jitter = 0.1;
  BackOff::Options options;
  options.set_initial_backoff(initial_backoff)
      .set_multiplier(multiplier)
      .set_jitter(jitter)
      .set_max_backoff(max_backoff);
  BackOff backoff(options);

  backoff.SetRandomSeed(0);  // force consistent PRNG

  grpc_core::ExecCtx exec_ctx;
  grpc_millis next = backoff.NextAttemptTime();
  EXPECT_EQ(next - grpc_core::ExecCtx::Get()->Now(), initial_backoff);

  grpc_millis expected_next_lower_bound = static_cast<grpc_millis>(
      static_cast<double>(current_backoff) * (1 - jitter));
  grpc_millis expected_next_upper_bound = static_cast<grpc_millis>(
      static_cast<double>(current_backoff) * (1 + jitter));

  for (int i = 0; i < 10000; i++) {
    next = backoff.NextAttemptTime();
    // next-now must be within (jitter*100)% of the current backoff (which
    // increases by * multiplier up to max_backoff).
    const grpc_millis timeout_millis = next - grpc_core::ExecCtx::Get()->Now();
    EXPECT_GE(timeout_millis, expected_next_lower_bound);
    EXPECT_LE(timeout_millis, expected_next_upper_bound);
    current_backoff =
        std::min(static_cast<grpc_millis>(static_cast<double>(current_backoff) *
                                          multiplier),
                 max_backoff);
    expected_next_lower_bound = static_cast<grpc_millis>(
        static_cast<double>(current_backoff) * (1 - jitter));
    expected_next_upper_bound = static_cast<grpc_millis>(
        static_cast<double>(current_backoff) * (1 + jitter));
  }
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
