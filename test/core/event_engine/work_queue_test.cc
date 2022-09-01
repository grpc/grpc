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

#include <thread>

#include <gtest/gtest.h>

#include "src/core/lib/event_engine/workqueue.h"
#include "test/core/util/test_config.h"

namespace {
using ::grpc_event_engine::experimental::WorkQueue;

TEST(WorkQueueTest, StartsEmpty) {
  WorkQueue<int> queue;
  ASSERT_TRUE(queue.Empty());
}

TEST(WorkQueueTest, BecomesEmptyOnPopFront) {
  WorkQueue<int> queue;
  queue.Add(1);
  ASSERT_EQ(queue.Size(), 1);
  ASSERT_FALSE(queue.Empty());
  ASSERT_EQ(queue.PopFront(), 1);
  ASSERT_TRUE(queue.Empty());
}

TEST(WorkQueueTest, BecomesEmptyOnPopBack) {
  WorkQueue<int> queue;
  queue.Add(1);
  ASSERT_EQ(queue.Size(), 1);
  ASSERT_FALSE(queue.Empty());
  ASSERT_EQ(queue.PopBack(), 1);
  ASSERT_TRUE(queue.Empty());
}

TEST(WorkQueueTest, PopFrontIsFIFO) {
  WorkQueue<int> queue;
  queue.Add(1);
  queue.Add(2);
  queue.Add(3);
  queue.Add(4);
  ASSERT_EQ(queue.Size(), 4);
  ASSERT_FALSE(queue.Empty());
  ASSERT_EQ(queue.PopFront(), 1);
  ASSERT_EQ(queue.PopFront(), 2);
  ASSERT_EQ(queue.PopFront(), 3);
  ASSERT_EQ(queue.PopFront(), 4);
  ASSERT_TRUE(queue.Empty());
}

TEST(WorkQueueTest, PopBackIsIIFO) {
  WorkQueue<int> queue;
  queue.Add(1);
  queue.Add(2);
  queue.Add(3);
  queue.Add(4);
  ASSERT_EQ(queue.Size(), 4);
  ASSERT_FALSE(queue.Empty());
  ASSERT_EQ(queue.PopBack(), 4);
  ASSERT_EQ(queue.PopBack(), 3);
  ASSERT_EQ(queue.PopBack(), 2);
  ASSERT_EQ(queue.PopBack(), 1);
  ASSERT_TRUE(queue.Empty());
}

TEST(WorkQueueTest, OldestEnqueuedTimestampIsSane) {
  WorkQueue<int> queue;
  ASSERT_EQ(queue.OldestEnqueuedTimestamp(), queue.kInvalidTimestamp);
  queue.Add(42);
  grpc_core::ExecCtx exec_ctx;
  ASSERT_LE(queue.OldestEnqueuedTimestamp(), exec_ctx.Now());
  queue.PopFront();
  ASSERT_EQ(queue.OldestEnqueuedTimestamp(), queue.kInvalidTimestamp);
}

TEST(WorkQueueTest, OldestEnqueuedTimestampOrderingIsCorrect) {
  WorkQueue<int> queue;
  grpc_core::ExecCtx exec_ctx;
  queue.Add(42);
  absl::SleepFor(absl::Milliseconds(2));
  queue.Add(43);
  absl::SleepFor(absl::Milliseconds(2));
  queue.Add(44);
  absl::SleepFor(absl::Milliseconds(2));
  auto oldest_ts = queue.OldestEnqueuedTimestamp();
  ASSERT_LE(oldest_ts, exec_ctx.Now());
  absl::optional<int> popped;
  // pop the oldest, and ensure the next oldest is younger
  do {
    popped = queue.PopFront();
  } while (!popped.has_value());
  auto second_oldest_ts = queue.OldestEnqueuedTimestamp();
  ASSERT_GT(second_oldest_ts, oldest_ts);
  // pop the oldest, and ensure the last one is youngest
  do {
    popped = queue.PopFront();
  } while (!popped.has_value());
  auto youngest_ts = queue.OldestEnqueuedTimestamp();
  ASSERT_GT(youngest_ts, second_oldest_ts);
  ASSERT_GT(youngest_ts, oldest_ts);
}

TEST(WorkQueueTest, ThreadedStress) {
  WorkQueue<int> queue;
  constexpr int thd_count = 33;
  constexpr int element_count_per_thd = 3333;
  std::vector<std::thread> threads;
  threads.reserve(thd_count);
  for (int i = 0; i < thd_count; i++) {
    threads.emplace_back([&] {
      int cnt = 0;
      do {
        queue.Add(42);
        ++cnt;
      } while (cnt < element_count_per_thd);
      cnt = 0;
      do {
        if (queue.PopFront().has_value()) ++cnt;
      } while (cnt < element_count_per_thd);
    });
  }
  for (auto& thd : threads) thd.join();
  EXPECT_TRUE(queue.Empty());
  EXPECT_EQ(queue.Size(), 0);
}

TEST(WorkQueueTest, StressLargeObjects) {
  struct Element {
    char storage[250 * 1024];  // 250Kb
  };
  WorkQueue<Element> queue;
  constexpr int thd_count = 20;
  constexpr int element_count_per_thd = 50;
  // The queue should never exceed 250Mb, and will likely remain much smaller
  std::vector<std::thread> threads;
  threads.reserve(thd_count);
  for (int i = 0; i < thd_count; i++) {
    threads.emplace_back([&] {
      int cnt = 0;
      do {
        queue.Add(Element{"qwfparst"});
        ++cnt;
      } while (cnt < element_count_per_thd);
      cnt = 0;
      do {
        if (queue.PopFront().has_value()) ++cnt;
      } while (cnt < element_count_per_thd);
    });
  }
  for (auto& thd : threads) thd.join();
  EXPECT_TRUE(queue.Empty());
  EXPECT_EQ(queue.Size(), 0);
}

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}