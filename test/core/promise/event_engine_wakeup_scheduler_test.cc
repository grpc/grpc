// Copyright 2021 gRPC authors.
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

#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <stdlib.h>

#include <memory>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/util/notification.h"

namespace grpc_core {

TEST(EventEngineWakeupSchedulerTest, Works) {
  int state = 0;
  Notification done;
  auto activity = MakeActivity(
      [&state]() mutable -> Poll<absl::Status> {
        ++state;
        switch (state) {
          case 1:
            return Pending();
          case 2:
            return absl::OkStatus();
          default:
            abort();
        }
      },
      EventEngineWakeupScheduler(
          grpc_event_engine::experimental::CreateEventEngine()),
      [&done](absl::Status status) {
        EXPECT_EQ(status, absl::OkStatus());
        done.Notify();
      });

  EXPECT_EQ(state, 1);
  EXPECT_FALSE(done.HasBeenNotified());
  activity->ForceWakeup();
  done.WaitForNotification();
  EXPECT_EQ(state, 2);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  auto r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
