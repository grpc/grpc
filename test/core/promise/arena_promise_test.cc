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

#include "src/core/lib/promise/arena_promise.h"

#include <memory>

#include <gtest/gtest.h>

namespace grpc_core {

TEST(ArenaPromiseTest, EmptyCallableNeedsNoArena) {
  ArenaPromise<int> p([] { return Poll<int>(42); });
  EXPECT_EQ(p(), Poll<int>(42));
}

TEST(ArenaPromiseTest, AllocatedWorks) {
  auto arena = MakeScopedArena(1024);
  int x = 42;
  ArenaPromise<int> p(arena.get(), [x] { return Poll<int>(x); });
  EXPECT_EQ(p(), Poll<int>(42));
  p = ArenaPromise<int>(arena.get(), [] { return Poll<int>(43); });
  EXPECT_EQ(p(), Poll<int>(43));
}

TEST(ArenaPromiseTest, DestructionWorks) {
  auto arena = MakeScopedArena(1024);
  auto x = std::make_shared<int>(42);
  auto p = ArenaPromise<int>(arena.get(), [x] { return Poll<int>(*x); });
  ArenaPromise<int> q(std::move(p));
  EXPECT_EQ(q(), Poll<int>(42));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
