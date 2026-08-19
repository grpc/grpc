// Copyright 2021 gRPC authors.
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

#include "src/core/lib/promise/race.h"

#include <memory>

#include "src/core/lib/promise/poll.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {

Poll<int> instant1() { return 1; }
Poll<int> instant2() { return 2; }
Poll<int> never() { return Pending(); }

TEST(RaceTest, Race1) { EXPECT_EQ(Race(instant1)(), Poll<int>(1)); }
TEST(RaceTest, Race2A) { EXPECT_EQ(Race(instant1, never)(), Poll<int>(1)); }
TEST(RaceTest, Race2B) { EXPECT_EQ(Race(never, instant1)(), Poll<int>(1)); }

TEST(RaceTest, BothPending) {
  // If all the participants return Pending, the Race also returns Pending.
  auto race = Race(never, never);
  EXPECT_EQ(race(), Poll<int>(Pending{}));
  EXPECT_EQ(race(), Poll<int>(Pending{}));
  EXPECT_EQ(race(), Poll<int>(Pending{}));
}

TEST(RaceTest, BothReady) {
  // If both are ready, the left one should win.
  auto race1 = Race(instant1, instant2);
  auto race2 = Race(instant2, instant1);
  EXPECT_EQ(race1(), Poll<int>(1));
  EXPECT_EQ(race2(), Poll<int>(2));
}

TEST(RaceTest, OneReturnsError) {
  // A race returning an absl::Status, where one promise returns an error.
  auto race = Race([]() -> Poll<absl::Status> { return Pending{}; },
                   []() -> Poll<absl::Status> {
                     return absl::InternalError("Error Message");
                   });

  auto result = race();
  EXPECT_TRUE(result.ready());
  EXPECT_EQ(result.value().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(result.value().message(), "Error Message");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
