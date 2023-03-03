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

#include <memory>

#include "gtest/gtest.h"

namespace grpc_core {

TEST(PollTest, IsItPoll) {
  EXPECT_EQ(PollTraits<Poll<int>>::is_poll(), true);
  EXPECT_EQ(PollTraits<Poll<bool>>::is_poll(), true);
  EXPECT_EQ(PollTraits<Poll<std::unique_ptr<int>>>::is_poll(), true);
  EXPECT_EQ(PollTraits<int>::is_poll(), false);
  EXPECT_EQ(PollTraits<bool>::is_poll(), false);
  EXPECT_EQ(PollTraits<std::unique_ptr<int>>::is_poll(), false);
}

TEST(PollTest, Pending) {
  Poll<int> i = Pending();
  EXPECT_TRUE(i.pending());
}

TEST(PollTest, Ready) {
  Poll<int> i = 1;
  EXPECT_TRUE(i.ready());
  EXPECT_EQ(i.value(), 1);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
