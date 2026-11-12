//
// Copyright 2023 The gRPC Authors
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

#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"

#include <grpc/support/port_platform.h>

#include <functional>
#include <memory>

#include "src/core/util/time.h"
#include "gtest/gtest.h"
#include "absl/synchronization/notification.h"

using ::grpc_event_engine::experimental::FuzzingEventEngine;

TEST(FuzzingEventEngine, RunAfterAndTickForDuration) {
  auto fuzzing_ee = std::make_shared<FuzzingEventEngine>(
      FuzzingEventEngine::Options(), fuzzing_event_engine::Actions());
  absl::Notification notification1;
  absl::Notification notification2;
  fuzzing_ee->RunAfter(grpc_core::Duration::Milliseconds(250), [&]() {
    notification1.Notify();
    fuzzing_ee->RunAfter(grpc_core::Duration::Milliseconds(250),
                         [&]() { notification2.Notify(); });
  });
  EXPECT_FALSE(notification1.HasBeenNotified());
  fuzzing_ee->TickForDuration(grpc_core::Duration::Milliseconds(250));
  EXPECT_TRUE(notification1.HasBeenNotified());
  EXPECT_FALSE(notification2.HasBeenNotified());
  fuzzing_ee->TickForDuration(grpc_core::Duration::Milliseconds(250));
  EXPECT_TRUE(notification2.HasBeenNotified());
}

TEST(FuzzingEventEngine, IsIdleIgnoringTimers) {
  auto fuzzing_ee = std::make_shared<FuzzingEventEngine>(
      FuzzingEventEngine::Options(), fuzzing_event_engine::Actions());
  EXPECT_TRUE(fuzzing_ee->IsIdle());
  EXPECT_TRUE(fuzzing_ee->IsIdleIgnoringTimers());

  // Schedule a recurring timer and a chained Run() callback.
  int timer_runs = 0;
  std::function<void()> schedule_recurring_timer;
  schedule_recurring_timer = [&]() {
    fuzzing_ee->RunAfter(grpc_core::Duration::Seconds(1), [&]() {
      ++timer_runs;
      schedule_recurring_timer();
    });
  };
  schedule_recurring_timer();
  EXPECT_FALSE(fuzzing_ee->IsIdle());
  EXPECT_TRUE(fuzzing_ee->IsIdleIgnoringTimers());

  bool run1_called = false;
  bool run2_called = false;
  fuzzing_ee->Run([&]() {
    run1_called = true;
    fuzzing_ee->Run([&]() { run2_called = true; });
  });
  EXPECT_FALSE(fuzzing_ee->IsIdleIgnoringTimers());
  fuzzing_ee->TickUntilIdleIgnoringTimers();
  EXPECT_TRUE(run1_called);
  EXPECT_TRUE(run2_called);
  EXPECT_EQ(timer_runs, 0);
  EXPECT_TRUE(fuzzing_ee->IsIdleIgnoringTimers());

  // Cancelling a zero-delay RunAfter() immediately restores idle state.
  bool zero_delay_called = false;
  auto handle = fuzzing_ee->RunAfter(grpc_core::Duration::Zero(),
                                     [&]() { zero_delay_called = true; });
  EXPECT_FALSE(fuzzing_ee->IsIdleIgnoringTimers());
  EXPECT_TRUE(fuzzing_ee->Cancel(handle));
  EXPECT_TRUE(fuzzing_ee->IsIdleIgnoringTimers());
  fuzzing_ee->TickForDuration(grpc_core::Duration::Seconds(1));
  EXPECT_FALSE(zero_delay_called);
  EXPECT_EQ(timer_runs, 1);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
