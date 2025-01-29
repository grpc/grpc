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

namespace grpc_core {

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

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
