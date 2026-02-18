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

#include <cstdint>
#include <optional>
#include <utility>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "test/core/call/yodel/yodel_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

namespace grpc_core {

namespace {
using ::grpc_core::http2::PingInterface;
using ::grpc_core::http2::PingManager;
using ::testing::MockFunction;
using ::testing::StrictMock;
constexpr Duration kInterval = Duration::Seconds(10);
constexpr Duration kLongInterval = Duration::Hours(1);
constexpr Duration kShortInterval = Duration::Seconds(1);

class MockPingInterface : public PingInterface {
 public:
  MOCK_METHOD(absl::Status, TriggerWrite, (), (override));
  MOCK_METHOD(Promise<absl::Status>, PingTimeout, (), (override));

  void ExpectPingTimeout() {
    EXPECT_CALL(*this, PingTimeout).WillOnce(([]() {
      LOG(INFO) << "MockPingInterface PingTimeout Polled";
      return Immediate(absl::OkStatus());
    }));
  }
  void ExpectTriggerWrite() {
    EXPECT_CALL(*this, TriggerWrite).WillOnce(([]() {
      return absl::OkStatus();
    }));
  }
};

class PingManagerTest : public YodelTest {
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

  void SpawnPingRequest(PingManager& ping_system, bool important,
                        absl::AnyInvocable<void()> on_initiate_cb,
                        absl::AnyInvocable<absl::Status()> on_done_cb) {
    GetParty()->Spawn(
        "PingRequest",
        TrySeq(ping_system.RequestPing(std::move(on_initiate_cb), important),
               [on_done_cb = std::move(on_done_cb)]() mutable {
                 return on_done_cb();
               }),
        [](auto) { LOG(INFO) << "Reached PingRequest end"; });
  }

 private:
  void InitCoreConfiguration() override {}
  void Shutdown() override { party_.reset(); }

