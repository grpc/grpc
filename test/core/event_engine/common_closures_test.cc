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

#include <gtest/gtest.h>

#include "src/core/lib/event_engine/promise.h"
#include "test/core/util/test_config.h"

using ::grpc_event_engine::experimental::AnyInvocableClosure;
using ::grpc_event_engine::experimental::Promise;
using ::grpc_event_engine::experimental::SelfDeletingClosure;

class AnyInvocableClosureTest : public testing::Test {};

TEST_F(AnyInvocableClosureTest, CallsItsFunction) {
  Promise<bool> promise;
  AnyInvocableClosure closure([&promise] { promise.Set(true); });
  closure.Run();
  ASSERT_TRUE(promise.WaitWithTimeout(absl::Seconds(3)));
}

class SelfDeletingClosureTest : public testing::Test {};

TEST_F(SelfDeletingClosureTest, CallsItsFunction) {
  Promise<bool> promise;
  auto* closure =
      SelfDeletingClosure::Create([&promise] { promise.Set(true); });
  closure->Run();
  ASSERT_TRUE(promise.WaitWithTimeout(absl::Seconds(3)));
  // ASAN should catch if this closure is not deleted
}

TEST_F(SelfDeletingClosureTest, CallsItsFunctionAndIsDestroyed) {
  Promise<bool> fn_called;
  Promise<bool> destroyed;
  auto* closure =
      SelfDeletingClosure::Create([&fn_called] { fn_called.Set(true); },
                                  [&destroyed] { destroyed.Set(true); });
  closure->Run();
  ASSERT_TRUE(fn_called.WaitWithTimeout(absl::Seconds(3)));
  ASSERT_TRUE(destroyed.WaitWithTimeout(absl::Seconds(3)));
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
