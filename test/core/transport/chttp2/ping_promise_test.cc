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
#include "src/core/ext/transport/chttp2/transport/ping_promise.h"

#include "absl/log/log.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/notification.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

using ::grpc_core::http2::PingSystem;
using ::grpc_core::http2::PingSystemInterface;
using ::testing::MockFunction;
using ::testing::StrictMock;
using ::testing::WithArgs;

class MockPingSystemInterface : public PingSystemInterface {
 public:
  MOCK_METHOD(Promise<absl::Status>, SendPing, (SendPingArgs args), (override));
  MOCK_METHOD(Promise<absl::Status>, TriggerWrite, (), (override));
  MOCK_METHOD(Promise<absl::Status>, PingTimeout, (), (override));

  void ExpectSendPing(SendPingArgs expected_args) {
    EXPECT_CALL(*this, SendPing)
        .WillOnce(WithArgs<0>([expected_args](SendPingArgs args) {
          EXPECT_EQ(expected_args.ack, args.ack);
          LOG(INFO) << "MockPingSystemInterface SendPing Polled (ack: "
                    << args.ack << " opaque_data: " << args.opaque_data << ")."
                    << "Expected (ack: " << expected_args.ack << ")";
          return Immediate(absl::OkStatus());
        }));
  }
  void ExpectPingTimeout() {
    EXPECT_CALL(*this, PingTimeout).WillOnce(([]() {
      LOG(INFO) << "MockPingSystemInterface PingTimeout Polled";
      return Immediate(absl::OkStatus());
    }));
  }
  void ExpectTriggerWrite(Timestamp call_after) {
    EXPECT_CALL(*this, TriggerWrite).WillOnce(([call_after]() {
      Timestamp now = Timestamp::Now();
      LOG(INFO) << "MockPingSystemInterface TriggerWrite Polled(now: " << now
                << " call_after: " << call_after << ")";
      EXPECT_GE(now, call_after);
      return Immediate(absl::OkStatus());
    }));
  }
  auto ExpectSendPingReturnArgs(SendPingArgs expected_args) {
    struct SendPingReturn {
      SendPingArgs args;
      Notification ready;
    };
    std::shared_ptr<SendPingReturn> send_ping_return =
        std::make_shared<SendPingReturn>();

    EXPECT_CALL(*this, SendPing)
        .WillOnce(
            WithArgs<0>([send_ping_return, expected_args](SendPingArgs args) {
              LOG(INFO) << "MockPingSystemInterface SendPing Polled (ack: "
                        << args.ack << " opaque_data: " << args.opaque_data
                        << ")."
                        << "Expected (ack: " << expected_args.ack << ")";
              EXPECT_EQ(expected_args.ack, args.ack);
              send_ping_return->args = args;
              send_ping_return->ready.Notify();
              return Immediate(absl::OkStatus());
            }));
    return [send_ping_return] {
      send_ping_return->ready.WaitForNotification();
      return send_ping_return->args;
    };
  }
};

class PingSystemTest : public YodelTest {
 protected:
  using SendPingArgs = PingSystemInterface::SendPingArgs;
  using YodelTest::YodelTest;

  Party* GetParty() { return party_.get(); }
  void InitParty() {
    auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
    general_party_arena
        ->SetContext<grpc_event_engine::experimental::EventEngine>(
            event_engine().get());
    party_ = Party::Make(std::move(general_party_arena));
  }

  ChannelArgs GetChannelArgs() {
    return CoreConfiguration::Get()
        .channel_args_preconditioning()
        .PreconditionChannelArgs(nullptr);
  }

 private:
  void InitCoreConfiguration() override {}
  void Shutdown() override { party_.reset(); }

  RefCountedPtr<Party> party_;
};
#define PING_SYSTEM_TEST(name) YODEL_TEST(PingSystemTest, name)

PING_SYSTEM_TEST(NoOp) {}

