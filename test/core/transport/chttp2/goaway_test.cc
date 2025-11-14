//
//
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
//
//

#include "src/core/ext/transport/chttp2/transport/goaway.h"

#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "test/core/call/yodel/yodel_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {
using ::grpc_core::http2::GoawayInterface;
using ::grpc_core::http2::GoawayManager;
using ::testing::MockFunction;
using ::testing::StrictMock;

class MockGoawayInterface : public GoawayInterface {
 public:
  MOCK_METHOD(Promise<absl::Status>, SendPingAndWaitForAck, (), (override));
  MOCK_METHOD(void, TriggerWriteCycle, (), (override));
  MOCK_METHOD(uint32_t, GetLastAcceptedStreamId, (), (override));

  ::testing::Sequence trigger_write_cycle_seq_;
  ::testing::Sequence send_ping_and_wait_for_ack_seq_;

  template <typename Factory>
  void ExpectSendPingAndWaitForAck(Factory on_ping_sent) {
    EXPECT_CALL(*this, SendPingAndWaitForAck)
        .InSequence(send_ping_and_wait_for_ack_seq_)
        .WillOnce(([on_ping_sent = std::move(on_ping_sent)]() mutable {
          LOG(INFO) << "MockGoawayInterface SendPingAndWaitForAck Polled";
          return on_ping_sent();
        }));
  }

  void ExpectTriggerWriteCycle() {
    EXPECT_CALL(*this, TriggerWriteCycle)
        .InSequence(trigger_write_cycle_seq_)
        .WillOnce(([]() {
          LOG(INFO) << "MockGoawayInterface TriggerWriteCycle Polled";
        }));
  }

  void ExpectGetLastAcceptedStreamId(uint32_t last_accepted_stream_id) {
    EXPECT_CALL(*this, GetLastAcceptedStreamId)
        .WillOnce(([last_accepted_stream_id]() {
          LOG(INFO) << "MockGoawayInterface GetLastAcceptedStreamId Called";
          return last_accepted_stream_id;
        }));
  }
};

class GoawayTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  Party* GetParty() { return party_.get(); }
  void InitParty() {
    auto party_arena = SimpleArenaAllocator(0)->MakeArena();
    party_arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine().get());
    party_ = Party::Make(std::move(party_arena));
  }

  ChannelArgs GetChannelArgs() {
    return CoreConfiguration::Get()
        .channel_args_preconditioning()
        .PreconditionChannelArgs(nullptr);
  }

  void SpawnGoawayRequest(GoawayManager& goaway_manager, const bool immediate,
                          const http2::Http2ErrorCode error_code,
                          absl::string_view debug_data,
                          const uint32_t last_good_stream_id,
                          absl::AnyInvocable<void(absl::Status)> on_done_cb) {
    GetParty()->Spawn(
        "GoawayRequest",
        TrySeq(goaway_manager.RequestGoaway(error_code,
                                            Slice::FromCopiedString(debug_data),
                                            last_good_stream_id, immediate)),
        [&, on_done_cb = std::move(on_done_cb)](absl::Status status) mutable {
          EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                    http2::GoawayState::kDone);
          on_done_cb(std::move(status));
          LOG(INFO) << "Reached GoawayRequest end";
        });
  }

 private:
  void InitCoreConfiguration() override {}
  void InitTest() override { InitParty(); }
  void Shutdown() override { party_.reset(); }

  RefCountedPtr<Party> party_;
};
constexpr bool kImmediate = true;
constexpr bool kNotImmediate = false;
constexpr uint32_t kLastGoodStreamId = 123;
constexpr uint32_t kLastGoodStreamId2 = 455;
constexpr http2::Http2ErrorCode kNoErrorCode = http2::Http2ErrorCode::kNoError;
constexpr http2::Http2ErrorCode kProtocolError =
    http2::Http2ErrorCode::kProtocolError;
constexpr absl::string_view kImmediateDebugData = "immediate_goaway_request";
constexpr absl::string_view kImmediateDebugData2 = "immediate_goaway_request2";
constexpr absl::string_view kGracefulDebugData = "graceful_goaway_request";
}  // namespace

