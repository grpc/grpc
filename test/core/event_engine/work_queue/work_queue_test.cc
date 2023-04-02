// Copyright 2022 The gRPC Authors
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

#include "src/core/lib/event_engine/work_queue.h"

#include <thread>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/util/test_config.h"

namespace {
using ::grpc_event_engine::experimental::AnyInvocableClosure;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::WorkQueue;

TEST(WorkQueueTest, StartsEmpty) {
  WorkQueue queue;
  ASSERT_TRUE(queue.Empty());
}

TEST(WorkQueueTest, TakesClosures) {
  WorkQueue queue;
  bool ran = false;
  AnyInvocableClosure closure([&ran] { ran = true; });
  queue.Add(&closure);
  ASSERT_FALSE(queue.Empty());
  EventEngine::Closure* popped = queue.PopFront();
  ASSERT_NE(popped, nullptr);
  popped->Run();
  ASSERT_TRUE(ran);
  ASSERT_TRUE(queue.Empty());
}

TEST(WorkQueueTest, TakesAnyInvocables) {
  WorkQueue queue;
  bool ran = false;
  queue.Add([&ran] { ran = true; });
  ASSERT_FALSE(queue.Empty());
  EventEngine::Closure* popped = queue.PopFront();
  ASSERT_NE(popped, nullptr);
  popped->Run();
  ASSERT_TRUE(ran);
  ASSERT_TRUE(queue.Empty());
}

TEST(WorkQueueTest, BecomesEmptyOnPopBack) {
  WorkQueue queue;
  bool ran = false;
  queue.Add([&ran] { ran = true; });
  ASSERT_FALSE(queue.Empty());
  EventEngine::Closure* closure = queue.PopBack();
  ASSERT_NE(closure, nullptr);
  closure->Run();
  ASSERT_TRUE(ran);
  ASSERT_TRUE(queue.Empty());
}

TEST(WorkQueueTest, PopFrontIsFIFO) {
  WorkQueue queue;
  int flag = 0;
  queue.Add([&flag] { flag |= 1; });
  queue.Add([&flag] { flag |= 2; });
  queue.PopFront()->Run();
  EXPECT_TRUE(flag & 1);
  EXPECT_FALSE(flag & 2);
  queue.PopFront()->Run();
  EXPECT_TRUE(flag & 1);
  EXPECT_TRUE(flag & 2);
  ASSERT_TRUE(queue.Empty());
}

TEST(WorkQueueTest, PopBackIsLIFO) {
  WorkQueue queue;
  int flag = 0;
  queue.Add([&flag] { flag |= 1; });
  queue.Add([&flag] { flag |= 2; });
  queue.PopBack()->Run();
  EXPECT_FALSE(flag & 1);
  EXPECT_TRUE(flag & 2);
  queue.PopBack()->Run();
  EXPECT_TRUE(flag & 1);
  EXPECT_TRUE(flag & 2);
  ASSERT_TRUE(queue.Empty());
}

TEST(WorkQueueTest, OldestEnqueuedTimestampIsSane) {
  WorkQueue queue;
  ASSERT_EQ(queue.OldestEnqueuedTimestamp(), grpc_core::Timestamp::InfPast());
  queue.Add([] {});
  ASSERT_LE(queue.OldestEnqueuedTimestamp(), grpc_core::Timestamp::Now());
  auto* popped = queue.PopFront();
  ASSERT_EQ(queue.OldestEnqueuedTimestamp(), grpc_core::Timestamp::InfPast());
  // prevent leaks by executing or deleting the closure
  delete popped;
}

TEST(WorkQueueTest, OldestEnqueuedTimestampOrderingIsCorrect) {
  WorkQueue queue;
  AnyInvocableClosure closure([] {});
  queue.Add(&closure);
  absl::SleepFor(absl::Milliseconds(2));
  queue.Add(&closure);
  absl::SleepFor(absl::Milliseconds(2));
  queue.Add(&closure);
  absl::SleepFor(absl::Milliseconds(2));
  auto oldest_ts = queue.OldestEnqueuedTimestamp();
  ASSERT_LE(oldest_ts, grpc_core::Timestamp::Now());
  // pop the oldest, and ensure the next oldest is younger
  EventEngine::Closure* popped = queue.PopFront();
  ASSERT_NE(popped, nullptr);
  auto second_oldest_ts = queue.OldestEnqueuedTimestamp();
  ASSERT_GT(second_oldest_ts, oldest_ts);
  // pop the oldest, and ensure the last one is youngest
  popped = queue.PopFront();
  ASSERT_NE(popped, nullptr);
  auto youngest_ts = queue.OldestEnqueuedTimestamp();
  ASSERT_GT(youngest_ts, second_oldest_ts);
  ASSERT_GT(youngest_ts, oldest_ts);
}

TEST(WorkQueueTest, ThreadedStress) {
  WorkQueue queue;
  constexpr int thd_count = 33;
  constexpr int element_count_per_thd = 3333;
  std::vector<std::thread> threads;
  threads.reserve(thd_count);
  class TestClosure : public EventEngine::Closure {
   public:
    void Run() override { delete this; }
  };
  for (int i = 0; i < thd_count; i++) {
    threads.emplace_back([&] {
      for (int j = 0; j < element_count_per_thd; j++) {
        queue.Add(new TestClosure());
      }
      int run_count = 0;
      while (run_count < element_count_per_thd) {
        if (auto* c = queue.PopFront()) {
          c->Run();
          ++run_count;
        }
      }
    });
  }
  for (auto& thd : threads) thd.join();
  EXPECT_TRUE(queue.Empty());
}

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
