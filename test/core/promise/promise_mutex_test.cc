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

#include "src/core/lib/promise/promise_mutex.h"

#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "test/core/promise/test_wakeup_schedulers.h"

namespace grpc_core {
namespace {

TEST(PromiseMutexTest, Basic) {
  PromiseMutex<int> mutex{1};
  bool done = false;
  MakeActivity(
      [&]() {
        return Seq(
            Join(Seq(mutex.Acquire(),
                     [](PromiseMutex<int>::Lock l) {
                       EXPECT_EQ(*l, 1);
                       *l = 2;
                     }),
                 Seq(mutex.Acquire(),
                     [](PromiseMutex<int>::Lock l) {
                       EXPECT_EQ(*l, 2);
                       *l = 3;
                     }),
                 Seq(mutex.Acquire(),
                     [](PromiseMutex<int>::Lock l) { EXPECT_EQ(*l, 3); })),
            []() { return absl::OkStatus(); });
      },
      InlineWakeupScheduler(),
      [&done](absl::Status status) {
        EXPECT_TRUE(status.ok());
        done = true;
      });
  EXPECT_TRUE(done);
  EXPECT_EQ(**NowOrNever(mutex.Acquire()), 3);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
