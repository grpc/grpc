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

#include "src/core/ext/transport/chttp2/transport/ping_abuse_policy.h"

#include <chrono>
#include <thread>

#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>

namespace grpc_core {
namespace {

TEST(PingAbusePolicy, NoOp) {
  Chttp2PingAbusePolicy policy{ChannelArgs()};
  EXPECT_EQ(policy.TestOnlyMaxPingStrikes(), 2);
  EXPECT_EQ(policy.TestOnlyMinPingIntervalWithoutData(), Duration::Minutes(5));
}

TEST(PingAbusePolicy, WithChannelArgs) {
  Chttp2PingAbusePolicy policy{
      ChannelArgs()
          .Set(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 100)
          .Set(GRPC_ARG_HTTP2_MAX_PING_STRIKES, 42)};
  EXPECT_EQ(policy.TestOnlyMaxPingStrikes(), 42);
  EXPECT_EQ(policy.TestOnlyMinPingIntervalWithoutData(),
            Duration::Milliseconds(100));
}

TEST(PingAbusePolicy, ChannelArgsRangeCheck) {
  Chttp2PingAbusePolicy policy{
      ChannelArgs()
          .Set(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, -1000)
          .Set(GRPC_ARG_HTTP2_MAX_PING_STRIKES, -100)};
  EXPECT_EQ(policy.TestOnlyMaxPingStrikes(), 0);
  EXPECT_EQ(policy.TestOnlyMinPingIntervalWithoutData(), Duration::Zero());
}

TEST(PingAbusePolicy, BasicOut) {
  Chttp2PingAbusePolicy policy{ChannelArgs()};
  EXPECT_EQ(policy.TestOnlyMaxPingStrikes(), 2);
  // First ping ok
  EXPECT_FALSE(policy.ReceivedOnePing(false));
  // Strike 1... too soon
  EXPECT_FALSE(policy.ReceivedOnePing(false));
  // Strike 2... too soon
  EXPECT_FALSE(policy.ReceivedOnePing(false));
  // Strike 3... you're out!
  EXPECT_TRUE(policy.ReceivedOnePing(false));
}

TEST(PingAbusePolicy, TimePreventsOut) {
  Chttp2PingAbusePolicy policy{ChannelArgs().Set(
      GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 1000)};
  EXPECT_EQ(policy.TestOnlyMaxPingStrikes(), 2);
  // First ping ok
  EXPECT_FALSE(policy.ReceivedOnePing(false));
  // Strike 1... too soon
  EXPECT_FALSE(policy.ReceivedOnePing(false));
  // Strike 2... too soon
  EXPECT_FALSE(policy.ReceivedOnePing(false));
  // Sleep a bit, allowed
  std::this_thread::sleep_for(std::chrono::seconds(2));
  EXPECT_FALSE(policy.ReceivedOnePing(false));
}

TEST(PingAbusePolicy, TimerSustains) {
  Chttp2PingAbusePolicy policy{ChannelArgs().Set(
      GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 10)};
  EXPECT_EQ(policy.TestOnlyMaxPingStrikes(), 2);
  for (int i = 0; i < 100; i++) {
    EXPECT_FALSE(policy.ReceivedOnePing(false));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

TEST(PingAbusePolicy, IdleIncreasesTimeout) {
  Chttp2PingAbusePolicy policy{ChannelArgs().Set(
      GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 1000)};
  EXPECT_EQ(policy.TestOnlyMaxPingStrikes(), 2);
  // First ping ok
  EXPECT_FALSE(policy.ReceivedOnePing(true));
  // Strike 1... too soon
  EXPECT_FALSE(policy.ReceivedOnePing(true));
  // Strike 2... too soon
  EXPECT_FALSE(policy.ReceivedOnePing(true));
  // Sleep a bit, allowed
  std::this_thread::sleep_for(std::chrono::seconds(2));
  EXPECT_TRUE(policy.ReceivedOnePing(true));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
