// Copyright 2025 gRPC authors.
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

#include "src/core/call/delay_tracker.h"

#include "gtest/gtest.h"
#include "test/core/promise/poll_matcher.h"
#include "test/core/promise/test_context.h"

namespace grpc_core {

//
// DelayTracker tests
//

class DelayTrackerTest : public ::testing::Test {
 protected:
  void IncrementTimeBy(Duration duration) {
    time_cache_.TestOnlySetNow(Timestamp::Now() + duration);
  }

  ScopedTimeCache time_cache_;
  DelayTracker tracker_;
};

TEST_F(DelayTrackerTest, FinishedDelay) {
  DelayTracker tracker;
  auto handle = tracker.StartDelay("foo");
  IncrementTimeBy(Duration::Seconds(5));
  tracker.EndDelay(handle);
  EXPECT_EQ(tracker.GetDelayInfo(), "foo delay 5000ms");
}

TEST_F(DelayTrackerTest, UnfinishedDelay) {
  DelayTracker tracker;
  tracker.StartDelay("foo");
  IncrementTimeBy(Duration::Seconds(5));
  EXPECT_EQ(tracker.GetDelayInfo(), "foo timed out after 5000ms");
}

TEST_F(DelayTrackerTest, ConcurrentDelays) {
  DelayTracker tracker;
  auto handle0 = tracker.StartDelay("foo");
  IncrementTimeBy(Duration::Seconds(1));
  auto handle1 = tracker.StartDelay("bar");
  IncrementTimeBy(Duration::Seconds(2));
  auto handle2 = tracker.StartDelay("baz");
  IncrementTimeBy(Duration::Seconds(3));
  tracker.EndDelay(handle0);
  IncrementTimeBy(Duration::Seconds(4));
  tracker.EndDelay(handle1);
  IncrementTimeBy(Duration::Seconds(5));
  tracker.EndDelay(handle2);
  EXPECT_EQ(tracker.GetDelayInfo(),
            "foo delay 6000ms; bar delay 9000ms; baz delay 12000ms");
}

TEST_F(DelayTrackerTest, Children) {
  DelayTracker tracker;
  auto handle = tracker.StartDelay("foo");
  IncrementTimeBy(Duration::Seconds(5));
  tracker.EndDelay(handle);
  DelayTracker child0;
  handle = child0.StartDelay("bar");
  IncrementTimeBy(Duration::Seconds(3));
  child0.EndDelay(handle);
  DelayTracker child1;
  handle = child1.StartDelay("baz");
  IncrementTimeBy(Duration::Seconds(4));
  child1.EndDelay(handle);
  tracker.AddChild("attempt 0", std::move(child0));
  tracker.AddChild("attempt 1", std::move(child1));
  EXPECT_EQ(tracker.GetDelayInfo(),
            "foo delay 5000ms; "
            "attempt 0:[bar delay 3000ms]; "
            "attempt 1:[baz delay 4000ms]");
}

//
// TrackDelay promise tests
//

class TrackDelayTest : public ::testing::Test {
 protected:
  void IncrementTimeBy(Duration duration) {
    time_cache_.TestOnlySetNow(Timestamp::Now() + duration);
  }

  RefCountedPtr<Arena> arena_ = SimpleArenaAllocator()->MakeArena();
  TestContext<Arena> context_{arena_.get()};
  ScopedTimeCache time_cache_;
};

TEST_F(TrackDelayTest, NoDelay) {
  auto x = TrackDelay("foo", []() { return 42; });
  EXPECT_THAT(x(), IsReady(42));
  EXPECT_EQ(MaybeGetContext<DelayTracker>(), nullptr);
}

TEST_F(TrackDelayTest, Delay) {
  EXPECT_EQ(MaybeGetContext<DelayTracker>(), nullptr);
  auto x = TrackDelay("foo", [n = 1]() mutable -> Poll<int> {
    if (n == 0) return 42;
    --n;
    return Pending{};
  });
  EXPECT_THAT(x(), IsPending());
  DelayTracker* tracker = MaybeGetContext<DelayTracker>();
  ASSERT_NE(tracker, nullptr);
  IncrementTimeBy(Duration::Seconds(1));
  EXPECT_EQ(tracker->GetDelayInfo(), "foo timed out after 1000ms");
  IncrementTimeBy(Duration::Seconds(2));
  EXPECT_THAT(x(), IsReady(42));
  EXPECT_EQ(tracker->GetDelayInfo(), "foo delay 3000ms");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
