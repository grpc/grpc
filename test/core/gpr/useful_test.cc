//
//
// Copyright 2015 gRPC authors.
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
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/gpr/useful.h"

#include <stdint.h>

#include <limits>
#include <memory>

#include "gtest/gtest.h"

namespace grpc_core {

TEST(UsefulTest, ClampWorks) {
  EXPECT_EQ(Clamp(1, 0, 2), 1);
  EXPECT_EQ(Clamp(0, 0, 2), 0);
  EXPECT_EQ(Clamp(2, 0, 2), 2);
  EXPECT_EQ(Clamp(-1, 0, 2), 0);
  EXPECT_EQ(Clamp(3, 0, 2), 2);
}

TEST(UsefulTest, Rotate) {
  EXPECT_EQ(RotateLeft(0x80000001u, 1u), 3);
  EXPECT_EQ(RotateRight(0x80000001u, 1u), 0xc0000000);
}

TEST(UsefulTest, ArraySize) {
  int four[4];
  int five[5];

  EXPECT_EQ(GPR_ARRAY_SIZE(four), 4);
  EXPECT_EQ(GPR_ARRAY_SIZE(five), 5);
}

TEST(UsefulTest, BitOps) {
  uint32_t bitset = 0;

  EXPECT_EQ(BitCount((1u << 31) - 1), 31);
  EXPECT_EQ(BitCount(1u << 3), 1);
  EXPECT_EQ(BitCount(0), 0);
  EXPECT_EQ(SetBit(&bitset, 3), 8);
  EXPECT_EQ(BitCount(bitset), 1);
  EXPECT_EQ(GetBit(bitset, 3), 1);
  EXPECT_EQ(SetBit(&bitset, 1), 10);
  EXPECT_EQ(BitCount(bitset), 2);
  EXPECT_EQ(ClearBit(&bitset, 3), 2);
  EXPECT_EQ(BitCount(bitset), 1);
  EXPECT_EQ(GetBit(bitset, 3), 0);
  EXPECT_EQ(BitCount(std::numeric_limits<uint64_t>::max()), 64);
}

TEST(UsefulTest, SaturatingAdd) {
  EXPECT_EQ(SaturatingAdd(0, 0), 0);
  EXPECT_EQ(SaturatingAdd(0, 1), 1);
  EXPECT_EQ(SaturatingAdd(1, 0), 1);
  EXPECT_EQ(SaturatingAdd(1, 1), 2);
  EXPECT_EQ(SaturatingAdd(std::numeric_limits<int64_t>::max(), 1),
            std::numeric_limits<int64_t>::max());
  EXPECT_EQ(SaturatingAdd(std::numeric_limits<int64_t>::max(),
                          std::numeric_limits<int64_t>::max()),
            std::numeric_limits<int64_t>::max());
  EXPECT_EQ(SaturatingAdd(std::numeric_limits<int64_t>::min(), -1),
            std::numeric_limits<int64_t>::min());
}

TEST(UsefulTest, RoundUpToPowerOf2) {
  EXPECT_EQ(RoundUpToPowerOf2(0), 0);
  EXPECT_EQ(RoundUpToPowerOf2(1), 1);
  EXPECT_EQ(RoundUpToPowerOf2(2), 2);
  EXPECT_EQ(RoundUpToPowerOf2(3), 4);
  EXPECT_EQ(RoundUpToPowerOf2(4), 4);
  EXPECT_EQ(RoundUpToPowerOf2(5), 8);
  EXPECT_EQ(RoundUpToPowerOf2(6), 8);
  EXPECT_EQ(RoundUpToPowerOf2(7), 8);
  EXPECT_EQ(RoundUpToPowerOf2(8), 8);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
