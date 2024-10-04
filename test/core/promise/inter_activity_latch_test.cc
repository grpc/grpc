// Copyright 2023 gRPC authors.
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

#include "src/core/lib/promise/inter_activity_latch.h"

#include <grpc/grpc.h>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/util/notification.h"

using grpc_event_engine::experimental::GetDefaultEventEngine;

namespace grpc_core {
namespace {

TEST(InterActivityLatchTest, Works) {
  InterActivityLatch<void> latch;

  // Start some waiting activities
  Notification n1;
  auto a1 = MakeActivity(
      [&] {
        return Seq(latch.Wait(), [&](Empty) {
          n1.Notify();
          return absl::OkStatus();
        });
      },
      EventEngineWakeupScheduler{GetDefaultEventEngine()}, [](absl::Status) {});
  Notification n2;
  auto a2 = MakeActivity(
      [&] {
        return Seq(latch.Wait(), [&](Empty) {
          n2.Notify();
          return absl::OkStatus();
        });
      },
      EventEngineWakeupScheduler{GetDefaultEventEngine()}, [](absl::Status) {});
  Notification n3;
  auto a3 = MakeActivity(
      [&] {
        return Seq(latch.Wait(), [&](Empty) {
          n3.Notify();
          return absl::OkStatus();
        });
      },
      EventEngineWakeupScheduler{GetDefaultEventEngine()}, [](absl::Status) {});

  ASSERT_FALSE(n1.HasBeenNotified());
  ASSERT_FALSE(n2.HasBeenNotified());
  ASSERT_FALSE(n3.HasBeenNotified());

  // Start a setting activity
  auto kicker = MakeActivity(
      [&] {
        latch.Set();
        return absl::OkStatus();
      },
      EventEngineWakeupScheduler{GetDefaultEventEngine()}, [](absl::Status) {});

  // Start another waiting activity
  Notification n4;
  auto a4 = MakeActivity(
      [&] {
        return Seq(latch.Wait(), [&](Empty) {
          n4.Notify();
          return absl::OkStatus();
        });
      },
      EventEngineWakeupScheduler{GetDefaultEventEngine()}, [](absl::Status) {});

  // Everything should finish
  n1.WaitForNotification();
  n2.WaitForNotification();
  n3.WaitForNotification();
  n4.WaitForNotification();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();  // for GetDefaultEventEngine
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
