// Copyright 2022 gRPC authors.
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

#include "src/core/lib/gprpp/sorted_pack.h"

#include <vector>

#include "gtest/gtest.h"

using grpc_core::WithSortedPack;

template <int I>
struct Int {
  int value() const { return I; }
};

template <typename A, typename B>
struct Cmp;

template <int A, int B>
struct Cmp<Int<A>, Int<B>> {
  static constexpr bool kValue = A < B;
};

template <typename... Args>
struct VecMaker {
  static std::vector<int> Make() { return {Args().value()...}; }
};

template <int... Args>
std::vector<int> TestVec() {
  return WithSortedPack<VecMaker, Cmp, Int<Args>...>::Type::Make();
}

TEST(SortedPackTest, Empty) { EXPECT_EQ(TestVec<>(), std::vector<int>{}); }

TEST(SortedPackTest, LenOne) {
  EXPECT_EQ((TestVec<1>()), (std::vector<int>{1}));
  EXPECT_EQ((TestVec<2>()), (std::vector<int>{2}));
}

TEST(SortedPackTest, Len2) {
  EXPECT_EQ((TestVec<1, 2>()), (std::vector<int>{1, 2}));
  EXPECT_EQ((TestVec<2, 1>()), (std::vector<int>{1, 2}));
}

TEST(SortedPackTest, Len3) {
  EXPECT_EQ((TestVec<1, 2, 3>()), (std::vector<int>{1, 2, 3}));
  EXPECT_EQ((TestVec<1, 3, 2>()), (std::vector<int>{1, 2, 3}));
  EXPECT_EQ((TestVec<2, 1, 3>()), (std::vector<int>{1, 2, 3}));
  EXPECT_EQ((TestVec<2, 3, 1>()), (std::vector<int>{1, 2, 3}));
  EXPECT_EQ((TestVec<3, 1, 2>()), (std::vector<int>{1, 2, 3}));
  EXPECT_EQ((TestVec<3, 2, 1>()), (std::vector<int>{1, 2, 3}));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
