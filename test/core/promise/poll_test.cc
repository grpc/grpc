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

#include "src/core/lib/promise/poll.h"
#include <gtest/gtest.h>

namespace grpc_core {

TEST(PollTest, Pending) {
  Poll<int> i = PENDING;
  EXPECT_EQ(i.pending(), true);
  EXPECT_EQ(i.ready(), false);
  EXPECT_EQ(i.get_ready(), nullptr);
  EXPECT_EQ(i.Map([](int i) -> int { abort(); }).pending(), true);
}

TEST(PollTest, Ready) {
  Poll<int> i = 1;
  EXPECT_EQ(i.pending(), false);
  EXPECT_EQ(i.ready(), true);
  EXPECT_EQ(*i.get_ready(), 1);
  EXPECT_EQ(*i.Map([](int i) { return i + 1; }).get_ready(), 2);
  EXPECT_EQ(i.pending(), false);
  EXPECT_EQ(i.ready(), true);
}

TEST(PollTest, Take) {
  Poll<int> i = 1;
  EXPECT_EQ(i.pending(), false);
  EXPECT_EQ(i.ready(), true);
  EXPECT_EQ(*i.get_ready(), 1);
  Poll<int> j = i.take();
  EXPECT_EQ(j.pending(), false);
  EXPECT_EQ(j.ready(), true);
  EXPECT_EQ(*j.get_ready(), 1);
  EXPECT_EQ(i.pending(), true);
  EXPECT_EQ(i.ready(), false);
  EXPECT_EQ(i.get_ready(), nullptr);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