// No-op test.
YODEL_TEST(GoawayTest, NoOp) {}

// Tests that an immediate GOAWAY request works as expected.
// The test asserts the following:
// 1. An immediate GOAWAY request triggers a write cycle.
// 2. The GOAWAY frame is created with the correct parameters.
// 3. The GOAWAY state transitions to kDone after the GOAWAY frame is sent.
YODEL_TEST(GoawayTest, ImmediateGoawayWorks) {
  auto goaway_interface = std::make_unique<StrictMock<MockGoawayInterface>>();
  auto* mock_goaway_interface = goaway_interface.get();
  GoawayManager goaway_manager(std::move(goaway_interface));

  EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(), http2::GoawayState::kIdle);

  mock_goaway_interface->ExpectTriggerWriteCycle();

  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));

  SpawnGoawayRequest(goaway_manager, kImmediate,
                     http2::Http2ErrorCode::kNoError, kImmediateDebugData,
                     kLastGoodStreamId,
                     [&](absl::Status status) { on_done.Call(status); });

  GetParty()->Spawn(
      "transport-write-cycle",
      [&]() {
        std::optional<Http2Frame> goaway_frame =
            goaway_manager.TestOnlyMaybeGetGoawayFrame();
        // Validate the goaway frame.
        EXPECT_TRUE(goaway_frame.has_value());
        Http2Frame expected_goaway_frame = Http2GoawayFrame{
            /*last_stream_id=*/kLastGoodStreamId,
            /*error_code=*/static_cast<uint32_t>(kNoErrorCode),
            /*debug_data=*/Slice::FromCopiedString(kImmediateDebugData)};
        EXPECT_EQ(goaway_frame, expected_goaway_frame);
        EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                  http2::GoawayState::kImmediateGoawayRequested);
        goaway_manager.NotifyGoawaySent();

        EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                  http2::GoawayState::kDone);
        return absl::OkStatus();
      },
      [](auto) {});

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

// Tests that when multiple immediate GOAWAY requests are made, the first one
// takes precedence.
// The test asserts the following:
// 1. The GOAWAY frame is created with the parameters from the first request.
// 2. The GOAWAY state transitions to kDone after the GOAWAY frame is sent.
// 3. Both GOAWAY request promises are resolved.
YODEL_TEST(GoawayTest, MultipleImmediateGoawayRequests) {
  auto goaway_interface = std::make_unique<StrictMock<MockGoawayInterface>>();
  auto* mock_goaway_interface = goaway_interface.get();
  GoawayManager goaway_manager(std::move(goaway_interface));

  EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(), http2::GoawayState::kIdle);

  mock_goaway_interface->ExpectTriggerWriteCycle();

  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus())).Times(2);

  SpawnGoawayRequest(goaway_manager, kImmediate, kProtocolError,
                     kImmediateDebugData, kLastGoodStreamId,
                     [&](absl::Status status) { on_done.Call(status); });
  SpawnGoawayRequest(goaway_manager, kImmediate, kNoErrorCode,
                     kImmediateDebugData2, kLastGoodStreamId,
                     [&](absl::Status status) { on_done.Call(status); });

  GetParty()->Spawn(
      "transport-write-cycle",
      [&]() {
        std::optional<Http2Frame> goaway_frame =
            goaway_manager.TestOnlyMaybeGetGoawayFrame();
        // Validate the goaway frame.
        EXPECT_TRUE(goaway_frame.has_value());
        Http2Frame expected_goaway_frame = Http2GoawayFrame{
            /*last_stream_id=*/kLastGoodStreamId,
            /*error_code=*/static_cast<uint32_t>(kProtocolError),
            /*debug_data=*/Slice::FromCopiedString(kImmediateDebugData)};
        EXPECT_EQ(goaway_frame, expected_goaway_frame);
        EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                  http2::GoawayState::kImmediateGoawayRequested);
        goaway_manager.NotifyGoawaySent();

        EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                  http2::GoawayState::kDone);
      },
      [](auto) {});

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

