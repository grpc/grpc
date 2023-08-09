// Copyright 2022 gRPC authors.
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

#include "src/core/lib/promise/sleep.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"
#include "src/core/lib/promise/race.h"
#include "test/core/event_engine/mock_event_engine.h"
#include "test/core/promise/test_wakeup_schedulers.h"

using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::GetDefaultEventEngine;
using grpc_event_engine::experimental::MockEventEngine;
using testing::_;
using testing::DoAll;
using testing::Matcher;
using testing::Mock;
using testing::Return;
using testing::SaveArg;
using testing::StrictMock;

namespace grpc_core {
namespace {

TEST(Sleep, Zzzz) {
  ExecCtx exec_ctx;
  Notification done;
  Timestamp done_time = Timestamp::Now() + Duration::Seconds(1);
  auto engine = GetDefaultEventEngine();
  // Sleep for one second then set done to true.
  auto activity = MakeActivity(
      Sleep(done_time), InlineWakeupScheduler(),
      [&done](absl::Status r) {
        EXPECT_EQ(r, absl::OkStatus());
        done.Notify();
      },
      engine.get());
  done.WaitForNotification();
  exec_ctx.InvalidateNow();
  EXPECT_GE(Timestamp::Now(), done_time);
}

TEST(Sleep, OverlyEagerEventEngine) {
  StrictMock<MockEventEngine> mock_event_engine;

  ExecCtx exec_ctx;
  bool done = false;
  // Schedule a sleep for a very long time.
  Timestamp done_time = Timestamp::Now() + Duration::Seconds(1e6);
  EventEngine::Closure* wakeup = nullptr;
  EXPECT_CALL(mock_event_engine, RunAfter(_, Matcher<EventEngine::Closure*>(_)))
      .WillOnce(
          DoAll(SaveArg<1>(&wakeup), Return(EventEngine::TaskHandle{42, 123})));
  auto activity = MakeActivity(
      Sleep(done_time), InlineWakeupScheduler(),
      [&done](absl::Status r) {
        EXPECT_EQ(r, absl::OkStatus());
        done = true;
      },
      static_cast<EventEngine*>(&mock_event_engine));
  Mock::VerifyAndClearExpectations(&mock_event_engine);
  EXPECT_NE(wakeup, nullptr);
  EXPECT_FALSE(done);
  // Schedule the wakeup instantaneously - It won't have passed the scheduled
  // time yet, but sleep should believe the EventEngine.
  wakeup->Run();
  EXPECT_TRUE(done);
}

TEST(Sleep, AlreadyDone) {
  ExecCtx exec_ctx;
  Notification done;
  Timestamp done_time = Timestamp::Now() - Duration::Seconds(1);
  auto engine = GetDefaultEventEngine();
  // Sleep for no time at all then set done to true.
  auto activity = MakeActivity(
      Sleep(done_time), InlineWakeupScheduler(),
      [&done](absl::Status r) {
        EXPECT_EQ(r, absl::OkStatus());
        done.Notify();
      },
      engine.get());
  done.WaitForNotification();
}

TEST(Sleep, Cancel) {
  ExecCtx exec_ctx;
  Notification done;
  Timestamp done_time = Timestamp::Now() + Duration::Seconds(1);
  auto engine = GetDefaultEventEngine();
  // Sleep for one second but race it to complete immediately
  auto activity = MakeActivity(
      Race(Sleep(done_time), [] { return absl::CancelledError(); }),
      InlineWakeupScheduler(),
      [&done](absl::Status r) {
        EXPECT_EQ(r, absl::CancelledError());
        done.Notify();
      },
      engine.get());
  done.WaitForNotification();
  exec_ctx.InvalidateNow();
  EXPECT_LT(Timestamp::Now(), done_time);
}

TEST(Sleep, MoveSemantics) {
  // ASAN should help determine if there are any memory leaks here
  ExecCtx exec_ctx;
  Notification done;
  Timestamp done_time = Timestamp::Now() + Duration::Milliseconds(111);
  Sleep donor(done_time);
  Sleep sleeper = std::move(donor);
  auto engine = GetDefaultEventEngine();
  auto activity = MakeActivity(
      std::move(sleeper), InlineWakeupScheduler(),
      [&done](absl::Status r) {
        EXPECT_EQ(r, absl::OkStatus());
        done.Notify();
      },
      engine.get());
  done.WaitForNotification();
  exec_ctx.InvalidateNow();
  EXPECT_GE(Timestamp::Now(), done_time);
}

TEST(Sleep, StressTest) {
  // Kick off a bunch sleeps for one second.
  static const int kNumActivities = 100000;
  ExecCtx exec_ctx;
  std::vector<std::shared_ptr<Notification>> notifications;
  std::vector<ActivityPtr> activities;
  auto engine = GetDefaultEventEngine();
  gpr_log(GPR_INFO, "Starting %d sleeps for 1sec", kNumActivities);
  for (int i = 0; i < kNumActivities; i++) {
    auto notification = std::make_shared<Notification>();
    auto activity = MakeActivity(
        Sleep(Timestamp::Now() + Duration::Seconds(1)),
        ExecCtxWakeupScheduler(),
        [notification](absl::Status /*r*/) { notification->Notify(); },
        engine.get());
    notifications.push_back(std::move(notification));
    activities.push_back(std::move(activity));
  }
  gpr_log(GPR_INFO,
          "Waiting for the first %d sleeps, whilst cancelling the other half",
          kNumActivities / 2);
  for (size_t i = 0; i < kNumActivities / 2; i++) {
    notifications[i]->WaitForNotification();
    activities[i].reset();
    activities[i + kNumActivities / 2].reset();
    exec_ctx.Flush();
  }
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
