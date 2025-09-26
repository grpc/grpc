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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "test/core/call/yodel/yodel_test.h"

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
        TrySeq(goaway_manager.RequestGoaway(std::move(error_code),
                                            std::move(debug_data),
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
}  // namespace

YODEL_TEST(GoawayTest, NoOp) {}

YODEL_TEST(GoawayTest, ImmediateGoawayWorks) {
  auto goaway_interface = std::make_unique<StrictMock<MockGoawayInterface>>();
  auto* mock_goaway_interface = goaway_interface.get();
  GoawayManager goaway_manager(std::move(goaway_interface));
  constexpr uint32_t kLastGoodStreamId = 123;
  constexpr absl::string_view kDebugData = "immediate_goaway_request";
  constexpr http2::Http2ErrorCode kErrorCode = http2::Http2ErrorCode::kNoError;
  const bool kImmediate = true;

  EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(), http2::GoawayState::kIdle);

  mock_goaway_interface->ExpectTriggerWriteCycle();

  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));

  SpawnGoawayRequest(
      goaway_manager, kImmediate, http2::Http2ErrorCode::kNoError, kDebugData,
      kLastGoodStreamId, [&](absl::Status status) { on_done.Call(status); });

  std::optional<Http2Frame> goaway_frame =
      goaway_manager.TestOnlyMaybeGetGoawayFrame();
  // Validate the goaway frame.
  EXPECT_TRUE(goaway_frame.has_value());
  Http2Frame expected_goaway_frame =
      Http2GoawayFrame{/*last_stream_id=*/kLastGoodStreamId,
                       /*error_code=*/static_cast<uint32_t>(kErrorCode),
                       /*debug_data=*/Slice::FromCopiedString(kDebugData)};
  EXPECT_EQ(goaway_frame, expected_goaway_frame);
  goaway_manager.NotifyGoawaySent();

  EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(), http2::GoawayState::kDone);

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(GoawayTest, MultipleImmediateGoawayRequests) {
  auto goaway_interface = std::make_unique<StrictMock<MockGoawayInterface>>();
  auto* mock_goaway_interface = goaway_interface.get();
  GoawayManager goaway_manager(std::move(goaway_interface));
  constexpr uint32_t kLastGoodStreamId = 123;
  constexpr absl::string_view kDebugData2 = "immediate_goaway_request";
  constexpr http2::Http2ErrorCode kErrorCode2 = http2::Http2ErrorCode::kNoError;
  constexpr absl::string_view kDebugData = "immediate_goaway_request2";
  constexpr http2::Http2ErrorCode kErrorCode =
      http2::Http2ErrorCode::kProtocolError;
  const bool kImmediate = true;

  EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(), http2::GoawayState::kIdle);

  mock_goaway_interface->ExpectTriggerWriteCycle();

  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus())).Times(2);

  SpawnGoawayRequest(goaway_manager, kImmediate, kErrorCode, kDebugData,
                     kLastGoodStreamId,
                     [&](absl::Status status) { on_done.Call(status); });
  SpawnGoawayRequest(goaway_manager, kImmediate, kErrorCode2, kDebugData2,
                     kLastGoodStreamId,
                     [&](absl::Status status) { on_done.Call(status); });

  std::optional<Http2Frame> goaway_frame =
      goaway_manager.TestOnlyMaybeGetGoawayFrame();
  // Validate the goaway frame.
  EXPECT_TRUE(goaway_frame.has_value());
  Http2Frame expected_goaway_frame =
      Http2GoawayFrame{/*last_stream_id=*/kLastGoodStreamId,
                       /*error_code=*/static_cast<uint32_t>(kErrorCode),
                       /*debug_data=*/Slice::FromCopiedString(kDebugData)};
  EXPECT_EQ(goaway_frame, expected_goaway_frame);
  goaway_manager.NotifyGoawaySent();

  EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(), http2::GoawayState::kDone);

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(GoawayTest, GracefulGoawayWorks) {
  constexpr uint32_t kLastGoodStreamId = 123;
  constexpr absl::string_view kDebugData = "graceful_goaway_request";
  constexpr http2::Http2ErrorCode kErrorCode = http2::Http2ErrorCode::kNoError;
  const bool kImmediate = false;
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

  GoawayManager goaway_manager(std::move(goaway_interface));
  EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(), http2::GoawayState::kIdle);

  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));

  SpawnGoawayRequest(goaway_manager, kImmediate, kErrorCode, kDebugData,
                     kLastGoodStreamId,
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
            /*debug_data=*/Slice::FromCopiedString(kDebugData)};
        EXPECT_EQ(goaway_frame, expected_goaway_frame);
        goaway_manager.NotifyGoawaySent();
        EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                  http2::GoawayState::kGracefulGoawayScheduled);
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
                   /*error_code=*/static_cast<uint32_t>(kErrorCode),
                   /*debug_data=*/Slice::FromCopiedString(kDebugData)};
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

