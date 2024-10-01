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

#include "src/core/ext/transport/chttp2/transport/ping_callbacks.h"

#include <chrono>

#include "absl/random/random.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/util/crash.h"
#include "test/core/event_engine/mock_event_engine.h"

using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::MockEventEngine;
using testing::_;
using testing::Matcher;
using testing::Return;
using testing::StrictMock;

namespace grpc_core {
namespace {

TEST(PingCallbacksTest, RequestPingRequestsPing) {
  Chttp2PingCallbacks callbacks;
  EXPECT_FALSE(callbacks.ping_requested());
  callbacks.RequestPing();
  EXPECT_TRUE(callbacks.ping_requested());
}

TEST(PingCallbacksTest, OnPingRequestsPing) {
  Chttp2PingCallbacks callbacks;
  EXPECT_FALSE(callbacks.ping_requested());
  callbacks.OnPing([] {}, [] {});
  EXPECT_TRUE(callbacks.ping_requested());
}

TEST(PingCallbacksTest, OnPingAckRequestsPing) {
  Chttp2PingCallbacks callbacks;
  EXPECT_FALSE(callbacks.ping_requested());
  callbacks.OnPingAck([] {});
  EXPECT_TRUE(callbacks.ping_requested());
}

TEST(PingCallbacksTest, PingAckBeforeTimerStarted) {
  StrictMock<MockEventEngine> event_engine;
  absl::BitGen bitgen;
  Chttp2PingCallbacks callbacks;
  bool started = false;
  bool acked = false;
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_FALSE(callbacks.started_new_ping_without_setting_timeout());
  // Request ping
  callbacks.OnPing(
      [&started] {
        EXPECT_FALSE(started);
        started = true;
      },
      [&acked] {
        EXPECT_FALSE(acked);
        acked = true;
      });
  EXPECT_TRUE(callbacks.ping_requested());
  EXPECT_FALSE(callbacks.started_new_ping_without_setting_timeout());
  EXPECT_EQ(callbacks.pings_inflight(), 0);
  EXPECT_FALSE(started);
  EXPECT_FALSE(acked);
  auto id = callbacks.StartPing(bitgen);
  EXPECT_TRUE(callbacks.started_new_ping_without_setting_timeout());
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_EQ(callbacks.pings_inflight(), 1);
  EXPECT_TRUE(started);
  EXPECT_FALSE(acked);
  callbacks.AckPing(id, &event_engine);
  EXPECT_TRUE(callbacks.started_new_ping_without_setting_timeout());
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_EQ(callbacks.pings_inflight(), 0);
  EXPECT_TRUE(started);
  EXPECT_TRUE(acked);
  callbacks.OnPingTimeout(Duration::Milliseconds(1), &event_engine,
                          [] { Crash("should never reach here"); });
}

TEST(PingCallbacksTest, PingRoundtrips) {
  StrictMock<MockEventEngine> event_engine;
  absl::BitGen bitgen;
  Chttp2PingCallbacks callbacks;
  bool started = false;
  bool acked = false;
  EXPECT_FALSE(callbacks.ping_requested());
  // Request ping
  callbacks.OnPing(
      [&started] {
        EXPECT_FALSE(started);
        started = true;
      },
      [&acked] {
        EXPECT_FALSE(acked);
        acked = true;
      });
  EXPECT_TRUE(callbacks.ping_requested());
  EXPECT_EQ(callbacks.pings_inflight(), 0);
  EXPECT_FALSE(started);
  EXPECT_FALSE(acked);
  // Start ping should call the start methods, set a timeout, and clear the
  // request
  EXPECT_CALL(event_engine, RunAfter(EventEngine::Duration(Duration::Hours(24)),
                                     Matcher<absl::AnyInvocable<void()>>(_)))
      .WillOnce([]() { return EventEngine::TaskHandle{123, 456}; });
  auto id = callbacks.StartPing(bitgen);
  callbacks.OnPingTimeout(Duration::Hours(24), &event_engine,
                          [] { Crash("should not reach here"); });
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_EQ(callbacks.pings_inflight(), 1);
  EXPECT_TRUE(started);
  EXPECT_FALSE(acked);
  // Ack should cancel the timeout
  EXPECT_CALL(event_engine, Cancel(EventEngine::TaskHandle{123, 456}))
      .WillOnce(Return(true));
  EXPECT_TRUE(callbacks.AckPing(id, &event_engine));
  EXPECT_EQ(callbacks.pings_inflight(), 0);
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started);
  EXPECT_TRUE(acked);
}

TEST(PingCallbacksTest, PingRoundtripsWithInfiniteTimeout) {
  StrictMock<MockEventEngine> event_engine;
  absl::BitGen bitgen;
  Chttp2PingCallbacks callbacks;
  bool started = false;
  bool acked = false;
  EXPECT_FALSE(callbacks.ping_requested());
  // Request ping
  callbacks.OnPing(
      [&started] {
        EXPECT_FALSE(started);
        started = true;
      },
      [&acked] {
        EXPECT_FALSE(acked);
        acked = true;
      });
  EXPECT_TRUE(callbacks.ping_requested());
  EXPECT_EQ(callbacks.pings_inflight(), 0);
  EXPECT_FALSE(started);
  EXPECT_FALSE(acked);
  auto id = callbacks.StartPing(bitgen);
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_EQ(callbacks.pings_inflight(), 1);
  EXPECT_TRUE(started);
  EXPECT_FALSE(acked);
  EXPECT_TRUE(callbacks.AckPing(id, &event_engine));
  EXPECT_EQ(callbacks.pings_inflight(), 0);
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started);
  EXPECT_TRUE(acked);
}

