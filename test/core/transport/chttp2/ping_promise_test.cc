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
#include "src/core/config/core_configuration.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/notification.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "test/core/call/yodel/yodel_test.h"
#include "testing/base/public/gmock.h"
#include "testing/base/public/gunit.h"

namespace grpc_core {

namespace {
using ::grpc_core::http2::PingSystem;
using ::grpc_core::http2::PingSystemInterface;
using ::testing::MockFunction;
using ::testing::StrictMock;
using ::testing::WithArgs;
}  // namespace

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

 private:
  RefCountedPtr<Party> party_;
};
#define PING_SYSTEM_TEST(name) YODEL_TEST(PingSystemTest, name)

PING_SYSTEM_TEST(NoOp) {}

class MockPingSystemInterface : public PingSystemInterface {
 public:
  MOCK_METHOD(Promise<absl::Status>, SendPing, (SendPingArgs args), (override));
  MOCK_METHOD(Promise<absl::Status>, TriggerWrite, (), (override));
  MOCK_METHOD(Promise<absl::Status>, PingTimeout, (), (override));

  void ExpectSendPing(SendPingArgs args) {
    EXPECT_CALL(*this, SendPing).WillOnce(WithArgs<0>([](SendPingArgs args) {
      EXPECT_EQ(args.ack, args.ack);
      // EXPECT_EQ(args.ping_id, args.ping_id);
      return Immediate(absl::OkStatus());
    }));
  }
  void ExpectPingTimeout() {
    EXPECT_CALL(*this, PingTimeout).WillOnce(([]() {
      return Immediate(absl::OkStatus());
    }));
  }
  void ExpectTriggerWrite() {
    EXPECT_CALL(*this, TriggerWrite).WillOnce(([]() {
      return Immediate(absl::OkStatus());
    }));
  }
  auto ExpectSendPingReturn(SendPingArgs args) {
    struct SendPingReturn {
      SendPingArgs args;
      Notification ready;
    };
    std::shared_ptr<SendPingReturn> send_ping_return =
        std::make_shared<SendPingReturn>();

    EXPECT_CALL(*this, SendPing)
        .WillOnce(WithArgs<0>([send_ping_return](SendPingArgs args) {
          EXPECT_EQ(args.ack, args.ack);
          // EXPECT_EQ(args.ping_id, args.ping_id);
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

PING_SYSTEM_TEST(TestPingRequest) {
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  PingSystem ping_system(GetChannelArgs(), /*is_client=*/true,
                         std::move(ping_interface));
  std::string execution_order;
  StrictMock<MockFunction<void(absl::Status)>> on_done;

  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  auto party = GetParty();
  EXPECT_EQ(ping_system.CountPingInflight(), 0);
  EXPECT_FALSE(ping_system.PingRequested());

  party->Spawn("PingRequest",
               TrySeq(ping_system.RequestPing([&execution_order]() {
                 LOG(INFO) << "Ping initiated";
                 execution_order.append("2");
               }),
                      [&on_done]() {
                        on_done.Call(absl::OkStatus());
                        return absl::OkStatus();
                      }),
               [](auto) { LOG(INFO) << "Reached PingRequest end"; });

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

  EXPECT_EQ(execution_order, "1234");

  WaitForAllPendingWork();
}

PING_SYSTEM_TEST(TestPingUnrelatedAck) {
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  PingSystem ping_system(GetChannelArgs(), /*is_client=*/true,
                         std::move(ping_interface));
  std::string execution_order;
  StrictMock<MockFunction<void(absl::Status)>> on_done;

  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  auto party = GetParty();
  EXPECT_EQ(ping_system.CountPingInflight(), 0);
  EXPECT_FALSE(ping_system.PingRequested());

  party->Spawn("PingRequest",
               TrySeq(ping_system.RequestPing([&execution_order]() {
                 LOG(INFO) << "Ping initiated";
                 execution_order.append("2");
               }),
                      [&on_done]() {
                        on_done.Call(absl::OkStatus());
                        return absl::OkStatus();
                      }),
               [](auto) { LOG(INFO) << "Reached PingRequest end"; });

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

  EXPECT_EQ(execution_order, "12345");

  WaitForAllPendingWork();
}

PING_SYSTEM_TEST(TestPingWaitForAck) {
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  PingSystem ping_system(GetChannelArgs(), /*is_client=*/true,
                         std::move(ping_interface));
  std::string execution_order;
  StrictMock<MockFunction<void(absl::Status)>> on_done;

  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  auto party = GetParty();
  EXPECT_EQ(ping_system.CountPingInflight(), 0);
  EXPECT_FALSE(ping_system.PingRequested());

  party->Spawn("WaitForPingAck",
               TrySeq(ping_system.WaitForPingAck(),
                      [&on_done, &execution_order]() {
                        execution_order.append("2");
                        on_done.Call(absl::OkStatus());
                        return absl::OkStatus();
                      }),
               [](auto) { LOG(INFO) << "Reached WaitForPingAck end"; });

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

  EXPECT_EQ(execution_order, "1342");

  WaitForAllPendingWork();
}

PING_SYSTEM_TEST(TestPingCancel) {
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  PingSystem ping_system(GetChannelArgs(), /*is_client=*/true,
                         std::move(ping_interface));

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

  ping_system.Cancel(event_engine().get());
  EXPECT_FALSE(ping_system.PingRequested());
  WaitForAllPendingWork();
}

PING_SYSTEM_TEST(TestPingSystem) {
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  ping_interface->ExpectSendPing(SendPingArgs{false, 1234});
  ping_interface->ExpectPingTimeout();

  PingSystem ping_system(GetChannelArgs(), /*is_client=*/true,
                         std::move(ping_interface));
  auto party = GetParty();
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
                                       Duration::Seconds(100),
                                       /*ping_timeout=*/Duration::Seconds(10),
                                       party),
             []() { return absl::OkStatus(); }),
      [](auto) { LOG(INFO) << "Reached PingSystem end"; });

  WaitForAllPendingWork();
}

PING_SYSTEM_TEST(TestPingSystemDelayedPing) {
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  ping_interface->ExpectSendPing(SendPingArgs{false, 1234});
  ping_interface->ExpectPingTimeout();

  // Delayed ping
  ping_interface->ExpectTriggerWrite();

  PingSystem ping_system(GetChannelArgs(), /*is_client=*/true,
                         std::move(ping_interface));
  Duration next_allowed_ping_interval = Duration::Seconds(10);
  auto party = GetParty();
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
                                       next_allowed_ping_interval,
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
                                       next_allowed_ping_interval,
                                       /*ping_timeout=*/Duration::Seconds(100),
                                       party),
             []() { return absl::OkStatus(); }),
      [](auto) { LOG(INFO) << "Reached PingSystem end"; });

  WaitForAllPendingWork();
}