// Tests that a graceful GOAWAY request works as expected.
// The test asserts the following:
// 1. A graceful GOAWAY request triggers a write cycle and sends a ping.
// 2. The first GOAWAY frame is sent with max stream ID.
// 3. After the ping ack is received, a second GOAWAY frame is sent with the
//    correct last good stream id.
// 4. The GOAWAY state transitions correctly from kIdle ->
//    kInitialGracefulGoawayScheduled -> kFinalGracefulGoawayScheduled -> kDone.
YODEL_TEST(GoawayTest, GracefulGoawayWorks) {
  Latch<void> goaway1_sent;
  Latch<void> goaway2_ready_to_send;

  auto goaway_interface = std::make_unique<StrictMock<MockGoawayInterface>>();
  auto* mock_goaway_interface = goaway_interface.get();

  mock_goaway_interface->ExpectSendPingAndWaitForAck([&]() {
    return Map(goaway1_sent.Wait(), [&](Empty) {
      goaway2_ready_to_send.Set();
      return absl::OkStatus();
    });
  });
  mock_goaway_interface->ExpectTriggerWriteCycle();
  mock_goaway_interface->ExpectTriggerWriteCycle();
  mock_goaway_interface->ExpectGetLastAcceptedStreamId(kLastGoodStreamId2);

  GoawayManager goaway_manager(std::move(goaway_interface));
  EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(), http2::GoawayState::kIdle);

  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));

  SpawnGoawayRequest(goaway_manager, kNotImmediate, kNoErrorCode,
                     kGracefulDebugData, kLastGoodStreamId,
                     [&](absl::Status status) { on_done.Call(status); });

  GetParty()->Spawn(
      "send-goaway1",
      [&]() {
        // GOAWAY #1 sent out.
        std::optional<Http2Frame> goaway_frame =
            goaway_manager.TestOnlyMaybeGetGoawayFrame();
        // Validate the goaway frame.
        EXPECT_TRUE(goaway_frame.has_value());
        Http2Frame expected_goaway_frame = Http2GoawayFrame{
            /*last_stream_id=*/RFC9113::kMaxStreamId31Bit,
            /*error_code=*/
            static_cast<uint32_t>(http2::Http2ErrorCode::kNoError),
            /*debug_data=*/Slice::FromCopiedString(kGracefulDebugData)};
        EXPECT_EQ(goaway_frame, expected_goaway_frame);
        EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                  http2::GoawayState::kInitialGracefulGoawayScheduled);
        goaway_manager.NotifyGoawaySent();
        EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                  http2::GoawayState::kInitialGracefulGoawayScheduled);
        goaway1_sent.Set();
        return Empty{};
      },
      [](auto) {});

  GetParty()->Spawn(
      "send-goaway2",
      TrySeq(goaway2_ready_to_send.Wait(),
             [&]() {
               // GOAWAY #2 sending out.
               std::optional<Http2Frame> goaway_frame =
                   goaway_manager.TestOnlyMaybeGetGoawayFrame();
               // Validate the goaway frame.
               EXPECT_TRUE(goaway_frame.has_value());
               Http2Frame expected_goaway_frame = Http2GoawayFrame{
                   /*last_stream_id=*/kLastGoodStreamId2,
                   /*error_code=*/static_cast<uint32_t>(kNoErrorCode),
                   /*debug_data=*/Slice::FromCopiedString(kGracefulDebugData)};
               EXPECT_EQ(goaway_frame, expected_goaway_frame);
               EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                         http2::GoawayState::kFinalGracefulGoawayScheduled);
               goaway_manager.NotifyGoawaySent();
               EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                         http2::GoawayState::kDone);
               return absl::OkStatus();
             }),
      [&](auto) {});

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

