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

#include "src/core/lib/transport/bdp_estimator.h"

#include <grpc/grpc.h>
#include <stdlib.h>

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

namespace grpc_core {
namespace testing {
namespace {
std::atomic<int> g_clock{123};

gpr_timespec fake_gpr_now(gpr_clock_type clock_type) {
  gpr_timespec ts;
  ts.tv_sec = g_clock.load();
  ts.tv_nsec = 0;
  ts.clock_type = clock_type;
  return ts;
}

void increment_time(const int seconds = 30) {
  g_clock.fetch_add(seconds);
  if (ExecCtx::Get() != nullptr) {
    ExecCtx::Get()->InvalidateNow();
  }
}

class BdpEstimatorTest : public ::testing::Test {
 public:
  void AddSamples(int64_t* samples, size_t n) {
    increment_time();
    est.SchedulePing();
    est.StartPing();
    for (size_t i = 0u; i < n; i++) {
      est.AddIncomingBytes(samples[i]);
    }
    increment_time(/*seconds=*/1);
    ExecCtx::Get()->InvalidateNow();
    est.CompletePing();
  }

  void AddSample(int64_t sample) { AddSamples(&sample, 1u); }

 protected:
  void SetUp() override {
    g_clock.store(123);
    if (ExecCtx::Get() != nullptr) {
      ExecCtx::Get()->InvalidateNow();
    }
  }

