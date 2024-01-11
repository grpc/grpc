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

#include "src/core/lib/avl/avl.h"

#include <memory>

#include "gtest/gtest.h"

namespace grpc_core {

TEST(AvlTest, NoOp) { AVL<int, int> avl; }

TEST(AvlTest, Lookup) {
  auto avl = AVL<int, int>().Add(1, 42);
  EXPECT_EQ(nullptr, avl.Lookup(2));
  EXPECT_EQ(42, *avl.Lookup(1));
  avl = avl.Remove(1);
  EXPECT_EQ(nullptr, avl.Lookup(1));
  avl = avl.Add(1, 42).Add(1, 1);
  EXPECT_EQ(1, *avl.Lookup(1));
  avl = avl.Add(2, 2).Add(3, 3).Add(4, 4);
  EXPECT_EQ(1, *avl.Lookup(1));
  EXPECT_EQ(2, *avl.Lookup(2));
  EXPECT_EQ(3, *avl.Lookup(3));
  EXPECT_EQ(4, *avl.Lookup(4));
  EXPECT_EQ(nullptr, avl.Lookup(5));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
