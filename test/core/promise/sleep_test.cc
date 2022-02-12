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

#include <atomic>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/synchronization/notification.h"

#include <grpc/grpc.h>

#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "test/core/promise/test_wakeup_schedulers.h"

namespace grpc_core {
namespace {

TEST(Sleep, Zzzz) {
  ExecCtx exec_ctx;
  absl::Notification done;
  grpc_millis done_time = ExecCtx::Get()->Now() + 1000;
  // Sleep for one second then set done to true.
  auto activity = MakeActivity(Sleep(done_time), InlineWakeupScheduler(),
                               [&done](absl::Status r) {
                                 EXPECT_EQ(r, absl::OkStatus());
                                 done.Notify();
                               });
  done.WaitForNotification();
  exec_ctx.InvalidateNow();
  EXPECT_GE(ExecCtx::Get()->Now(), done_time);
}

TEST(Sleep, AlreadyDone) {
  ExecCtx exec_ctx;
  absl::Notification done;
  grpc_millis done_time = ExecCtx::Get()->Now() - 1000;
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
  absl::Notification done;
  grpc_millis done_time = ExecCtx::Get()->Now() + 1000;
  // Sleep for one second but race it to complete immediately
  auto activity = MakeActivity(
      Race(Sleep(done_time), [] { return absl::CancelledError(); }),
      InlineWakeupScheduler(), [&done](absl::Status r) {
        EXPECT_EQ(r, absl::CancelledError());
        done.Notify();
      });
  done.WaitForNotification();
  exec_ctx.InvalidateNow();
  EXPECT_LT(ExecCtx::Get()->Now(), done_time);
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
