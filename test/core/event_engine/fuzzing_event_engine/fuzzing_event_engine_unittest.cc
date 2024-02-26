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

#include <grpc/support/port_platform.h>

#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

#include "src/core/lib/gprpp/time.h"

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

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