TEST(PingCallbacksTest, InvalidPingIdFlagsError) {
  StrictMock<MockEventEngine> event_engine;
  Chttp2PingCallbacks callbacks;
  EXPECT_FALSE(callbacks.AckPing(1234, &event_engine));
}

TEST(PingCallbacksTest, DuplicatePingIdFlagsError) {
  StrictMock<MockEventEngine> event_engine;
  absl::BitGen bitgen;
  Chttp2PingCallbacks callbacks;
  bool started = false;
  bool acked = false;
  EXPECT_FALSE(callbacks.ping_requested());
  callbacks.OnPing(
      [&started] {
        EXPECT_FALSE(started);
        started = true;
      },
      [&acked] {
        EXPECT_FALSE(acked);
        acked = true;
      });
  EXPECT_TRUE(callbacks.ping_requested());
  EXPECT_FALSE(started);
  EXPECT_FALSE(acked);
  EXPECT_CALL(event_engine, RunAfter(EventEngine::Duration(Duration::Hours(24)),
                                     Matcher<absl::AnyInvocable<void()>>(_)))
      .WillOnce([]() { return EventEngine::TaskHandle{123, 456}; });
  auto id = callbacks.StartPing(bitgen);
  callbacks.OnPingTimeout(Duration::Hours(24), &event_engine,
                          [] { Crash("should not reach here"); });
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started);
  EXPECT_FALSE(acked);
  EXPECT_CALL(event_engine, Cancel(EventEngine::TaskHandle{123, 456}))
      .WillOnce(Return(true));
  EXPECT_TRUE(callbacks.AckPing(id, &event_engine));
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started);
  EXPECT_TRUE(acked);
  // Second ping ack on the same id should fail
  EXPECT_FALSE(callbacks.AckPing(id, &event_engine));
}

TEST(PingCallbacksTest, OnPingAckCanPiggybackInflightPings) {
  StrictMock<MockEventEngine> event_engine;
  absl::BitGen bitgen;
  Chttp2PingCallbacks callbacks;
  bool started = false;
  bool acked_first = false;
  bool acked_second = false;
  EXPECT_FALSE(callbacks.ping_requested());
  callbacks.OnPing(
      [&started] {
        EXPECT_FALSE(started);
        started = true;
      },
      [&acked_first] {
        EXPECT_FALSE(acked_first);
        acked_first = true;
      });
  EXPECT_TRUE(callbacks.ping_requested());
  EXPECT_FALSE(started);
  EXPECT_FALSE(acked_first);
  EXPECT_FALSE(acked_second);
  EXPECT_CALL(event_engine, RunAfter(EventEngine::Duration(Duration::Hours(24)),
                                     Matcher<absl::AnyInvocable<void()>>(_)))
      .WillOnce([]() { return EventEngine::TaskHandle{123, 456}; });
  auto id = callbacks.StartPing(bitgen);
  callbacks.OnPingTimeout(Duration::Hours(24), &event_engine,
                          [] { Crash("should not reach here"); });
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started);
  EXPECT_FALSE(acked_first);
  EXPECT_FALSE(acked_second);
  callbacks.OnPingAck([&acked_second] {
    EXPECT_FALSE(acked_second);
    acked_second = true;
  });
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started);
  EXPECT_FALSE(acked_first);
  EXPECT_FALSE(acked_second);
  EXPECT_CALL(event_engine, Cancel(EventEngine::TaskHandle{123, 456}))
      .WillOnce(Return(true));
  EXPECT_TRUE(callbacks.AckPing(id, &event_engine));
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started);
  EXPECT_TRUE(acked_first);
  EXPECT_TRUE(acked_second);
}

