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
  MOCK_METHOD(bool, NeedToSendKeepAlivePing, (), (override));

  void ExpectSendPing(int& end_after) {
    DCHECK_GT(end_after, 0);
    EXPECT_CALL(*this, SendPing())
        .Times(end_after)
        .WillRepeatedly([&end_after] {
          LOG(INFO) << "ExpectSendPing Called. Remaining times: "
                    << end_after - 1;
          if (--end_after == 0) {
            return Immediate(absl::CancelledError());
          }
          return Immediate(absl::OkStatus());
        });
  }

  void ExpectSendPingWithSleep(Duration duration, int& end_after) {
    DCHECK_GT(end_after, 0);
    EXPECT_CALL(*this, SendPing())
        .Times(end_after)
        .WillRepeatedly([duration, &end_after] {
          LOG(INFO) << "ExpectSendPingWithSleep Called. Remaining times: "
                    << end_after - 1;
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

  void ExpectNeedToSendKeepAlivePing(int times, bool return_value) {
    EXPECT_CALL(*this, NeedToSendKeepAlivePing())
        .Times(times)
        .WillRepeatedly([return_value] { return return_value; });
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
  // Simple test to trigger two keepalive pings. The first one resolves
  // successfully and the second one returns a failure.
  InitParty();
  int end_after = 2;
  Duration keepalive_timeout = Duration::Infinity();
  Duration keepalive_interval = Duration::Seconds(1);

  std::unique_ptr<StrictMock<MockKeepAliveSystemInterface>>
      keep_alive_interface =
          std::make_unique<StrictMock<MockKeepAliveSystemInterface>>();
  keep_alive_interface->ExpectSendPing(end_after);
  keep_alive_interface->ExpectNeedToSendKeepAlivePing(/*times=*/2,
                                                      /*return_value=*/true);

  KeepAliveSystem keep_alive_system(std::move(keep_alive_interface),
                                    keepalive_timeout, keepalive_interval);
  auto party = GetParty();
  keep_alive_system.Spawn(party);

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(KeepAliveSystemTest, TestKeepAliveTimeout) {
  // Simple test to simulate sending a keepalive ping and not receiving any data
  // within the keepalive timeout. The test asserts that:
  // 1. The keepalive timeout is triggered.
  // 2. The keepalive ping is sent.
  InitParty();
  int end_after = 1;
  Duration keepalive_timeout = Duration::Seconds(1);
  Duration keepalive_interval = Duration::Seconds(1);

  std::unique_ptr<StrictMock<MockKeepAliveSystemInterface>>
      keep_alive_interface =
          std::make_unique<StrictMock<MockKeepAliveSystemInterface>>();
  keep_alive_interface->ExpectKeepAliveTimeout();
  keep_alive_interface->ExpectSendPingWithSleep(Duration::Hours(1), end_after);
  keep_alive_interface->ExpectNeedToSendKeepAlivePing(/*times=*/1,
                                                      /*return_value=*/true);

  KeepAliveSystem keep_alive_system(std::move(keep_alive_interface),
                                    keepalive_timeout, keepalive_interval);
  auto party = GetParty();
  keep_alive_system.Spawn(party);

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(KeepAliveSystemTest, TestKeepAliveWithData) {
  // Test to simulate reading of data at certain intervals. The test asserts
  // that:
  // 1. The keepalive ping is not sent as long as there is data read within the
  //    keepalive interval.
  InitParty();
  int end_after = 1;
  Duration keepalive_timeout = Duration::Hours(1);
  Duration keepalive_interval = Duration::Hours(1);
  int read_loop_end_after = 5;
  std::unique_ptr<StrictMock<MockKeepAliveSystemInterface>>
      keep_alive_interface =
          std::make_unique<StrictMock<MockKeepAliveSystemInterface>>();

  // Break keepalive loop
  keep_alive_interface->ExpectSendPing(end_after);
  keep_alive_interface->ExpectNeedToSendKeepAlivePing(/*times=*/1,
                                                      /*return_value=*/true);

  KeepAliveSystem keep_alive_system(std::move(keep_alive_interface),
                                    keepalive_timeout, keepalive_interval);
  auto party = GetParty();
  keep_alive_system.Spawn(party);

  party->Spawn("ReadData", Loop([&read_loop_end_after, &keep_alive_system]() {
                 return TrySeq(
                     [&keep_alive_system] {
                       keep_alive_system.GotData();
                       return absl::OkStatus();
                     },
                     Sleep(Duration::Minutes(75)),
                     [&read_loop_end_after]() mutable -> LoopCtl<absl::Status> {
                       if (--read_loop_end_after == 0) {
                         return absl::OkStatus();
                       }
                       return Continue();
                     });
               }),
               [](auto) { LOG(INFO) << "ReadData end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

// YODEL_TEST(KeepAliveSystemTest, TestKeepAliveTimeoutWithData) {
//   InitParty();
//   int end_after = 1;
//   Duration keepalive_timeout = Duration::Seconds(1);
//   Duration keepalive_interval = Duration::Hours(1);
//   int read_loop_end_after = 5;
//   std::unique_ptr<StrictMock<MockKeepAliveSystemInterface>>
//       keep_alive_interface =
//           std::make_unique<StrictMock<MockKeepAliveSystemInterface>>();

//   // Break keepalive loop
//   keep_alive_interface->ExpectSendPingWithSleep(Duration::Hours(1),
//   end_after); keep_alive_interface->ExpectKeepAliveTimeout();
//   keep_alive_interface->ExpectNeedToSendKeepAlivePing(/*times=*/1,
//                                                       /*return_value=*/true);

//   KeepAliveSystem keep_alive_system(std::move(keep_alive_interface),
//                                     keepalive_timeout, keepalive_interval);
//   auto party = GetParty();
//   keep_alive_system.Spawn(party);

//   party->Spawn("ReadData", Loop([&read_loop_end_after, &keep_alive_system]()
//   {
//                  return TrySeq(
//                      [&keep_alive_system] {
//                        keep_alive_system.GotData();
//                        return absl::OkStatus();
//                      },
//                      Sleep(Duration::Minutes(75)),
//                      [&read_loop_end_after]() mutable ->
//                      LoopCtl<absl::Status> {
//                        if (--read_loop_end_after == 0) {
//                          return absl::OkStatus();
//                        }
//                        return Continue();
//                      });
//                }),
//                [](auto) { LOG(INFO) << "ReadData end"; });

//   WaitForAllPendingWork();
//   event_engine()->TickUntilIdle();
//   event_engine()->UnsetGlobalHooks();
// }

}  // namespace grpc_core
