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

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/grpc.h>

#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/call/call_spine.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_client_transport.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/util/notification.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/time.h"
#include "test/core/promise/poll_matcher.h"
#include "test/core/test_util/postmortem.h"
#include "test/core/transport/chttp2/http2_frame_test_helper.h"
#include "test/core/transport/util/mock_promise_endpoint.h"
#include "test/core/transport/util/transport_test.h"

namespace grpc_core {
namespace http2 {
namespace testing {

using EventEngineSlice = grpc_event_engine::experimental::Slice;

class SettingsTimeoutManagerTest : public ::testing::Test {
 protected:
  RefCountedPtr<Party> MakeParty() {
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    return Party::Make(std::move(arena));
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
};

constexpr uint32_t kSettingsShortTimeout = 300;
constexpr uint32_t kSettingsLongTimeoutTest = 1400;

auto MockStartSettingsTimeout(SettingsTimeoutManager& manager) {
  LOG(INFO) << "MockStartSettingsTimeout Factory";
  return manager.WaitForSettingsTimeout();
}

auto MockSettingsAckReceived(SettingsTimeoutManager& manager) {
  LOG(INFO) << "MockSettingsAckReceived Factory";
  return [&manager]() -> Poll<absl::Status> {
    LOG(INFO) << "MockSettingsAckReceived OnSettingsAckReceived";
    manager.OnSettingsAckReceived();
    return absl::OkStatus();
  };
}

auto MockSettingsAckReceivedDelayed(SettingsTimeoutManager& manager) {
  LOG(INFO) << "MockSettingsAckReceived Factory";
  return TrySeq(Sleep(Duration::Milliseconds(kSettingsShortTimeout * 0.8)),
                [&manager]() -> Poll<absl::Status> {
                  LOG(INFO) << "MockSettingsAckReceived OnSettingsAckReceived";
                  manager.OnSettingsAckReceived();
                  return absl::OkStatus();
                });
}

TEST_F(SettingsTimeoutManagerTest, NoTimeoutOneSetting) {
  // First start the timer and then immediately send the ACK
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(ChannelArgs(),
                             Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsTimeoutManagerTest",
      TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                              MockSettingsAckReceived(manager)),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsTimeoutManagerTest, NoTimeoutThreeSettings) {
  // Starting the timer and sending the ACK immediately three times in a row.
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(ChannelArgs(),
                             Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsTimeoutManagerTest",
      TrySeq(TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager))),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsTimeoutManagerTest, NoTimeoutThreeSettingsDelayed) {
  // Starting the timer and sending the ACK immediately three times in a row.
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(ChannelArgs(),
                             Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsTimeoutManagerTest",
      TrySeq(TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceivedDelayed(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceivedDelayed(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceivedDelayed(manager))),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsTimeoutManagerTest, NoTimeoutOneSettingRareOrder) {
  // Emulating the case where we receive the ACK before we even spawn the timer.
  // This could happen if our write promise gets blocked on a very large write
  // and the RTT is low and peer responsiveness is high.
  //
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(ChannelArgs(),
                             Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsTimeoutManagerTest",
      TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                              MockStartSettingsTimeout(manager)),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsTimeoutManagerTest, NoTimeoutThreeSettingsRareOrder) {
  // Emulating the case where we receive the ACK before we even spawn the timer.
  // This could happen if our write promise gets blocked on a very large write
  // and the RTT is low and peer responsiveness is high.
  //
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(ChannelArgs(),
                             Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsTimeoutManagerTest",
      TrySeq(TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager)),
             TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager)),
             TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager))),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsTimeoutManagerTest, NoTimeoutThreeSettingsMixedOrder) {
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(ChannelArgs(),
                             Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsTimeoutManagerTest",
      TrySeq(TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager)),
             TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager)),
             TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager))),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsTimeoutManagerTest, TimeoutOneSetting) {
  // Testing one timeout test
  // Also ensuring that receiving the ACK after the timeout does not crash or
  // leak memory.
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(
      ChannelArgs().Set(GRPC_ARG_SETTINGS_TIMEOUT, kSettingsShortTimeout),
      Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification1;
  Notification notification2;
  party->Spawn("SettingsTimeoutManagerTestStart",
               MockStartSettingsTimeout(manager),
               [&notification1](absl::Status status) {
                 EXPECT_TRUE(absl::IsCancelled(status));
                 EXPECT_EQ(status.message(), RFC9113::kSettingsTimeout);
                 notification1.Notify();
               });
  party->Spawn(
      "SettingsTimeoutManagerTestAck",
      TrySeq(Sleep(Duration::Milliseconds(kSettingsLongTimeoutTest)),
             MockSettingsAckReceived(manager)),
      [&notification2](absl::Status status) { notification2.Notify(); });
  notification1.WaitForNotification();
  notification2.WaitForNotification();
}

}  // namespace testing

}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
