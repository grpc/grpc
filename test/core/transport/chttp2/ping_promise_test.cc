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
            LOG(INFO) << "SendPing returning: " << absl::CancelledError();
            return Immediate(absl::CancelledError());
          }
          LOG(INFO) << "SendPing returning: " << absl::OkStatus();
          return Immediate(absl::OkStatus());
        });
  }
  void ExpectSendPing(Duration duration) {
    EXPECT_CALL(*this, SendPing()).WillOnce([duration] {
      return TrySeq(Sleep(duration),
                    [] { return Immediate(absl::OkStatus()); });
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
                                    Duration::Infinity(), Duration::Seconds(1));
  auto party = GetParty();
  keep_alive_system.Spawn(party);

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(KeepAliveSystemTest, TestKeepAliveTimeout) {
  InitParty();
  std::unique_ptr<StrictMock<MockKeepAliveSystemInterface>>
      keep_alive_interface =
          std::make_unique<StrictMock<MockKeepAliveSystemInterface>>();
  keep_alive_interface->ExpectKeepAliveTimeout();
  keep_alive_interface->ExpectSendPing(Duration::Hours(1));

  KeepAliveSystem keep_alive_system(std::move(keep_alive_interface),
                                    Duration::Hours(1), Duration::Seconds(1));
  auto party = GetParty();
  keep_alive_system.Spawn(party);

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

}  // namespace grpc_core
