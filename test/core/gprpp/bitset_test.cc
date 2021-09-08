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

#include "src/core/lib/gprpp/bitset.h"

#include <gtest/gtest.h>

namespace grpc_core {
namespace testing {

// Stand in type to make the size to test a type
template <std::size_t K>
struct Size {
  static constexpr std::size_t kBits = K;
};

using TestSizes = ::testing::Types<
    // All sizes up to 17 bits
    Size<1>, Size<2>, Size<3>, Size<4>, Size<5>, Size<6>, Size<7>, Size<8>,
    Size<9>, Size<10>, Size<11>, Size<12>, Size<13>, Size<14>, Size<15>,
    Size<16>, Size<17>,
    // Values around 32 bits
    Size<24>, Size<25>, Size<26>, Size<27>, Size<28>, Size<29>, Size<30>,
    Size<31>, Size<32>, Size<33>,
    // Values around 48 bits
    Size<47>, Size<48>, Size<49>,
    // Values around 64 bits
    Size<62>, Size<63>, Size<64>, Size<65>, Size<66>,
    // Values around 96 bits
    Size<95>, Size<96>, Size<97>,
    // Silly numbers of bits
    Size<1024>, Size<4000>, Size<4321> >;

template <typename S>
struct BitSetTest : public ::testing::Test {};

TYPED_TEST_SUITE(BitSetTest, TestSizes);

TYPED_TEST(BitSetTest, NoneAtInit) {
  BitSet<TypeParam::kBits> b;
  EXPECT_TRUE(b.none());
}

TYPED_TEST(BitSetTest, OneBit) {
  constexpr std::size_t kBits = TypeParam::kBits;
  for (std::size_t i = 0; i < kBits; i++) {
    BitSet<kBits> b;
    b.set(i);
    EXPECT_FALSE(b.none());
    for (std::size_t j = 0; j < kBits; j++) {
      EXPECT_EQ(b.is_set(j), i == j);
    }
  }
}

TYPED_TEST(BitSetTest, AllSet) {
  constexpr std::size_t kBits = TypeParam::kBits;
  BitSet<kBits> b;
  for (std::size_t i = 0; i < kBits; i++) {
    EXPECT_FALSE(b.all());
    b.set(i);
  }
  EXPECT_TRUE(b.all());
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
