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

#include "src/core/util/useful.h"

#include <stdint.h>

#include <limits>
#include <memory>

#include "gtest/gtest.h"

#include <grpc/support/port_platform.h>

namespace grpc_core {

TEST(UsefulTest, ClampWorks) {
  EXPECT_EQ(Clamp(1, 0, 2), 1);
  EXPECT_EQ(Clamp(0, 0, 2), 0);
  EXPECT_EQ(Clamp(2, 0, 2), 2);
  EXPECT_EQ(Clamp(-1, 0, 2), 0);
  EXPECT_EQ(Clamp(3, 0, 2), 2);
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

TEST(UsefulTest, CountTrailingZeros32) {
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(1)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(2)), 1);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(3)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(4)), 2);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(5)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(6)), 1);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(7)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(8)), 3);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(9)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(10)), 1);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(11)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(12)), 2);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(13)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(14)), 1);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(15)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(16)), 4);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(256)), 8);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(65535)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(65536)), 16);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint32_t>(0x80000000)), 31);
}

TEST(UsefulTest, CountTrailingZeros64) {
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(1)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(2)), 1);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(3)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(4)), 2);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(5)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(6)), 1);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(7)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(8)), 3);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(9)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(10)), 1);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(11)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(12)), 2);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(13)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(14)), 1);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(15)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(16)), 4);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(256)), 8);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(65535)), 0);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(65536)), 16);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(0x80000000)), 31);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(0x100000000)), 32);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(0x1000000000000)), 48);
  EXPECT_EQ(CountTrailingZeros(static_cast<uint64_t>(0x8000000000000000)), 63);
}

TEST(UsefulTest, LowestOneBit8) {
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(0)), 0);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(1)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(2)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(3)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(4)), 4);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(5)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(6)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(7)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(8)), 8);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(9)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(10)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(11)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(12)), 4);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(13)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(14)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(15)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(16)), 16);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(127)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint8_t>(128)), 128);
}

TEST(UsefulTest, LowestOneBit16) {
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(0)), 0);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(1)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(2)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(3)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(4)), 4);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(5)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(6)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(7)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(8)), 8);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(9)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(10)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(11)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(12)), 4);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(13)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(14)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(15)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(16)), 16);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(32767)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint16_t>(32768)), 32768);
}

TEST(UsefulTest, LowestOneBit32) {
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(0)), 0);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(1)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(2)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(3)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(4)), 4);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(5)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(6)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(7)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(8)), 8);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(9)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(10)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(11)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(12)), 4);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(13)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(14)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(15)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(16)), 16);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(2147483647)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint32_t>(2147483648)), 2147483648);
}

TEST(UsefulTest, LowestOneBit64) {
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(0)), 0);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(1)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(2)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(3)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(4)), 4);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(5)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(6)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(7)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(8)), 8);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(9)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(10)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(11)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(12)), 4);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(13)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(14)), 2);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(15)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(16)), 16);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(9223372036854775807)), 1);
  EXPECT_EQ(LowestOneBit(static_cast<uint64_t>(9223372036854775808U)),
            9223372036854775808U);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