TEST(PingCallbacksTest, PingAckRoundtrips) {
  StrictMock<MockEventEngine> event_engine;
  absl::BitGen bitgen;
  Chttp2PingCallbacks callbacks;
  bool acked = false;
  EXPECT_FALSE(callbacks.ping_requested());
  callbacks.OnPingAck([&acked] {
    EXPECT_FALSE(acked);
    acked = true;
  });
  EXPECT_TRUE(callbacks.ping_requested());
  EXPECT_FALSE(acked);
  EXPECT_CALL(event_engine, RunAfter(EventEngine::Duration(Duration::Hours(24)),
                                     Matcher<absl::AnyInvocable<void()>>(_)))
      .WillOnce([]() { return EventEngine::TaskHandle{123, 456}; });
  auto id = callbacks.StartPing(bitgen);
  callbacks.OnPingTimeout(Duration::Hours(24), &event_engine,
                          [] { Crash("should not reach here"); });
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_FALSE(acked);
  EXPECT_CALL(event_engine, Cancel(EventEngine::TaskHandle{123, 456}))
      .WillOnce(Return(true));
  EXPECT_TRUE(callbacks.AckPing(id, &event_engine));
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(acked);
}

TEST(PingCallbacksTest, MultiPingRoundtrips) {
  StrictMock<MockEventEngine> event_engine;
  absl::BitGen bitgen;
  Chttp2PingCallbacks callbacks;
  bool started1 = false;
  bool acked1 = false;
  bool started2 = false;
  bool acked2 = false;
  EXPECT_FALSE(callbacks.ping_requested());
  callbacks.OnPing(
      [&started1] {
        EXPECT_FALSE(started1);
        started1 = true;
      },
      [&acked1] {
        EXPECT_FALSE(acked1);
        acked1 = true;
      });
  EXPECT_TRUE(callbacks.ping_requested());
  EXPECT_FALSE(started1);
  EXPECT_FALSE(acked1);
  EXPECT_FALSE(started2);
  EXPECT_FALSE(acked2);
  EXPECT_CALL(event_engine, RunAfter(EventEngine::Duration(Duration::Hours(24)),
                                     Matcher<absl::AnyInvocable<void()>>(_)))
      .WillOnce([]() { return EventEngine::TaskHandle{123, 456}; });
  auto id1 = callbacks.StartPing(bitgen);
  callbacks.OnPingTimeout(Duration::Hours(24), &event_engine,
                          [] { Crash("should not reach here"); });
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started1);
  EXPECT_FALSE(acked1);
  EXPECT_FALSE(started2);
  EXPECT_FALSE(acked2);
  callbacks.OnPing(
      [&started2] {
        EXPECT_FALSE(started2);
        started2 = true;
      },
      [&acked2] {
        EXPECT_FALSE(acked2);
        acked2 = true;
      });
  EXPECT_TRUE(callbacks.ping_requested());
  EXPECT_TRUE(started1);
  EXPECT_FALSE(acked1);
  EXPECT_FALSE(started2);
  EXPECT_FALSE(acked2);
  EXPECT_CALL(event_engine, RunAfter(EventEngine::Duration(Duration::Hours(24)),
                                     Matcher<absl::AnyInvocable<void()>>(_)))
      .WillOnce([]() { return EventEngine::TaskHandle{123, 789}; });
  auto id2 = callbacks.StartPing(bitgen);
  callbacks.OnPingTimeout(Duration::Hours(24), &event_engine,
                          [] { Crash("should not reach here"); });
  EXPECT_NE(id1, id2);
  EXPECT_TRUE(started1);
  EXPECT_FALSE(acked1);
  EXPECT_TRUE(started2);
  EXPECT_FALSE(acked2);
  EXPECT_CALL(event_engine, Cancel(EventEngine::TaskHandle{123, 456}))
      .WillOnce(Return(true));
  EXPECT_TRUE(callbacks.AckPing(id1, &event_engine));
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started1);
  EXPECT_TRUE(acked1);
  EXPECT_TRUE(started2);
  EXPECT_FALSE(acked2);
  EXPECT_CALL(event_engine, Cancel(EventEngine::TaskHandle{123, 789}))
      .WillOnce(Return(true));
  EXPECT_TRUE(callbacks.AckPing(id2, &event_engine));
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started1);
  EXPECT_TRUE(acked1);
  EXPECT_TRUE(started2);
  EXPECT_TRUE(acked2);
}

