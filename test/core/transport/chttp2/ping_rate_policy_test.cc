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

#include "gtest/gtest.h"

namespace grpc_core {
namespace {

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
  EXPECT_EQ(policy.RequestSendPing(Duration::Milliseconds(100)), SendGranted());
}

TEST(PingRatePolicy, ClientBlockedUntilDataSent) {
  Chttp2PingRatePolicy policy{ChannelArgs(), true};
  EXPECT_EQ(policy.RequestSendPing(Duration::Milliseconds(10)),
            TooManyRecentPings());
  policy.ResetPingsBeforeDataRequired();
  EXPECT_EQ(policy.RequestSendPing(Duration::Milliseconds(10)), SendGranted());
  EXPECT_EQ(policy.RequestSendPing(Duration::Zero()), SendGranted());
  EXPECT_EQ(policy.RequestSendPing(Duration::Zero()), TooManyRecentPings());
}

TEST(PingRatePolicy, RateThrottlingWorks) {
  Chttp2PingRatePolicy policy{ChannelArgs(), false};
  // Observe that we can fail if we send in a tight loop
  while (policy.RequestSendPing(Duration::Milliseconds(10)) == SendGranted()) {
  }
  // Observe that we can succeed if we wait a bit between pings
  for (int i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(policy.RequestSendPing(Duration::Milliseconds(10)),
              SendGranted());
  }
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