// Tests that an immediate GOAWAY request takes precedence over a graceful
// GOAWAY request when the graceful GOAWAY is in the
// kInitialGracefulGoawayScheduled state. The test asserts the following:
// 1. The first graceful GOAWAY frame is sent.
// 2. An immediate GOAWAY request is made.
// 3. The next GOAWAY frame sent is the one from the immediate request.
// 4. The GOAWAY state transitions to kDone.
// 5. Both GOAWAY request promises are resolved.
YODEL_TEST(GoawayTest, ImmediateGoawayTakesPrecedenceOverGracefulGoaway1) {
  Latch<void> goaway1_sent;
  Latch<void> goaway2_ready_to_send;
  Latch<void> never_resolved;

  auto goaway_interface = std::make_unique<StrictMock<MockGoawayInterface>>();
  auto* mock_goaway_interface = goaway_interface.get();

  mock_goaway_interface->ExpectSendPingAndWaitForAck([&]() {
    return Map(never_resolved.Wait(), [](Empty) {
      Crash("Unreached here");
      return absl::OkStatus();
    });
  });
  mock_goaway_interface->ExpectTriggerWriteCycle();
  mock_goaway_interface->ExpectTriggerWriteCycle();

  GoawayManager goaway_manager(std::move(goaway_interface));
  EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(), http2::GoawayState::kIdle);

  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus())).Times(2);

  SpawnGoawayRequest(goaway_manager, kNotImmediate, kNoErrorCode,
                     kGracefulDebugData, kLastGoodStreamId,
                     [&](absl::Status status) { on_done.Call(status); });
  GetParty()->Spawn(
      "goaway-request2",
      [&]() {
        return TrySeq(
            goaway1_sent.Wait(),
            [&](auto) {
              goaway2_ready_to_send.Set();
              return goaway_manager.RequestGoaway(
                  kProtocolError, Slice::FromCopiedString(kImmediateDebugData),
                  kLastGoodStreamId,
                  /*immediate=*/true);
            },
            [&]() {
              on_done.Call(absl::OkStatus());
              return absl::OkStatus();
            });
      },
      [](auto) {});

  GetParty()->Spawn(
      "send-goaway1",
      [&]() {
        // GOAWAY #1 sent out.
        std::optional<Http2Frame> goaway_frame =
            goaway_manager.TestOnlyMaybeGetGoawayFrame();
        // Validate the goaway frame.
        EXPECT_TRUE(goaway_frame.has_value());
        Http2Frame expected_goaway_frame = Http2GoawayFrame{
            /*last_stream_id=*/RFC9113::kMaxStreamId31Bit,
            /*error_code=*/
            static_cast<uint32_t>(kNoErrorCode),
            /*debug_data=*/Slice::FromCopiedString(kGracefulDebugData)};
        EXPECT_EQ(goaway_frame, expected_goaway_frame);
        goaway_manager.NotifyGoawaySent();
        EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                  http2::GoawayState::kInitialGracefulGoawayScheduled);
        goaway1_sent.Set();
        return Empty{};
      },
      [](auto) {});

  GetParty()->Spawn(
      "send-goaway2",
      TrySeq(goaway2_ready_to_send.Wait(),
             [&]() {
               // GOAWAY #2 sending out.
               std::optional<Http2Frame> goaway_frame =
                   goaway_manager.TestOnlyMaybeGetGoawayFrame();
               // Validate the goaway frame.
               EXPECT_TRUE(goaway_frame.has_value());
               Http2Frame expected_goaway_frame = Http2GoawayFrame{
                   /*last_stream_id=*/kLastGoodStreamId,
                   /*error_code=*/static_cast<uint32_t>(kProtocolError),
                   /*debug_data=*/Slice::FromCopiedString(kImmediateDebugData)};
               EXPECT_EQ(goaway_frame, expected_goaway_frame);
               goaway_manager.NotifyGoawaySent();
               EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                         http2::GoawayState::kDone);
               return absl::OkStatus();
             }),
      [&](auto) {});

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

