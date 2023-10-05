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

#include "src/core/lib/promise/prioritized_race.h"

#include <memory>

#include "gtest/gtest.h"

#include "src/core/lib/promise/poll.h"

namespace grpc_core {

Poll<int> instant() { return 1; }
Poll<int> never() { return Pending(); }

TEST(PrioritizedRaceTest, Race1) {
  EXPECT_EQ(PrioritizedRace(instant)(), Poll<int>(1));
}

TEST(PrioritizedRaceTest, Race2A) {
  EXPECT_EQ(PrioritizedRace(instant, never)(), Poll<int>(1));
}

TEST(PrioritizedRaceTest, Race2B) {
  EXPECT_EQ(PrioritizedRace(never, instant)(), Poll<int>(1));
}

TEST(PrioritizedRaceTest, PrioritizedCompletion2A) {
  int first_polls = 0;
  int second_polls = 0;
  auto r = PrioritizedRace(
      [&first_polls]() -> Poll<int> {
        ++first_polls;
        return 1;
      },
      [&second_polls]() {
        ++second_polls;
        return 2;
      })();
  EXPECT_EQ(r, Poll<int>(1));
  // First promise completes immediately, so second promise is never polled.
  EXPECT_EQ(first_polls, 1);
  EXPECT_EQ(second_polls, 0);
}

TEST(PrioritizedRaceTest, PrioritizedCompletion2B) {
  int first_polls = 0;
  int second_polls = 0;
  auto r = PrioritizedRace(
      [&first_polls]() -> Poll<int> {
        ++first_polls;
        if (first_polls > 1) return 1;
        return Pending{};
      },
      [&second_polls]() {
        ++second_polls;
        return 2;
      })();
  EXPECT_EQ(r, Poll<int>(1));
  // First promise completes after second promise is polled.
  EXPECT_EQ(first_polls, 2);
  EXPECT_EQ(second_polls, 1);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