TEST(PingCallbacksTest, MultiPingRoundtripsWithOutOfOrderAcks) {
  StrictMock<MockEventEngine> event_engine;
  absl::BitGen bitgen;
  Chttp2PingCallbacks callbacks;
  bool started1 = false;
  bool acked1 = false;
  bool started2 = false;
  bool acked2 = false;
  EXPECT_FALSE(callbacks.ping_requested());
  callbacks.OnPing(
      [&started1] {
        EXPECT_FALSE(started1);
        started1 = true;
      },
      [&acked1] {
        EXPECT_FALSE(acked1);
        acked1 = true;
      });
  EXPECT_TRUE(callbacks.ping_requested());
  EXPECT_FALSE(started1);
  EXPECT_FALSE(acked1);
  EXPECT_FALSE(started2);
  EXPECT_FALSE(acked2);
  EXPECT_CALL(event_engine, RunAfter(EventEngine::Duration(Duration::Hours(24)),
                                     Matcher<absl::AnyInvocable<void()>>(_)))
      .WillOnce([]() { return EventEngine::TaskHandle{123, 456}; });
  auto id1 = callbacks.StartPing(bitgen);
  callbacks.OnPingTimeout(Duration::Hours(24), &event_engine,
                          [] { Crash("should not reach here"); });
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started1);
  EXPECT_FALSE(acked1);
  EXPECT_FALSE(started2);
  EXPECT_FALSE(acked2);
  callbacks.OnPing(
      [&started2] {
        EXPECT_FALSE(started2);
        started2 = true;
      },
      [&acked2] {
        EXPECT_FALSE(acked2);
        acked2 = true;
      });
  EXPECT_TRUE(callbacks.ping_requested());
  EXPECT_TRUE(started1);
  EXPECT_FALSE(acked1);
  EXPECT_FALSE(started2);
  EXPECT_FALSE(acked2);
  EXPECT_CALL(event_engine, RunAfter(EventEngine::Duration(Duration::Hours(24)),
                                     Matcher<absl::AnyInvocable<void()>>(_)))
      .WillOnce([]() { return EventEngine::TaskHandle{123, 789}; });
  auto id2 = callbacks.StartPing(bitgen);
  callbacks.OnPingTimeout(Duration::Hours(24), &event_engine,
                          [] { Crash("should not reach here"); });
  EXPECT_NE(id1, id2);
  EXPECT_TRUE(started1);
  EXPECT_FALSE(acked1);
  EXPECT_TRUE(started2);
  EXPECT_FALSE(acked2);
  EXPECT_CALL(event_engine, Cancel(EventEngine::TaskHandle{123, 789}))
      .WillOnce(Return(true));
  EXPECT_TRUE(callbacks.AckPing(id2, &event_engine));
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started1);
  EXPECT_FALSE(acked1);
  EXPECT_TRUE(started2);
  EXPECT_TRUE(acked2);
  EXPECT_CALL(event_engine, Cancel(EventEngine::TaskHandle{123, 456}))
      .WillOnce(Return(true));
  EXPECT_TRUE(callbacks.AckPing(id1, &event_engine));
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started1);
  EXPECT_TRUE(acked1);
  EXPECT_TRUE(started2);
  EXPECT_TRUE(acked2);
}

TEST(PingCallbacksTest, CoalescedPingsRoundtrip) {
  StrictMock<MockEventEngine> event_engine;
  absl::BitGen bitgen;
  Chttp2PingCallbacks callbacks;
  bool started1 = false;
  bool acked1 = false;
  bool started2 = false;
  bool acked2 = false;
  EXPECT_FALSE(callbacks.ping_requested());
  callbacks.OnPing(
      [&started1] {
        EXPECT_FALSE(started1);
        started1 = true;
      },
      [&acked1] {
        EXPECT_FALSE(acked1);
        acked1 = true;
      });
  callbacks.OnPing(
      [&started2] {
        EXPECT_FALSE(started2);
        started2 = true;
      },
      [&acked2] {
        EXPECT_FALSE(acked2);
        acked2 = true;
      });
  EXPECT_TRUE(callbacks.ping_requested());
  EXPECT_FALSE(started1);
  EXPECT_FALSE(acked1);
  EXPECT_FALSE(started2);
  EXPECT_FALSE(acked2);
  EXPECT_CALL(event_engine, RunAfter(EventEngine::Duration(Duration::Hours(24)),
                                     Matcher<absl::AnyInvocable<void()>>(_)))
      .WillOnce([]() { return EventEngine::TaskHandle{123, 456}; });
  auto id = callbacks.StartPing(bitgen);
  callbacks.OnPingTimeout(Duration::Hours(24), &event_engine,
                          [] { Crash("should not reach here"); });
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started1);
  EXPECT_FALSE(acked1);
  EXPECT_TRUE(started2);
  EXPECT_FALSE(acked2);
  EXPECT_CALL(event_engine, Cancel(EventEngine::TaskHandle{123, 456}))
      .WillOnce(Return(true));
  EXPECT_TRUE(callbacks.AckPing(id, &event_engine));
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started1);
  EXPECT_TRUE(acked1);
  EXPECT_TRUE(started2);
  EXPECT_TRUE(acked2);
}

