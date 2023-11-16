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

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/common_closures.h"

#include <memory>

#include "absl/functional/any_invocable.h"
#include "gtest/gtest.h"

#include "src/core/lib/gprpp/notification.h"

using ::grpc_event_engine::experimental::AnyInvocableClosure;
using ::grpc_event_engine::experimental::SelfDeletingClosure;

class AnyInvocableClosureTest : public testing::Test {};

TEST_F(AnyInvocableClosureTest, CallsItsFunction) {
  grpc_core::Notification signal;
  AnyInvocableClosure closure([&signal] { signal.Notify(); });
  closure.Run();
  signal.WaitForNotification();
}

class SelfDeletingClosureTest : public testing::Test {};

TEST_F(SelfDeletingClosureTest, CallsItsFunction) {
  grpc_core::Notification signal;
  auto* closure = SelfDeletingClosure::Create([&signal] { signal.Notify(); });
  closure->Run();
  signal.WaitForNotification();
  // ASAN should catch if this closure is not deleted
}

TEST_F(SelfDeletingClosureTest, CallsItsFunctionAndIsDestroyed) {
  grpc_core::Notification fn_called;
  grpc_core::Notification destroyed;
  auto* closure =
      SelfDeletingClosure::Create([&fn_called] { fn_called.Notify(); },
                                  [&destroyed] { destroyed.Notify(); });
  closure->Run();
  fn_called.WaitForNotification();
  destroyed.WaitForNotification();
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
