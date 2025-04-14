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

#include <memory>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/util/notification.h"
#include "src/core/util/time.h"
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

namespace {
using ::grpc_core::http2::KeepAliveSystem;
using ::grpc_core::http2::KeepAliveSystemInterface;

using ::testing::MockFunction;
using ::testing::StrictMock;
using ::testing::WithArgs;
}  // namespace

class MockKeepAliveSystemInterface : public KeepAliveSystemInterface {
 public:
  MOCK_METHOD(Promise<absl::Status>, SendPing, (), (override));
  MOCK_METHOD(Promise<absl::Status>, KeepAliveTimeout, (), (override));
  void ExpectSendPing(int& end_after) {
    EXPECT_CALL(*this, SendPing())
        .Times(end_after)
        .WillRepeatedly([&end_after] {
          if (--end_after == 0) {
            return Immediate(absl::CancelledError());
          }
          return Immediate(absl::OkStatus());
        });
  }

  void ExpectSendPingWithSleep(Duration duration, int& end_after) {
    EXPECT_CALL(*this, SendPing())
        .Times(end_after)
        .WillRepeatedly([duration, &end_after] {
          return If((--end_after == 0),
                    TrySeq(Sleep(duration),
                           [] { return Immediate(absl::CancelledError()); }),
                    TrySeq(Sleep(duration),
                           [] { return Immediate(absl::OkStatus()); }));
        });
  }

  void ExpectKeepAliveTimeout() {
    EXPECT_CALL(*this, KeepAliveTimeout()).WillOnce([] {
      return (Immediate(absl::OkStatus()));
    });
  }
};

class KeepAliveSystemTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  Party* GetParty() { return party_.get(); }
  void InitParty() {
    auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
    general_party_arena
        ->SetContext<grpc_event_engine::experimental::EventEngine>(
            event_engine().get());
    party_ = Party::Make(std::move(general_party_arena));
  }

 private:
  void InitCoreConfiguration() override {}
  void Shutdown() override { party_.reset(); }

  RefCountedPtr<Party> party_;
};

YODEL_TEST(KeepAliveSystemTest, TestKeepAlive) {
  InitParty();
  int end_after = 2;
  std::unique_ptr<StrictMock<MockKeepAliveSystemInterface>>
      keep_alive_interface =
          std::make_unique<StrictMock<MockKeepAliveSystemInterface>>();
  keep_alive_interface->ExpectSendPing(end_after);

  KeepAliveSystem keep_alive_system(std::move(keep_alive_interface),
                                    /*keepalive_timeout*/ Duration::Infinity());
  auto party = GetParty();
  keep_alive_system.Spawn(party, Duration::Seconds(1));

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(KeepAliveSystemTest, TestKeepAliveTimeout) {
  InitParty();

  int end_after = 1;
  std::unique_ptr<StrictMock<MockKeepAliveSystemInterface>>
      keep_alive_interface =
          std::make_unique<StrictMock<MockKeepAliveSystemInterface>>();
  keep_alive_interface->ExpectKeepAliveTimeout();
  keep_alive_interface->ExpectSendPingWithSleep(Duration::Hours(1), end_after);

  KeepAliveSystem keep_alive_system(std::move(keep_alive_interface),
                                    /*keepalive_timeout*/ Duration::Seconds(1));
  auto party = GetParty();
  keep_alive_system.Spawn(party, Duration::Seconds(1));

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(KeepAliveSystemTest, TestGotData) {
  InitParty();
  std::string execution_order;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  StrictMock<MockFunction<void(absl::Status)>> on_done2;
  std::unique_ptr<StrictMock<MockKeepAliveSystemInterface>>
      keep_alive_interface =
          std::make_unique<StrictMock<MockKeepAliveSystemInterface>>();
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_done2, Call(absl::OkStatus()));

  KeepAliveSystem keep_alive_system(std::move(keep_alive_interface),
                                    /*keepalive_timeout*/ Duration::Hours(1));
  auto party = GetParty();
  Latch<void> latch;
  Latch<void> latch2;

  party->Spawn("WaitForData",
               TrySeq(keep_alive_system.TestOnlyWaitForData(),
                      [&on_done, &execution_order, &latch]() {
                        execution_order.append("2");
                        on_done.Call(absl::OkStatus());
                        latch.Set();
                        return absl::OkStatus();
                      }),
               [](auto) { LOG(INFO) << "WaitForData1 resolved"; });
  party->Spawn("ReadDataAndWaitForData",
               TrySeq(
                   [&keep_alive_system, &execution_order]() {
                     execution_order.append("1");
                     keep_alive_system.GotData();
                     // redundant GotData call
                     keep_alive_system.GotData();
                     return absl::OkStatus();
                   },
                   [&latch]() { return latch.Wait(); },
                   [&keep_alive_system, &latch2, &execution_order] {
                     execution_order.append("3");
                     latch2.Set();
                     return keep_alive_system.TestOnlyWaitForData();
                   },
                   [&on_done2, &execution_order] {
                     execution_order.append("5");
                     on_done2.Call(absl::OkStatus());
                     return absl::OkStatus();
                   }),
               [](auto) { LOG(INFO) << "WaitForData2 resolved"; });

  party->Spawn("ReadData2",
               TrySeq(latch2.Wait(),
                      [&keep_alive_system, &execution_order] {
                        execution_order.append("4");
                        keep_alive_system.GotData();
                        return absl::OkStatus();
                      }),
               [](auto) { LOG(INFO) << "ReadData2 end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
  EXPECT_STREQ(execution_order.c_str(), "12345");
}

YODEL_TEST(KeepAliveSystemTest, TestKeepAliveTimeoutGotData) {
  InitParty();
  int end_after = 2;
  std::unique_ptr<StrictMock<MockKeepAliveSystemInterface>>
      keep_alive_interface =
          std::make_unique<StrictMock<MockKeepAliveSystemInterface>>();
  keep_alive_interface->ExpectSendPingWithSleep(Duration::Hours(100),
                                                end_after);
  keep_alive_interface->ExpectKeepAliveTimeout();

  KeepAliveSystem keep_alive_system(std::move(keep_alive_interface),
                                    /*keepalive_timeout=*/Duration::Hours(1));
  auto party = GetParty();
  keep_alive_system.Spawn(party, Duration::Seconds(1));

  party->Spawn("ReadData", TrySeq([&keep_alive_system]() {
                 keep_alive_system.GotData();
                 return absl::OkStatus();
               }),
               [](auto) { LOG(INFO) << "ReadData end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

}  // namespace grpc_core