  RefCountedPtr<Party> party_;
};

void MaybeSpawnDelayedPing(PingManager& ping_system,
                           std::optional<Duration> wait) {
  if (wait.has_value()) {
    GetContext<Party>()->Spawn(
        "DelayedPing",
        [&ping_system, wait = *wait]() {
          return ping_system.DelayedPingPromise(wait);
        },
        [](auto) {});
  }
}

void MaybeSpawnTimeout(PingManager& ping_system,
                       std::optional<uint64_t> opaque_data) {
  if (opaque_data.has_value()) {
    GetContext<Party>()->Spawn(
        "PingTimeout",
        [&ping_system, opaque_data = *opaque_data]() {
          return ping_system.TimeoutPromise(opaque_data);
        },
        [](auto) {});
  }
}

void MaybeSendPing(PingManager& ping_system,
                   Duration next_allowed_ping_interval) {
  SliceBuffer output_buf;
  std::optional<Duration> delayed_ping_wait =
      ping_system.MaybeGetSerializedPingFrames(output_buf,
                                               next_allowed_ping_interval);
  MaybeSpawnDelayedPing(ping_system, delayed_ping_wait);
  MaybeSpawnTimeout(ping_system, ping_system.NotifyPingSent());
}

std::optional<uint64_t> MaybeSendPingAndReturnID(
    PingManager& ping_system, Duration next_allowed_ping_interval,
    bool trigger_ping_timeout = true) {
  SliceBuffer output_buf;
  std::optional<Duration> delayed_ping_wait =
      ping_system.MaybeGetSerializedPingFrames(output_buf,
                                               next_allowed_ping_interval);
  MaybeSpawnDelayedPing(ping_system, delayed_ping_wait);
  std::optional<uint64_t> opaque_data = ping_system.NotifyPingSent();
  if (trigger_ping_timeout) {
    MaybeSpawnTimeout(ping_system, opaque_data);
  }
  return opaque_data;
}
}  // namespace

#define PING_MANAGER_TEST(name) YODEL_TEST(PingManagerTest, name)

PING_MANAGER_TEST(NoOp) {}

// Promise based ping callbacks tests
PING_MANAGER_TEST(TestPingRequest) {
  // Test to spawn a promise waiting for a ping ack and trigger a ping ack.
  // This test asserts the following:
  // 1. Ping request promise is resolved on getting a ping ack with the same
  //    opqaue id.
  // 2. The ping callbacks are executed in the correct order.
  InitParty();
  std::unique_ptr<StrictMock<MockPingInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingInterface>>();

  PingManager ping_system(GetChannelArgs(), kInterval,
                          std::move(ping_interface), event_engine());
  std::string execution_order;
  StrictMock<MockFunction<void(absl::Status)>> on_done;

  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  auto party = GetParty();
  EXPECT_EQ(ping_system.CountPingInflight(), 0);
  EXPECT_FALSE(ping_system.PingRequested());

  party->Spawn("PingRequest",
               ping_system.RequestPing(
                   [&execution_order]() {
                     LOG(INFO) << "Ping requested. Waiting for ack.";
                     execution_order.append("2");
                   },
                   /*important*/ false),
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

  party->Spawn("PingAckReceived",
               Map([&ping_system, id] { return ping_system.AckPing(id); },
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

PING_MANAGER_TEST(TestPingUnrelatedAck) {
  // Test to spawn a promise waiting for a ping ack and trigger two ping acks,
  // one with an unrelated id and one with the correct id. This test asserts the
  // following:
  // 1. Ping request promise is resolved by the ack with the same opaque id.
  // 2. The ping callbacks are executed in the correct order.
  InitParty();
  std::unique_ptr<StrictMock<MockPingInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingInterface>>();

  PingManager ping_system(GetChannelArgs(), kInterval,
                          std::move(ping_interface), event_engine());
  std::string execution_order;
  StrictMock<MockFunction<void(absl::Status)>> on_done;

  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  auto party = GetParty();
  EXPECT_EQ(ping_system.CountPingInflight(), 0);
  EXPECT_FALSE(ping_system.PingRequested());

  party->Spawn("PingRequest",
               ping_system.RequestPing(
                   [&execution_order]() {
                     LOG(INFO) << "Ping requested. Waiting for ack.";
                     execution_order.append("2");
                   },
                   /*important*/ false),
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
      TrySeq(Map([&ping_system, id] { return ping_system.AckPing(id + 1); },
                 [&execution_order, &ping_system](bool) {
                   execution_order.append("4");
                   EXPECT_EQ(ping_system.CountPingInflight(), 1);
                   return absl::OkStatus();
                 }),
             Map([&ping_system, id] { return ping_system.AckPing(id); },
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

PING_MANAGER_TEST(TestPingWaitForAck) {
  // Test to spawn a promise waiting for a ping ack and trigger a ping ack.
  // This test asserts the following:
  // 1. Ping request promise is resolved on getting a ping ack with the same
  //    opqaue id.
  // 2. The ping callbacks are executed in the correct order.
  InitParty();
  std::unique_ptr<StrictMock<MockPingInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingInterface>>();

  PingManager ping_system(GetChannelArgs(), kInterval,
                          std::move(ping_interface), event_engine());
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
  party->Spawn("PingAckReceived",
               Map([&ping_system, id] { return ping_system.AckPing(id); },
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

PING_MANAGER_TEST(TestPingCancel) {
  // Test to spawn a promise waiting for a ping ack and cancel it. This test
  // asserts the following:
  // 1. There are no outstanding requests for ping.
  InitParty();
  std::unique_ptr<StrictMock<MockPingInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingInterface>>();

  PingManager ping_system(GetChannelArgs(), kInterval,
                          std::move(ping_interface), event_engine());

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

PING_MANAGER_TEST(TestPingManagerNoAck) {
  // Test to trigger a ping request for which no ack is received.
  // This test asserts the following:
  // 1. The ping timeout is triggered after the ping timeout duration.
  InitParty();
  std::unique_ptr<StrictMock<MockPingInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingInterface>>();

  ping_interface->ExpectPingTimeout();

  PingManager ping_system(GetChannelArgs(), kInterval,
                          std::move(ping_interface), event_engine());
  auto party = GetParty();
  party->Spawn(
      "PingRequest",
      TrySeq(ping_system.RequestPing(
                 []() { LOG(INFO) << "Ping requested. Waiting for ack."; },
                 /*important*/ false),
             []() {
               Crash("Unreachable");
               return absl::OkStatus();
             }),
      [](auto) { LOG(INFO) << "Received a Ping Ack"; });

  party->Spawn(
      "PingManager",
      [&ping_system] {
        MaybeSendPing(ping_system, /*next_allowed_ping_interval=*/
                      Duration::Seconds(100));
      },
      [](auto) { LOG(INFO) << "Reached PingManager end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

PING_MANAGER_TEST(DISABLED_TestPingManagerDelayedPing) {
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
  std::unique_ptr<StrictMock<MockPingInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingInterface>>();

  ping_interface->ExpectPingTimeout();

  // Delayed ping
  ping_interface->ExpectTriggerWrite();

  auto channel_args =
      GetChannelArgs().Set(GRPC_ARG_HTTP2_MAX_INFLIGHT_PINGS, 2);
  PingManager ping_system(channel_args, /*ping_timeout=*/Duration::Seconds(100),
                          std::move(ping_interface), event_engine());
  auto party = GetParty();

  // Ping 1
  party->Spawn(
      "PingRequest",
      TrySeq(ping_system.RequestPing([]() { LOG(INFO) << "Ping initiated"; },
                                     /*important*/ false),
             []() {
               Crash("Unreachable");
               return absl::OkStatus();
             }),
      [](auto) { LOG(INFO) << "Reached PingRequest end"; });

  party->Spawn(
      "PingManager",
      [&ping_system] {
        MaybeSendPing(ping_system, /*next_allowed_ping_interval=*/
                      kLongInterval);
      },
      [](auto) { LOG(INFO) << "Reached PingManager end"; });

  // Ping 2
  party->Spawn(
      "PingRequest2",
      TrySeq(ping_system.RequestPing([]() { LOG(INFO) << "Ping initiated"; },
                                     /*important*/ false),
             []() {
               Crash("Unreachable");
               return absl::OkStatus();
             }),
      [](auto) { LOG(INFO) << "Reached PingRequest end"; });

  party->Spawn(
      "PingManager2",
      [&ping_system] {
        MaybeSendPing(ping_system, /*next_allowed_ping_interval=*/
                      kLongInterval);
      },
      [](auto) { LOG(INFO) << "Reached PingManager end"; });
  party->Spawn(
      "PingManager3",
      [&ping_system] {
        MaybeSendPing(ping_system, /*next_allowed_ping_interval=*/
                      kLongInterval);
      },
      [](auto) { LOG(INFO) << "Reached PingManager end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

PING_MANAGER_TEST(TestPingManagerAck) {
  // Simple test to trigger a ping request and process a ping ack.
  // The test asserts the following:
  // 1. The ping request promise is resolved on getting a ping ack with the same
  //    opqaue id.
  // 2. The ping timeout is set to 1 hour to ensure that ping ack is processed
  //    first and ping timeout is not triggered.
  InitParty();
  std::unique_ptr<StrictMock<MockPingInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingInterface>>();
  StrictMock<MockFunction<void()>> on_ping_ack_received;
  EXPECT_CALL(on_ping_ack_received, Call());

  PingManager ping_system(GetChannelArgs(), /*ping_timeout=*/kLongInterval,
                          std::move(ping_interface), event_engine());
  auto party = GetParty();
  uint64_t opaque_data;

  party->Spawn(
      "PingRequest",
      TrySeq(ping_system.RequestPing(
                 []() { LOG(INFO) << "Ping requested. Waiting for ack."; },
                 /*important*/ false),
             [&on_ping_ack_received]() {
               on_ping_ack_received.Call();
               return absl::OkStatus();
             }),
      [](auto) { LOG(INFO) << "Reached PingRequest end"; });

  party->Spawn(
      "PingManager",
      [&ping_system, &opaque_data] {
        std::optional<uint64_t> recv_opaque_data = MaybeSendPingAndReturnID(
            ping_system, /*next_allowed_ping_interval=*/
            Duration::Seconds(100));
        EXPECT_TRUE(recv_opaque_data.has_value());
        opaque_data = recv_opaque_data.value();
      },
      [](auto) { LOG(INFO) << "Reached PingManager end"; });

  party->Spawn(
      "PingAckReceived",
      TrySeq(Sleep(kShortInterval),
             Map([&ping_system,
                  &opaque_data] { return ping_system.AckPing(opaque_data); },
                 [](bool result) {
                   EXPECT_TRUE(result);
                   return absl::OkStatus();
                 })),
      [](auto) { LOG(INFO) << "Reached PingAckReceived end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

PING_MANAGER_TEST(TestPingManagerDelayedAck) {
  // Test to trigger a ping request and process a ping ack after ping timeout.
  // The test asserts the following:
  // 1. The ping timeout is triggered after the ping timeout duration. To ensure
  //    that the ping timeout is triggered reliably, the ping ack promise is set
  //    to wait for 1 hour.
  // 2. Note: The ping request promise will be resolved after the ping ack is
  //    received.
  InitParty();
  std::unique_ptr<StrictMock<MockPingInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingInterface>>();
  ping_interface->ExpectPingTimeout();

  PingManager ping_system(GetChannelArgs(),
                          /*ping_timeout=*/kShortInterval,
                          std::move(ping_interface), event_engine());
  auto party = GetParty();
  uint64_t opaque_data;

  party->Spawn(
      "PingRequest",
      TrySeq(ping_system.RequestPing(
                 []() { LOG(INFO) << "Ping requested. Waiting for ack."; },
                 /*important*/ false),
             []() { return absl::OkStatus(); }),
      [](auto) { LOG(INFO) << "Reached PingRequest end"; });

  party->Spawn(
      "PingManager",
      [&ping_system, &opaque_data] {
        std::optional<uint64_t> recv_opaque_data = MaybeSendPingAndReturnID(
            ping_system, /*next_allowed_ping_interval=*/
            Duration::Seconds(100));
        EXPECT_TRUE(recv_opaque_data.has_value());
        opaque_data = recv_opaque_data.value();
      },
      [](auto) { LOG(INFO) << "Reached PingManager end"; });

  party->Spawn(
      "DelayedPingAckReceived",
      TrySeq(Sleep(kLongInterval),
             Map([&ping_system,
                  &opaque_data] { return ping_system.AckPing(opaque_data); },
                 [](bool) { return absl::OkStatus(); })),
      [](auto) { LOG(INFO) << "Reached PingAckReceived end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

PING_MANAGER_TEST(TestPingManagerNoPingRequest) {
  // Tests that MaybeSendPing returns immediately if no ping request has been
  // made.
  InitParty();
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  std::unique_ptr<StrictMock<MockPingInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingInterface>>();

  PingManager ping_system(GetChannelArgs(),
                          /*ping_timeout=*/kShortInterval,
                          std::move(ping_interface), event_engine());
  auto party = GetParty();
  EXPECT_CALL(on_done, Call(absl::OkStatus()));

  party->Spawn(
      "PingManager",
      [&ping_system] {
        MaybeSendPing(ping_system, /*next_allowed_ping_interval=*/
                      Duration::Seconds(100));
      },
      [&on_done](auto) {
        on_done.Call(absl::OkStatus());
        LOG(INFO) << "Reached PingManager end";
      });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

PING_MANAGER_TEST(TestPingManagerImportantPing) {
  // Tests important flag for ping requests. Asserts the following:
  // 1. The important flag is set correctly for the multiple ping requests.
  // 2. Once a ping request is sent out, the important flag is reset.

  InitParty();
  StrictMock<MockFunction<void(absl::Status)>> on_done;

  std::unique_ptr<StrictMock<MockPingInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingInterface>>();
  ping_interface->ExpectPingTimeout();

  PingManager ping_system(
      GetChannelArgs().Set(GRPC_ARG_HTTP2_MAX_INFLIGHT_PINGS, 1),
      /*ping_timeout=*/Duration::Seconds(100), std::move(ping_interface),
      event_engine());

  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_FALSE(ping_system.PingRequested());
  EXPECT_FALSE(ping_system.ImportantPingRequested());

  SpawnPingRequest(
      ping_system, /*important=*/false,
      [] { LOG(INFO) << "Ping1 requested. Waiting for ack."; },
      [] {
        Crash("Unreachable. No Ping ack expected.");
        return absl::OkStatus();
      });

  EXPECT_TRUE(ping_system.PingRequested());
  EXPECT_FALSE(ping_system.ImportantPingRequested());

  SpawnPingRequest(
      ping_system, /*important=*/true,
      [] { LOG(INFO) << "Ping2 requested. Waiting for ack."; },
      [] {
        Crash("Unreachable. No Ping ack expected.");
        return absl::OkStatus();
      });

  EXPECT_TRUE(ping_system.PingRequested());
  EXPECT_TRUE(ping_system.ImportantPingRequested());

  SpawnPingRequest(
      ping_system, /*important=*/false,
      [] { LOG(INFO) << "Ping3 requested. Waiting for ack."; },
      [] {
        Crash("Unreachable. No Ping ack expected.");
        return absl::OkStatus();
      });

  EXPECT_TRUE(ping_system.PingRequested());
  EXPECT_TRUE(ping_system.ImportantPingRequested());

  GetParty()->Spawn(
      "PingManager",
      [&ping_system] {
        MaybeSendPing(ping_system, /*next_allowed_ping_interval=*/
                      kShortInterval);
      },
      [&on_done, &ping_system](auto) {
        on_done.Call(absl::OkStatus());
        EXPECT_FALSE(ping_system.PingRequested());
        EXPECT_FALSE(ping_system.ImportantPingRequested());
        LOG(INFO) << "Reached PingManager end";
      });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

PING_MANAGER_TEST(TestPingManagerPingTimeoutAfterAck) {
  // Tests the scenario where the ping timeout is triggered after the ping ack
  // is received. The test asserts the following:
  // 1. The ping request promise is resolved on getting a ping ack with the same
  //    opqaue id.
  InitParty();
  StrictMock<MockFunction<void()>> on_ping_ack_received;
  EXPECT_CALL(on_ping_ack_received, Call());
  std::unique_ptr<StrictMock<MockPingInterface>> ping_interface =
      std::make_unique<StrictMock<MockPingInterface>>();

  PingManager ping_system(
      GetChannelArgs().Set(GRPC_ARG_HTTP2_MAX_INFLIGHT_PINGS, 1),
      /*ping_timeout=*/Duration::Seconds(100), std::move(ping_interface),
      event_engine());
  SliceBuffer output_buf;

  SpawnPingRequest(
      ping_system, /*important=*/false,
      [] { LOG(INFO) << "Ping1 requested. Waiting for ack."; },
      [&on_ping_ack_received] {
        on_ping_ack_received.Call();
        return absl::OkStatus();
      });

  GetParty()->Spawn(
      "PingManager",
      [&ping_system] {
        return TrySeq([&ping_system]() {
          auto recv_opaque_data = MaybeSendPingAndReturnID(
              ping_system, /*next_allowed_ping_interval=*/kShortInterval,
              /*trigger_ping_timeout=*/false);
          EXPECT_TRUE(recv_opaque_data.has_value());
          uint64_t opaque_data = recv_opaque_data.value();
          EXPECT_TRUE(ping_system.AckPing(opaque_data));
          MaybeSpawnTimeout(ping_system, recv_opaque_data);
          return absl::OkStatus();
        });
      },
      [](auto) { LOG(INFO) << "Reached PingManager end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

}  // namespace grpc_core