  BdpEstimator est{/*name=*/"test"};
};

}  // namespace

TEST_F(BdpEstimatorTest, EstimateBdpNoSamples) {
  // This is just to assert on the default value of the estimator. If the
  // default changes, other tests may need to change. This failure would flag
  // that instantly.
  ExecCtx exec_ctx;
  EXPECT_EQ(est.EstimateBdp(), 65536);
}

TEST_F(BdpEstimatorTest, ZeroElapsedTimeDoesNotCrash) {
  ExecCtx exec_ctx;
  // Test completing a ping with zero elapsed time.
  // This verifies the estimator handles division by zero safely.
  est.SchedulePing();
  est.StartPing();
  // We do not increment g_clock between starting and completing the ping.
  est.CompletePing();
  // The estimate should remain at the default starting value.
  EXPECT_EQ(est.EstimateBdp(), kInitialBdpDefault);
}

TEST_F(BdpEstimatorTest, SteadyPingsIncreaseDelay) {
  ExecCtx exec_ctx;
  // Verify that steady pings increase the inter-ping delay.
  // Delay starts at 100ms, increases after two steady pings.
  // First steady ping: stable_estimate_count_ becomes 1.
  est.SchedulePing();
  est.StartPing();
  increment_time();
  Timestamp next_time_1 = est.CompletePing();
  Duration delay_1 = next_time_1 - Timestamp::Now();
  EXPECT_EQ(delay_1, Duration::Milliseconds(kDefaultInterPingDelayMillis));
  // Second steady ping: stable_estimate_count_ becomes 2.
  est.SchedulePing();
  est.StartPing();
  increment_time();
  Timestamp next_time_2 = est.CompletePing();
  Duration delay_2 = next_time_2 - Timestamp::Now();
  // Delay increases by jitter of 100ms to 200ms.
  EXPECT_GE(delay_2, Duration::Milliseconds(200));
  EXPECT_LE(delay_2, Duration::Milliseconds(300));
}

TEST_F(BdpEstimatorTest, AccessorsAndBandwidth) {
  constexpr int64_t kIncomingBytes1 = 100000;
  constexpr int64_t kIncomingBytes2 = 900000;
  constexpr int kTimeIncrementSeconds = 10;
  ExecCtx exec_ctx;
  // Verify accumulator, EstimateBandwidth, and EstimateBdp.
  // Add incoming bytes and verify accumulator accessor value is correct.
  est.AddIncomingBytes(kIncomingBytes1);
  EXPECT_EQ(est.accumulator(), kIncomingBytes1);

  // Schedule a ping. This resets the accumulator to zero.
  est.SchedulePing();
  EXPECT_EQ(est.accumulator(), 0);

  // Start the ping and add incoming bytes during the ping.
  est.StartPing();
  est.AddIncomingBytes(kIncomingBytes2);
  EXPECT_EQ(est.accumulator(), kIncomingBytes2);

  // Advance the clock by 10 units. Each unit is 1 second.
  increment_time(/*seconds=*/kTimeIncrementSeconds);
  est.CompletePing();

  // Verify that bandwidth matches accumulator divided by duration.
  EXPECT_DOUBLE_EQ(
      est.EstimateBandwidth(),
      static_cast<double>(kIncomingBytes2) / kTimeIncrementSeconds);
}

TEST_F(BdpEstimatorTest, EstimateIncreaseAndDelayDecrease) {
  ExecCtx exec_ctx;
  // Verify estimate increases and delay decreases.
  // Schedule and start the ping.
  est.SchedulePing();
  est.StartPing();

  // Add incoming bytes greater than 2/3 of the initial estimate (65536).
  est.AddIncomingBytes(100000);
  increment_time(/*seconds=*/1);
  const Timestamp next_time = est.CompletePing();

  // The estimate should double.
  EXPECT_EQ(est.EstimateBdp(), 2 * kInitialBdpDefault);

  // The delay should run faster and halve to 50ms.
  const Duration delay = next_time - Timestamp::Now();
  EXPECT_EQ(delay, Duration::Milliseconds(kDefaultInterPingDelayMillis / 2));
}

TEST_F(BdpEstimatorTest, EstimateIncreaseToAccumulator) {
  ExecCtx exec_ctx;
  // Verify that if the accumulator is greater than 2 * estimate, the new
  // estimate becomes the accumulator value.
  int64_t next_accumulator = kInitialBdpDefault;
  for (int i = 0; i < 3; ++i) {
    est.SchedulePing();
    est.StartPing();

    // Add incoming bytes greater than 2 * current estimate
    next_accumulator = 2 * next_accumulator + 1;
    est.AddIncomingBytes(next_accumulator);
    increment_time(/*seconds=*/1);
    est.CompletePing();

    // The estimate should be exactly the accumulator.
    EXPECT_EQ(est.EstimateBdp(), next_accumulator);
  }
}

TEST_F(BdpEstimatorTest, HighAccumulatorLowBandwidth) {
  ExecCtx exec_ctx;
  // First ping to establish a baseline bandwidth estimate.
  est.SchedulePing();
  est.StartPing();
  constexpr int64_t kBandwidth = 100000;
  est.AddIncomingBytes(kBandwidth);
  increment_time(/*seconds=*/1);
  est.CompletePing();

  // Estimate should double, and bandwidth estimate is 100000 bytes/sec.
  const int64_t expected_estimate = 2 * kInitialBdpDefault;
  EXPECT_EQ(est.EstimateBdp(), expected_estimate);

  // Run 10 randomized iterations varying the accumulator and seconds, but
  // keeping bandwidth (accumulator / seconds) exactly the same. The estimate
  // should remain untouched because the bandwidth did not strictly increase.
  for (int i = 0; i < 10; ++i) {
    est.SchedulePing();
    est.StartPing();

    // Randomize ping duration between 1 and 3600 seconds
    const uint16_t seconds = 1 + (rand() % 3600);

    // Keep the bandwidth exactly equal to kBandwidth.
    const int64_t accumulator = kBandwidth * seconds;

    // Ensure the accumulator is large enough (> 2/3 of estimate) so that
    // it would normally trigger an increase if the bandwidth was also larger.
    ASSERT_GT(accumulator, 2 * expected_estimate / 3);

    est.AddIncomingBytes(accumulator);
    increment_time(seconds);
    est.CompletePing();

    // Since the bandwidth did not strictly increase, BDP remains the same.
    EXPECT_EQ(est.EstimateBdp(), expected_estimate);
  }
}

TEST_F(BdpEstimatorTest, InterPingDelayCap) {
  ExecCtx exec_ctx;
  // Verify that the inter-ping delay is capped at 10 seconds.
  // Perform multiple steady pings in a loop to increase the delay.
  Timestamp next_time = Timestamp::Now();
  for (int i = 0; i < 200; ++i) {
    est.SchedulePing();
    est.StartPing();
    increment_time(/*seconds=*/1);
    next_time = est.CompletePing();
  }

  // Verify the final delay has not exceeded kMaxInterPingDelaySeconds + 200ms
  // jitter.
  const Duration delay = next_time - Timestamp::Now();
  EXPECT_GE(delay, Duration::Seconds(kMaxInterPingDelaySeconds));
  EXPECT_LE(delay, Duration::Seconds(kMaxInterPingDelaySeconds) +
                       Duration::Milliseconds(200));
}

namespace {
int64_t NextPow2(int64_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v |= v >> 32;
  v++;
  return v;
}
}  // namespace

class BdpEstimatorRandomTest : public BdpEstimatorTest,
                               public ::testing::WithParamInterface<size_t> {};

TEST_P(BdpEstimatorRandomTest, GetEstimateRandomValues) {
  ExecCtx exec_ctx;
  const int kMaxSample = 65535;
  int min = kMaxSample;
  int max = 0;
  for (size_t i = 0u; i < GetParam(); i++) {
    const int sample = rand() % (kMaxSample + 1);
    if (sample < min) min = sample;
    if (sample > max) max = sample;
    AddSample(sample);
    if (i >= 3u) {
      EXPECT_LE(est.EstimateBdp(),
                std::max(kInitialBdpDefault, 2 * NextPow2(max)))
          << " min:" << min << " max:" << max << " sample:" << sample;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(TooManyNames, BdpEstimatorRandomTest,
                         ::testing::Values(3, 4, 6, 9, 13, 19, 28, 42, 63, 94,
                                           141, 211, 316, 474, 711));

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  gpr_now_impl = grpc_core::testing::fake_gpr_now;
  grpc_init();
  grpc_timer_manager_set_threading(false);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