// Graceful goaway state is kGracefulGoawayScheduled.
YODEL_TEST(GoawayTest, ImmediateGoawayTakesPrecedenceOverGracefulGoaway1) {
  constexpr uint32_t kLastGoodStreamId = 123;
  constexpr absl::string_view kDebugData1 = "Immediate_goaway_request";
  constexpr http2::Http2ErrorCode kErrorCode1 =
      http2::Http2ErrorCode::kProtocolError;
  constexpr absl::string_view kDebugData2 = "graceful_goaway_request";
  constexpr http2::Http2ErrorCode kErrorCode2 = http2::Http2ErrorCode::kNoError;
  const bool kImmediate = false;
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

  SpawnGoawayRequest(goaway_manager, kImmediate, kErrorCode2, kDebugData2,
                     kLastGoodStreamId,
                     [&](absl::Status status) { on_done.Call(status); });
  GetParty()->Spawn(
      "goaway-request2",
      [&]() {
        return TrySeq(
            goaway1_sent.Wait(),
            [&](auto) {
              goaway2_ready_to_send.Set();
              return goaway_manager.RequestGoaway(kErrorCode1, kDebugData1,
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
            static_cast<uint32_t>(kErrorCode2),
            /*debug_data=*/Slice::FromCopiedString(kDebugData2)};
        EXPECT_EQ(goaway_frame, expected_goaway_frame);
        goaway_manager.NotifyGoawaySent();
        EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                  http2::GoawayState::kGracefulGoawayScheduled);
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
                   /*error_code=*/static_cast<uint32_t>(kErrorCode1),
                   /*debug_data=*/Slice::FromCopiedString(kDebugData1)};
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

// Graceful goaway state is kGracefulGoawaySent.
YODEL_TEST(GoawayTest, ImmediateGoawayTakesPrecedenceOverGracefulGoaway2) {
  constexpr uint32_t kLastGoodStreamId = 123;
  constexpr absl::string_view kDebugData1 = "Immediate_goaway_request";
  constexpr http2::Http2ErrorCode kErrorCode1 =
      http2::Http2ErrorCode::kProtocolError;
  constexpr absl::string_view kDebugData2 = "graceful_goaway_request";
  constexpr http2::Http2ErrorCode kErrorCode2 = http2::Http2ErrorCode::kNoError;
  const bool kImmediate = false;
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

  SpawnGoawayRequest(goaway_manager, kImmediate, kErrorCode2, kDebugData2,
                     kLastGoodStreamId,
                     [&](absl::Status status) { on_done.Call(status); });
  GetParty()->Spawn(
      "goaway-request2",
      [&]() {
        return TrySeq(
            goaway2_ready_to_send.Wait(),
            [&](auto) {
              goaway3.Set();
              return goaway_manager.RequestGoaway(kErrorCode1, kDebugData1,
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
            static_cast<uint32_t>(kErrorCode2),
            /*debug_data=*/Slice::FromCopiedString(kDebugData2)};
        EXPECT_EQ(goaway_frame, expected_goaway_frame);
        goaway_manager.NotifyGoawaySent();
        EXPECT_EQ(goaway_manager.TestOnlyGetGoawayState(),
                  http2::GoawayState::kGracefulGoawayScheduled);
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
                   /*error_code=*/static_cast<uint32_t>(kErrorCode1),
                   /*debug_data=*/Slice::FromCopiedString(kDebugData1)};
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

}  // namespace grpc_core
