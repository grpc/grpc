// Copyright 2026 gRPC authors.
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

#include "src/core/lib/slice/byte_source.h"

#include <cstdint>

#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

TEST(ByteSourceTest, ReadU8) {
  const uint8_t input[] = {0x01, 0x02, 0x03};
  ByteSource src(input, sizeof(input));

  auto v = src.ReadU8();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(*v, 0x01);
  v = src.ReadU8();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(*v, 0x02);
  v = src.ReadU8();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(*v, 0x03);
  EXPECT_FALSE(src.ReadU8().has_value());
}

TEST(ByteSourceTest, ReadSpan) {
  const uint8_t input[] = {'a', 'b', 'c', 'd', 'e'};
  ByteSource src(input, sizeof(input));

  auto span = src.ReadSpan(3);
  ASSERT_TRUE(span.has_value());
  std::vector<uint8_t> actual(span->begin(), span->end());
  EXPECT_THAT(actual, ElementsAre('a', 'b', 'c'));
  EXPECT_EQ(src.remaining(), 2u);
}

TEST(ByteSourceTest, Skip) {
  const uint8_t input[] = {'a', 'b', 'c', 'd', 'e'};
  ByteSource src(input, sizeof(input));

  EXPECT_TRUE(src.Skip(3));
  EXPECT_EQ(src.remaining(), 2u);
  auto v = src.ReadU8();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(*v, 'd');
  EXPECT_FALSE(src.Skip(3));
}

TEST(ByteSourceTest, CopyTo) {
  const uint8_t input[] = {'a', 'b', 'c', 'd', 'e'};
  uint8_t output[3] = {};
  ByteSource src(input, sizeof(input));

  EXPECT_TRUE(src.CopyTo(absl::MakeSpan(output), sizeof(output)));
  EXPECT_THAT(std::vector<uint8_t>(output, output + 3), ElementsAre('a', 'b', 'c'));
  EXPECT_EQ(src.remaining(), 2u);
}

TEST(ByteSourceTest, CopyToChecksDestinationSize) {
  const uint8_t input[] = {'a', 'b', 'c'};
  uint8_t output[] = {0xaa, 0xaa};
  ByteSource src(input, sizeof(input));

  EXPECT_FALSE(src.CopyTo(absl::MakeSpan(output), 3));
  EXPECT_EQ(src.remaining(), 3u);
  EXPECT_THAT(std::vector<uint8_t>(output, output + 2), ElementsAre(0xaa, 0xaa));
}

TEST(ByteSourceTest, OutOfBounds) {
  ByteSource src(nullptr, 0);

  EXPECT_FALSE(src.ReadU8().has_value());
  EXPECT_FALSE(src.ReadSpan(1).has_value());
  EXPECT_FALSE(src.Skip(1));
}

TEST(ByteSinkTest, WriteU8) {
  uint8_t output[1] = {};
  ByteSink sink(absl::MakeSpan(output));

  ASSERT_TRUE(sink.WriteU8(0x12));
  EXPECT_THAT(std::vector<uint8_t>(output, output + 1), ElementsAre(0x12));
  EXPECT_TRUE(sink.empty());
}

TEST(ByteSinkTest, WriteIntegers) {
  uint8_t output[12] = {};
  ByteSink sink(absl::MakeSpan(output));

  ASSERT_TRUE(sink.WriteU8(0x01));
  ASSERT_TRUE(sink.WriteU24BE(0x234567));
  ASSERT_TRUE(sink.WriteU32BE(0x89abcdef));
  ASSERT_TRUE(sink.WriteU32BE(0x01234567));

  EXPECT_THAT(std::vector<uint8_t>(output, output + 12), ElementsAre(
      0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
      0xcd, 0xef, 0x01, 0x23, 0x45, 0x67));
}

TEST(ByteSinkTest, MultiByteWriteDoesNotPartiallyWrite) {
  uint8_t output[] = {0xaa, 0xaa, 0xaa};
  ByteSink sink(absl::MakeSpan(output));

  EXPECT_FALSE(sink.WriteU32BE(0x12345678));
  EXPECT_THAT(std::vector<uint8_t>(output, output + 3), ElementsAre(0xaa, 0xaa, 0xaa));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
