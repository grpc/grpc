// Copyright 2023 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/ping_rate_policy.h"

#include <chrono>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/experiments/experiments.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

using ::testing::PrintToString;

Chttp2PingRatePolicy::RequestSendPingResult SendGranted() {
  return Chttp2PingRatePolicy::SendGranted{};
}

Chttp2PingRatePolicy::RequestSendPingResult TooManyRecentPings() {
  return Chttp2PingRatePolicy::TooManyRecentPings{};
}

TEST(PingRatePolicy, NoOpClient) {
  Chttp2PingRatePolicy policy{ChannelArgs(), true};
  EXPECT_EQ(policy.TestOnlyMaxPingsWithoutData(), 2);
}

TEST(PingRatePolicy, NoOpServer) {
  Chttp2PingRatePolicy policy{ChannelArgs(), false};
  EXPECT_EQ(policy.TestOnlyMaxPingsWithoutData(), 0);
}

TEST(PingRatePolicy, ServerCanSendAtStart) {
  Chttp2PingRatePolicy policy{ChannelArgs(), false};
  EXPECT_EQ(policy.RequestSendPing(Duration::Milliseconds(100), 0),
            SendGranted());
}

TEST(PingRatePolicy, ClientBlockedUntilDataSent) {
  if (IsMaxPingsWoDataThrottleEnabled()) {
    GTEST_SKIP()
        << "Pings are not blocked if max_pings_wo_data_throttle is enabled.";
  }
  Chttp2PingRatePolicy policy{ChannelArgs(), true};
  EXPECT_EQ(policy.RequestSendPing(Duration::Milliseconds(10), 0),
            TooManyRecentPings());
  policy.ResetPingsBeforeDataRequired();
  EXPECT_EQ(policy.RequestSendPing(Duration::Milliseconds(10), 0),
            SendGranted());
  policy.SentPing();
  EXPECT_EQ(policy.RequestSendPing(Duration::Zero(), 0), SendGranted());
  policy.SentPing();
  EXPECT_EQ(policy.RequestSendPing(Duration::Zero(), 0), TooManyRecentPings());
}

MATCHER_P2(IsWithinRange, lo, hi,
           absl::StrCat(negation ? "isn't" : "is", " between ",
                        PrintToString(lo), " and ", PrintToString(hi))) {
  return lo <= arg && arg <= hi;
}

TEST(PingRatePolicy, ClientThrottledUntilDataSent) {
  if (!IsMaxPingsWoDataThrottleEnabled()) {
    GTEST_SKIP()
        << "Throttling behavior is enabled with max_pings_wo_data_throttle.";
  }
  Chttp2PingRatePolicy policy{ChannelArgs(), true};
  // First ping is allowed.
  EXPECT_EQ(policy.RequestSendPing(Duration::Milliseconds(10), 0),
            SendGranted());
  policy.SentPing();
  // Second ping is throttled since no data has been sent.
  auto result = policy.RequestSendPing(Duration::Zero(), 0);
  EXPECT_TRUE(std::holds_alternative<Chttp2PingRatePolicy::TooSoon>(result));
  EXPECT_THAT(std::get<Chttp2PingRatePolicy::TooSoon>(result).wait,
              IsWithinRange(Duration::Seconds(59), Duration::Minutes(1)));
  policy.ResetPingsBeforeDataRequired();
  // After resetting pings before data required (data sent), we can send pings
  // without being throttled.
  EXPECT_EQ(policy.RequestSendPing(Duration::Zero(), 0), SendGranted());
  policy.SentPing();
  EXPECT_EQ(policy.RequestSendPing(Duration::Zero(), 0), SendGranted());
  policy.SentPing();
  // After reaching limit, we are throttled again.
  result = policy.RequestSendPing(Duration::Zero(), 0);
  EXPECT_TRUE(std::holds_alternative<Chttp2PingRatePolicy::TooSoon>(result));
  EXPECT_THAT(std::get<Chttp2PingRatePolicy::TooSoon>(result).wait,
              IsWithinRange(Duration::Seconds(59), Duration::Minutes(1)));
}

TEST(PingRatePolicy, RateThrottlingWorks) {
  Chttp2PingRatePolicy policy{ChannelArgs(), false};
  // Observe that we can fail if we send in a tight loop
  while (policy.RequestSendPing(Duration::Milliseconds(10), 0) ==
         SendGranted()) {
    policy.SentPing();
  }
  // Observe that we can succeed if we wait a bit between pings
  for (int i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(policy.RequestSendPing(Duration::Milliseconds(10), 0),
              SendGranted());
    policy.SentPing();
  }
}

TEST(PingRatePolicy, TooManyPingsInflightBlocksSendingPings) {
  Chttp2PingRatePolicy policy{ChannelArgs(), false};
  EXPECT_EQ(policy.RequestSendPing(Duration::Milliseconds(1), 100000000),
            TooManyRecentPings());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