// Promise based ping callbacks tests
PING_SYSTEM_TEST(TestPingRequest) {
  // Test to spawn a promise waiting for a ping ack and trigger a ping ack.
  // This test asserts the following:
  // 1. Ping request promise is resolved on getting a ping ack with the same
  //    opqaue id.
  // 2. The ping callbacks are executed in the correct order.
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  PingSystem ping_system(GetChannelArgs(), std::move(ping_interface),
                         event_engine());
  std::string execution_order;
  StrictMock<MockFunction<void(absl::Status)>> on_done;

  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  auto party = GetParty();
  EXPECT_EQ(ping_system.CountPingInflight(), 0);
  EXPECT_FALSE(ping_system.PingRequested());

  party->Spawn("PingRequest", ping_system.RequestPing([&execution_order]() {
    LOG(INFO) << "Ping requested. Waiting for ack.";
    execution_order.append("2");
  }),
               [&on_done](auto) {
                 LOG(INFO) << "Got a Ping Ack";
                 on_done.Call(absl::OkStatus());
               });

  EXPECT_TRUE(ping_system.PingRequested());
  execution_order.append("1");
  uint64_t id = ping_system.StartPing();
  EXPECT_EQ(ping_system.CountPingInflight(), 1);
  EXPECT_FALSE(ping_system.PingRequested());
  execution_order.append("3");

  party->Spawn(
      "PingAckReceived",
      Map([&ping_system, id,
           this] { return ping_system.AckPing(id, event_engine().get()); },
          [&execution_order, &ping_system](bool) {
            execution_order.append("4");
            EXPECT_EQ(ping_system.CountPingInflight(), 0);
            return absl::OkStatus();
          }),
      [](auto) { LOG(INFO) << "Reached PingAck end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();

  EXPECT_STREQ(execution_order.c_str(), "1234");
}

PING_SYSTEM_TEST(TestPingUnrelatedAck) {
  // Test to spawn a promise waiting for a ping ack and trigger two ping acks,
  // one with an unrelated id and one with the correct id. This test asserts the
  // following:
  // 1. Ping request promise is resolved by the ack with the same opaque id.
  // 2. The ping callbacks are executed in the correct order.
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  PingSystem ping_system(GetChannelArgs(), std::move(ping_interface),
                         event_engine());
  std::string execution_order;
  StrictMock<MockFunction<void(absl::Status)>> on_done;

  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  auto party = GetParty();
  EXPECT_EQ(ping_system.CountPingInflight(), 0);
  EXPECT_FALSE(ping_system.PingRequested());

  party->Spawn("PingRequest", ping_system.RequestPing([&execution_order]() {
    LOG(INFO) << "Ping requested. Waiting for ack.";
    execution_order.append("2");
  }),
               [&on_done](auto) {
                 LOG(INFO) << "Got a Ping Ack";
                 on_done.Call(absl::OkStatus());
               });

  EXPECT_TRUE(ping_system.PingRequested());
  execution_order.append("1");
  uint64_t id = ping_system.StartPing();
  EXPECT_EQ(ping_system.CountPingInflight(), 1);
  EXPECT_FALSE(ping_system.PingRequested());
  execution_order.append("3");
  party->Spawn(
      "PingAckReceived",
      TrySeq(
          Map(
              [&ping_system, id, this] {
                return ping_system.AckPing(id + 1, event_engine().get());
              },
              [&execution_order, &ping_system](bool) {
                execution_order.append("4");
                EXPECT_EQ(ping_system.CountPingInflight(), 1);
                return absl::OkStatus();
              }),
          Map([&ping_system, id,
               this] { return ping_system.AckPing(id, event_engine().get()); },
              [&execution_order, &ping_system](bool) {
                execution_order.append("5");
                EXPECT_EQ(ping_system.CountPingInflight(), 0);
                return absl::OkStatus();
              })),
      [](auto) { LOG(INFO) << "Reached PingAck end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();

  EXPECT_STREQ(execution_order.c_str(), "12345");
}

PING_SYSTEM_TEST(TestPingWaitForAck) {
  // Test to spawn a promise waiting for a ping ack and trigger a ping ack.
  // This test asserts the following:
  // 1. Ping request promise is resolved on getting a ping ack with the same
  //    opqaue id.
  // 2. The ping callbacks are executed in the correct order.
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  PingSystem ping_system(GetChannelArgs(), std::move(ping_interface),
                         event_engine());
  std::string execution_order;
  StrictMock<MockFunction<void(absl::Status)>> on_done;

  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  auto party = GetParty();
  EXPECT_EQ(ping_system.CountPingInflight(), 0);
  EXPECT_FALSE(ping_system.PingRequested());

  party->Spawn("WaitForPingAck", ping_system.WaitForPingAck(),
               [&on_done, &execution_order](auto) {
                 LOG(INFO) << "Reached WaitForPingAck end";
                 execution_order.append("2");
                 on_done.Call(absl::OkStatus());
               });

  execution_order.append("1");
  EXPECT_TRUE(ping_system.PingRequested());
  uint64_t id = ping_system.StartPing();
  EXPECT_EQ(ping_system.CountPingInflight(), 1);
  EXPECT_FALSE(ping_system.PingRequested());
  execution_order.append("3");
  party->Spawn(
      "PingAckReceived",
      Map([&ping_system, id,
           this] { return ping_system.AckPing(id, event_engine().get()); },
          [&execution_order, &ping_system](bool) {
            EXPECT_EQ(ping_system.CountPingInflight(), 0);
            execution_order.append("4");
            return absl::OkStatus();
          }),
      [](auto) { LOG(INFO) << "Reached PingAckReceived end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();

  EXPECT_STREQ(execution_order.c_str(), "1342");
}

PING_SYSTEM_TEST(TestPingCancel) {
  // Test to spawn a promise waiting for a ping ack and cancel it. This test
  // asserts the following:
  // 1. There are no outstanding requests for ping.
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  PingSystem ping_system(GetChannelArgs(), std::move(ping_interface),
                         event_engine());

  auto party = GetParty();
  EXPECT_EQ(ping_system.CountPingInflight(), 0);
  EXPECT_FALSE(ping_system.PingRequested());

  party->Spawn(
      "WaitForPingAck",
      TrySeq(ping_system.WaitForPingAck(), []() { Crash("Unreachable"); }),
      [](auto) { LOG(INFO) << "Reached WaitForPingAck end"; });

  EXPECT_TRUE(ping_system.PingRequested());
  ping_system.StartPing();
  EXPECT_EQ(ping_system.CountPingInflight(), 1);
  EXPECT_FALSE(ping_system.PingRequested());

  ping_system.CancelCallbacks();
  EXPECT_FALSE(ping_system.PingRequested());

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

PING_SYSTEM_TEST(TestPingSystemNoAck) {
  // Test to trigger a ping request for which no ack is received.
  // This test asserts the following:
  // 1. The ping timeout is triggered after the ping timeout duration.
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  ping_interface->ExpectSendPing(SendPingArgs{false, /*not used*/ 1234});
  ping_interface->ExpectPingTimeout();

  PingSystem ping_system(GetChannelArgs(), std::move(ping_interface),
                         event_engine());
  auto party = GetParty();
  party->Spawn("PingRequest",
               TrySeq(ping_system.RequestPing([]() {
                 LOG(INFO) << "Ping requested. Waiting for ack.";
               }),
                      []() {
                        Crash("Unreachable");
                        return absl::OkStatus();
                      }),
               [](auto) { LOG(INFO) << "Received a Ping Ack"; });

  party->Spawn(
      "PingSystem",
      TrySeq(ping_system.MaybeSendPing(/*next_allowed_ping_interval=*/
                                       Duration::Seconds(100),
                                       /*ping_timeout=*/Duration::Seconds(10),
                                       party),
             []() { return absl::OkStatus(); }),
      [](auto) { LOG(INFO) << "Reached PingSystem end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

PING_SYSTEM_TEST(TestPingSystemDelayedPing) {
  // Test to trigger two ping requests.
  // The test asserts the following:
  // 1. The first ping is sent successfully.
  // 2. The second ping is delayed based on the ping rate policy and a write
  //    cycle is triggered after the delay. The next_allowed_ping_interval is
  //    set to 1 hour to ensure that the test reliably makes an attempt to send
  //    both pings within next_allowed_ping_interval.
  // 3. The ping timeout is triggered for the first ping.
  // 4. Write cycle is triggered only once even if there are multiple calls to
  //    MaybeSendPing within the next_allowed_ping_interval.
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  ping_interface->ExpectSendPing(SendPingArgs{false, 1234});
  ping_interface->ExpectPingTimeout();

  // Delayed ping
  ping_interface->ExpectTriggerWrite(/*call_after*/ Timestamp::Now() +
                                     Duration::Hours(1));

  PingSystem ping_system(GetChannelArgs(), std::move(ping_interface),
                         event_engine());
  auto party = GetParty();

  // Ping 1
  party->Spawn(
      "PingRequest",
      TrySeq(ping_system.RequestPing([]() { LOG(INFO) << "Ping initiated"; }),
             []() {
               Crash("Unreachable");
               return absl::OkStatus();
             }),
      [](auto) { LOG(INFO) << "Reached PingRequest end"; });

  party->Spawn(
      "PingSystem",
      TrySeq(ping_system.MaybeSendPing(/*next_allowed_ping_interval=*/
                                       Duration::Hours(1),
                                       /*ping_timeout=*/Duration::Seconds(100),
                                       party),
             []() { return absl::OkStatus(); }),
      [](auto) { LOG(INFO) << "Reached PingSystem end"; });

  // Ping 2
  party->Spawn(
      "PingRequest2",
      TrySeq(ping_system.RequestPing([]() { LOG(INFO) << "Ping initiated"; }),
             []() {
               Crash("Unreachable");
               return absl::OkStatus();
             }),
      [](auto) { LOG(INFO) << "Reached PingRequest end"; });

  party->Spawn(
      "PingSystem2",
      TrySeq(ping_system.MaybeSendPing(/*next_allowed_ping_interval=*/
                                       Duration::Hours(1),
                                       /*ping_timeout=*/Duration::Seconds(100),
                                       party),
             []() { return absl::OkStatus(); }),
      [](auto) { LOG(INFO) << "Reached PingSystem end"; });
  party->Spawn(
      "PingSystem3",
      TrySeq(ping_system.MaybeSendPing(/*next_allowed_ping_interval=*/
                                       Duration::Hours(1),
                                       /*ping_timeout=*/Duration::Seconds(100),
                                       party),
             []() { return absl::OkStatus(); }),
      [](auto) { LOG(INFO) << "Reached PingSystem end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

PING_SYSTEM_TEST(TestPingSystemAck) {
  // Simple test to trigger a ping request and process a ping ack.
  // The test asserts the following:
  // 1. The ping request promise is resolved on getting a ping ack with the same
  //    opqaue id.
  // 2. The ping timeout is set to 1 hour to ensure that ping ack is processed
  //    first and ping timeout is not triggered.
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  auto cb = ping_interface->ExpectSendPingReturnArgs(SendPingArgs{false, 1234});

  PingSystem ping_system(GetChannelArgs(), std::move(ping_interface),
                         event_engine());
  auto party = GetParty();

  party->Spawn("PingRequest",
               TrySeq(ping_system.RequestPing([]() {
                 LOG(INFO) << "Ping requested. Waiting for ack.";
               }),
                      []() { return absl::OkStatus(); }),
               [](auto) { LOG(INFO) << "Reached PingRequest end"; });

  party->Spawn("PingSystem",
               TrySeq(ping_system.MaybeSendPing(/*next_allowed_ping_interval=*/
                                                Duration::Seconds(100),
                                                /*ping_timeout=*/
                                                Duration::Hours(1), party),
                      []() { return absl::OkStatus(); }),
               [](auto) { LOG(INFO) << "Reached PingSystem end"; });

  party->Spawn("PingAckReceived",
               TrySeq(Sleep(Duration::Seconds(1)),
                      Map(
                          [&ping_system, this, &cb] {
                            auto args = cb();
                            return ping_system.AckPing(args.opaque_data,
                                                       event_engine().get());
                          },
                          [](bool) { return absl::OkStatus(); })),
               [](auto) { LOG(INFO) << "Reached PingAckReceived end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

PING_SYSTEM_TEST(TestPingSystemDelayedAck) {
  // Test to trigger a ping request and process a ping ack after ping timeout.
  // The test asserts the following:
  // 1. The ping timeout is triggered after the ping timeout duration. To ensure
  //    that the ping timeout is triggered reliably, the ping ack promise is set
  //    to wait for 1 hour.
  // 2. Note: The ping request promise will be resolved after the ping ack is
  //    received.
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  auto cb = ping_interface->ExpectSendPingReturnArgs(SendPingArgs{false, 1234});
  ping_interface->ExpectPingTimeout();

  PingSystem ping_system(GetChannelArgs(), std::move(ping_interface),
                         event_engine());
  auto party = GetParty();
  party->Spawn("PingRequest",
               TrySeq(ping_system.RequestPing([]() {
                 LOG(INFO) << "Ping requested. Waiting for ack.";
               }),
                      []() { return absl::OkStatus(); }),
               [](auto) { LOG(INFO) << "Reached PingRequest end"; });

  party->Spawn(
      "PingSystem",
      TrySeq(ping_system.MaybeSendPing(/*next_allowed_ping_interval=*/
                                       Duration::Seconds(100),
                                       /*ping_timeout=*/Duration::Seconds(2),
                                       party),
             []() { return absl::OkStatus(); }),
      [](auto) { LOG(INFO) << "Reached PingSystem end"; });

  party->Spawn("DelayedPingAckReceived",
               TrySeq(Sleep(Duration::Hours(1)),
                      Map(
                          [&ping_system, this, &cb] {
                            auto args = cb();
                            return ping_system.AckPing(args.opaque_data,
                                                       event_engine().get());
                          },
                          [](bool) { return absl::OkStatus(); })),
               [](auto) { LOG(INFO) << "Reached PingAckReceived end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

PING_SYSTEM_TEST(TestPingSystemNoPingRequest) {
  // Tests that MaybeSendPing returns immediately if no ping request has been
  // made.
  InitParty();
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  PingSystem ping_system(GetChannelArgs(), std::move(ping_interface),
                         event_engine());
  auto party = GetParty();
  EXPECT_CALL(on_done, Call(absl::OkStatus()));

  party->Spawn(
      "PingSystem",
      TrySeq(ping_system.MaybeSendPing(/*next_allowed_ping_interval=*/
                                       Duration::Seconds(100),
                                       /*ping_timeout=*/Duration::Seconds(2),
                                       party),
             [&on_done]() {
               on_done.Call(absl::OkStatus());
               return absl::OkStatus();
             }),
      [](auto) { LOG(INFO) << "Reached PingSystem end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

}  // namespace grpc_core