PING_SYSTEM_TEST(TestPingSystemAck) {
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  auto cb = ping_interface->ExpectSendPingReturn(SendPingArgs{false, 1234});

  PingSystem ping_system(GetChannelArgs(), /*is_client=*/true,
                         std::move(ping_interface));
  auto party = GetParty();
  party->Spawn(
      "PingRequest",
      TrySeq(ping_system.RequestPing([]() { LOG(INFO) << "Ping initiated"; }),
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

  party->Spawn("PingAckReceived",
               TrySeq(Sleep(Duration::Seconds(1)),
                      Map(
                          [&ping_system, this, &cb] {
                            auto args = cb();
                            return ping_system.AckPing(args.ping_id,
                                                       event_engine().get());
                          },
                          [](bool) { return absl::OkStatus(); })),
               [](auto) { LOG(INFO) << "Reached PingAckReceived end"; });

  WaitForAllPendingWork();
}

PING_SYSTEM_TEST(TestPingSystemTimeout) {
  InitParty();
  std::unique_ptr<StrictMock<MockPingSystemInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingSystemInterface>>();

  auto cb = ping_interface->ExpectSendPingReturn(SendPingArgs{false, 1234});
  ping_interface->ExpectPingTimeout();

  PingSystem ping_system(GetChannelArgs(), /*is_client=*/true,
                         std::move(ping_interface));
  auto party = GetParty();
  party->Spawn(
      "PingRequest",
      TrySeq(ping_system.RequestPing([]() { LOG(INFO) << "Ping initiated"; }),
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
               TrySeq(Sleep(Duration::Seconds(3)),
                      Map(
                          [&ping_system, this, &cb] {
                            auto args = cb();
                            return ping_system.AckPing(args.ping_id,
                                                       event_engine().get());
                          },
                          [](bool) { return absl::OkStatus(); })),
               [](auto) { LOG(INFO) << "Reached PingAckReceived end"; });

  WaitForAllPendingWork();
}

}  // namespace grpc_core
