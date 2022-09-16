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

#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"
#include "src/core/lib/promise/race.h"
#include "test/core/promise/test_wakeup_schedulers.h"

namespace grpc_core {
namespace {

TEST(Sleep, Zzzz) {
  ExecCtx exec_ctx;
  Notification done;
  Timestamp done_time = Timestamp::Now() + Duration::Seconds(1);
  // Sleep for one second then set done to true.
  auto activity = MakeActivity(Sleep(done_time), InlineWakeupScheduler(),
                               [&done](absl::Status r) {
                                 EXPECT_EQ(r, absl::OkStatus());
                                 done.Notify();
                               });
  done.WaitForNotification();
  exec_ctx.InvalidateNow();
  EXPECT_GE(Timestamp::Now(), done_time);
}

TEST(Sleep, AlreadyDone) {
  ExecCtx exec_ctx;
  Notification done;
  Timestamp done_time = Timestamp::Now() - Duration::Seconds(1);
  // Sleep for no time at all then set done to true.
  auto activity = MakeActivity(Sleep(done_time), InlineWakeupScheduler(),
                               [&done](absl::Status r) {
                                 EXPECT_EQ(r, absl::OkStatus());
                                 done.Notify();
                               });
  done.WaitForNotification();
}

TEST(Sleep, Cancel) {
  ExecCtx exec_ctx;
  Notification done;
  Timestamp done_time = Timestamp::Now() + Duration::Seconds(1);
  // Sleep for one second but race it to complete immediately
  auto activity = MakeActivity(
      Race(Sleep(done_time), [] { return absl::CancelledError(); }),
      InlineWakeupScheduler(), [&done](absl::Status r) {
        EXPECT_EQ(r, absl::CancelledError());
        done.Notify();
      });
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
  auto activity = MakeActivity(std::move(sleeper), InlineWakeupScheduler(),
                               [&done](absl::Status r) {
                                 EXPECT_EQ(r, absl::OkStatus());
                                 done.Notify();
                               });
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
  gpr_log(GPR_INFO, "Starting %d sleeps for 1sec", kNumActivities);
  for (int i = 0; i < kNumActivities; i++) {
    auto notification = std::make_shared<Notification>();
    auto activity = MakeActivity(
        Sleep(Timestamp::Now() + Duration::Seconds(1)),
        ExecCtxWakeupScheduler(),
        [notification](absl::Status /*r*/) { notification->Notify(); });
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