// Tests that an immediate GOAWAY request takes precedence over a graceful
// GOAWAY request when the graceful GOAWAY is in the
// kFinalGracefulGoawayScheduled state. The test asserts the following:
// 1. The first graceful GOAWAY frame is sent and the ping ack is received.
// 2. An immediate GOAWAY request is made.
// 3. The next GOAWAY frame sent is the one from the immediate request.
// 4. The GOAWAY state transitions to kDone.
// 5. Both GOAWAY request promises are resolved.
YODEL_TEST(GoawayTest, ImmediateGoawayTakesPrecedenceOverGracefulGoaway2) {
  Latch<void> goaway1_sent;
  Latch<void> goaway2_ready_to_send;
  Latch<void> goaway3;

  auto goaway_interface = std::make_unique<StrictMock<MockGoawayInterface>>();
  auto* mock_goaway_interface = goaway_interface.get();

  mock_goaway_interface->ExpectSendPingAndWaitForAck([&]() {
    return Map(goaway1_sent.Wait(), [&](Empty) {
      goaway2_ready_to_send.Set();
      return absl::OkStatus();
    });
  });
  mock_goaway_interface->ExpectTriggerWriteCycle();
  mock_goaway_interface->ExpectTriggerWriteCycle();
  mock_goaway_interface->ExpectTriggerWriteCycle();

  GoawayManager goaway_manager(std::move(goaway_interface));
  EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(), http2::GoawayState::kIdle);

  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus())).Times(2);

  SpawnGoawayRequest(goaway_manager, kNotImmediate, kNoErrorCode,
                     kGracefulDebugData, kLastGoodStreamId,
                     [&](absl::Status status) { on_done.Call(status); });
  GetParty()->Spawn(
      "goaway-request2",
      [&]() {
        return TrySeq(
            goaway2_ready_to_send.Wait(),
            [&](auto) {
              goaway3.Set();
              return goaway_manager.RequestGoaway(
                  kProtocolError, Slice::FromCopiedString(kImmediateDebugData),
                  kLastGoodStreamId,
                  /*immediate=*/true);
            },
            [&]() {
              on_done.Call(absl::OkStatus());
              return absl::OkStatus();
            });
      },
      [](auto) {});

  GetParty()->Spawn(
      "send-goaway1",
      [&]() {
        // GOAWAY #1 sent out.
        std::optional<Http2Frame> goaway_frame =
            goaway_manager.TestOnlyMaybeGetGoawayFrame();
        // Validate the goaway frame.
        EXPECT_TRUE(goaway_frame.has_value());
        Http2Frame expected_goaway_frame = Http2GoawayFrame{
            /*last_stream_id=*/RFC9113::kMaxStreamId31Bit,
            /*error_code=*/
            static_cast<uint32_t>(kNoErrorCode),
            /*debug_data=*/Slice::FromCopiedString(kGracefulDebugData)};
        EXPECT_EQ(goaway_frame, expected_goaway_frame);
        EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                  http2::GoawayState::kInitialGracefulGoawayScheduled);
        goaway_manager.NotifyGoawaySent();
        EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                  http2::GoawayState::kInitialGracefulGoawayScheduled);
        goaway1_sent.Set();
        return Empty{};
      },
      [](auto) {});

  GetParty()->Spawn(
      "send-goaway2",
      TrySeq(goaway3.Wait(),
             [&]() {
               // GOAWAY #2 sending out.
               std::optional<Http2Frame> goaway_frame =
                   goaway_manager.TestOnlyMaybeGetGoawayFrame();
               // Validate the goaway frame.
               EXPECT_TRUE(goaway_frame.has_value());
               Http2Frame expected_goaway_frame = Http2GoawayFrame{
                   /*last_stream_id=*/kLastGoodStreamId,
                   /*error_code=*/static_cast<uint32_t>(kProtocolError),
                   /*debug_data=*/Slice::FromCopiedString(kImmediateDebugData)};
               EXPECT_EQ(goaway_frame, expected_goaway_frame);
               EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                         http2::GoawayState::kImmediateGoawayRequested);
               goaway_manager.NotifyGoawaySent();
               EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                         http2::GoawayState::kDone);
               return absl::OkStatus();
             }),
      [&](auto) {});

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

}  // namespace grpc_core