TEST(PingCallbacksTest, CancelAllCancelsCallbacks) {
  StrictMock<MockEventEngine> event_engine;
  absl::BitGen bitgen;
  Chttp2PingCallbacks callbacks;
  bool started = false;
  bool acked = false;
  EXPECT_FALSE(callbacks.ping_requested());
  callbacks.OnPing(
      [&started] {
        EXPECT_FALSE(started);
        started = true;
      },
      [&acked] {
        EXPECT_FALSE(acked);
        acked = true;
      });
  EXPECT_TRUE(callbacks.ping_requested());
  callbacks.CancelAll(&event_engine);
  EXPECT_FALSE(started);
  EXPECT_FALSE(acked);
  EXPECT_FALSE(callbacks.ping_requested());
  // Can still send a ping, no callback should be invoked
  EXPECT_CALL(event_engine, RunAfter(EventEngine::Duration(Duration::Hours(24)),
                                     Matcher<absl::AnyInvocable<void()>>(_)))
      .WillOnce([]() { return EventEngine::TaskHandle{123, 456}; });
  auto id = callbacks.StartPing(bitgen);
  callbacks.OnPingTimeout(Duration::Hours(24), &event_engine,
                          [] { Crash("should not reach here"); });
  EXPECT_FALSE(started);
  EXPECT_FALSE(acked);
  EXPECT_CALL(event_engine, Cancel(EventEngine::TaskHandle{123, 456}))
      .WillOnce(Return(true));
  EXPECT_TRUE(callbacks.AckPing(id, &event_engine));
  EXPECT_FALSE(started);
  EXPECT_FALSE(acked);
  EXPECT_FALSE(callbacks.ping_requested());
}

TEST(PingCallbacksTest, CancelAllCancelsInflightPings) {
  StrictMock<MockEventEngine> event_engine;
  absl::BitGen bitgen;
  Chttp2PingCallbacks callbacks;
  bool started = false;
  bool acked = false;
  EXPECT_FALSE(callbacks.ping_requested());
  callbacks.OnPing(
      [&started] {
        EXPECT_FALSE(started);
        started = true;
      },
      [&acked] {
        EXPECT_FALSE(acked);
        acked = true;
      });
  EXPECT_TRUE(callbacks.ping_requested());
  EXPECT_FALSE(started);
  EXPECT_FALSE(acked);
  EXPECT_CALL(event_engine, RunAfter(EventEngine::Duration(Duration::Hours(24)),
                                     Matcher<absl::AnyInvocable<void()>>(_)))
      .WillOnce([]() { return EventEngine::TaskHandle{123, 456}; });
  auto id = callbacks.StartPing(bitgen);
  callbacks.OnPingTimeout(Duration::Hours(24), &event_engine,
                          [] { Crash("should not reach here"); });
  EXPECT_FALSE(callbacks.ping_requested());
  EXPECT_TRUE(started);
  EXPECT_FALSE(acked);
  EXPECT_CALL(event_engine, Cancel(EventEngine::TaskHandle{123, 456}))
      .WillOnce(Return(true));
  callbacks.CancelAll(&event_engine);
  // Ensure Cancel call comes from CancelAll
  ::testing::Mock::VerifyAndClearExpectations(&event_engine);
  EXPECT_FALSE(acked);
  EXPECT_FALSE(callbacks.ping_requested());
  // Ping should still be valid, but no callback should be invoked
  EXPECT_TRUE(callbacks.AckPing(id, &event_engine));
  EXPECT_FALSE(acked);
  EXPECT_FALSE(callbacks.ping_requested());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
