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
#include "src/core/lib/slice/slice.h"
#include "gtest/gtest.h"
#include "absl/types/span.h"
#include <cstring>

using grpc_core::ByteSource;
using grpc_core::ByteSink;
using grpc_core::Slice;

TEST(ByteSourceTest, Empty) {
  ByteSource src;
  EXPECT_TRUE(src.empty());
  EXPECT_EQ(0u, src.remaining());
}

TEST(ByteSourceTest, FromSlice) {
  auto s = Slice::FromCopiedBuffer("hello", 5);
  ByteSource src(s);
  EXPECT_FALSE(src.empty());
  EXPECT_EQ(5u, src.remaining());
  EXPECT_EQ('h', *src.Peek());
  EXPECT_EQ('h', *src.ReadU8());
  EXPECT_EQ(4u, src.remaining());
}

TEST(ByteSourceTest, ReadU8) {
  auto s = Slice::FromCopiedBuffer("\x01\x02\x03", 3);
  ByteSource src(s);
  EXPECT_EQ(1u, *src.ReadU8());
  EXPECT_EQ(2u, *src.ReadU8());
  EXPECT_EQ(3u, *src.ReadU8());
  EXPECT_FALSE(src.ReadU8());
}

TEST(ByteSourceTest, ReadU16BE) {
  auto s = Slice::FromCopiedBuffer("\x12\x34", 2);
  ByteSource src(s);
  EXPECT_EQ(0x1234u, *src.ReadU16BE());
  EXPECT_FALSE(src.ReadU16BE());
}

TEST(ByteSourceTest, ReadU32BE) {
  auto s = Slice::FromCopiedBuffer("\x12\x34\x56\x78", 4);
  ByteSource src(s);
  EXPECT_EQ(0x12345678u, *src.ReadU32BE());
  EXPECT_FALSE(src.ReadU32BE());
}

TEST(ByteSourceTest, ReadU64BE) {
  auto s = Slice::FromCopiedBuffer("\x12\x34\x56\x78\x9a\xbc\xde\xf0", 8);
  ByteSource src(s);
  EXPECT_EQ(0x123456789abcdef0ull, *src.ReadU64BE());
  EXPECT_FALSE(src.ReadU64BE());
}

TEST(ByteSourceTest, ReadSpan) {
  auto s = Slice::FromCopiedBuffer("abcdefgh", 8);
  ByteSource src(s);
  auto sp = src.ReadSpan(3);
  EXPECT_TRUE(sp.has_value());
  EXPECT_EQ(3u, sp->size());
  EXPECT_EQ('a', sp->data()[0]);
  EXPECT_EQ('b', sp->data()[1]);
  EXPECT_EQ('c', sp->data()[2]);
  EXPECT_EQ(5u, src.remaining());
}

TEST(ByteSourceTest, Skip) {
  auto s = Slice::FromCopiedBuffer("hello", 5);
  ByteSource src(s);
  EXPECT_TRUE(src.Skip(3));
  EXPECT_EQ(2u, src.remaining());
  EXPECT_EQ('l', *src.ReadU8());
  EXPECT_FALSE(src.Skip(3));
}

TEST(ByteSourceTest, CopyTo) {
  uint8_t buf[3] = {};
  auto s = Slice::FromCopiedBuffer("abcde", 5);
  ByteSource src(s);
  EXPECT_TRUE(src.CopyTo(absl::MakeSpan(buf), 3));
  EXPECT_EQ('a', buf[0]);
  EXPECT_EQ('b', buf[1]);
  EXPECT_EQ('c', buf[2]);
  EXPECT_EQ(2u, src.remaining());
}

TEST(ByteSourceTest, CopyAtMost) {
  uint8_t buf[3] = {};
  auto s = Slice::FromCopiedBuffer("abcde", 5);
  ByteSource src(s);
  EXPECT_EQ(3u, src.CopyAtMost(absl::MakeSpan(buf)));
  EXPECT_EQ('a', buf[0]);
  EXPECT_EQ('b', buf[1]);
  EXPECT_EQ('c', buf[2]);
  EXPECT_EQ(2u, src.remaining());
}

TEST(ByteSourceTest, OutOfBounds) {
  ByteSource src(nullptr, 0);
  EXPECT_FALSE(src.ReadU8());
  EXPECT_FALSE(src.ReadU16BE());
  EXPECT_FALSE(src.ReadSpan(1));
  EXPECT_FALSE(src.Skip(1));
}

TEST(ByteSourceTest, ReadU24BE) {
  auto s = Slice::FromCopiedBuffer("\x12\x34\x56", 3);
  ByteSource src(s);
  EXPECT_EQ(0x123456u, *src.ReadU24BE());
  EXPECT_FALSE(src.ReadU24BE());
}

TEST(ByteSourceTest, ReadU31BE) {
  auto s = Slice::FromCopiedBuffer("\xff\xff\xff\xff", 4);
  ByteSource src(s);
  EXPECT_EQ(0x7fffffffu, *src.ReadU31BE());
  EXPECT_FALSE(src.ReadU31BE());
}

TEST(ByteSourceTest, CopyToChecksDestinationSize) {
  uint8_t buf[2] = {0xaa, 0xaa};

  auto s = Slice::FromCopiedBuffer("abc", 3);
  ByteSource src(s);

  EXPECT_FALSE(src.CopyTo(absl::MakeSpan(buf), 3));

  EXPECT_EQ(3u, src.remaining());
  EXPECT_EQ(0xaa, buf[0]);
  EXPECT_EQ(0xaa, buf[1]);
}

TEST(ByteSinkTest, WriteU8) {
  uint8_t buf[1] = {};

  ByteSink sink(absl::MakeSpan(buf));

  EXPECT_TRUE(sink.WriteU8(0x12));
  EXPECT_EQ(0x12, buf[0]);
  EXPECT_TRUE(sink.empty());
}

TEST(ByteSinkTest, WriteIntegers) {
  uint8_t buf[18] = {};

  ByteSink sink(absl::MakeSpan(buf));

  EXPECT_TRUE(sink.WriteU8(0x01));
  EXPECT_TRUE(sink.WriteU16BE(0x2345));
  EXPECT_TRUE(sink.WriteU24BE(0x6789ab));
  EXPECT_TRUE(sink.WriteU32BE(0xcdef0123));
  EXPECT_TRUE(sink.WriteU64BE(0x456789abcdef0123ull));

  EXPECT_EQ(18u, sink.bytes_written());

  const uint8_t expected[] = {
      0x01,
      0x23, 0x45,
      0x67, 0x89, 0xab,
      0xcd, 0xef, 0x01, 0x23,
      0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23,
  };

  EXPECT_EQ(0, memcmp(buf, expected, sizeof(expected)));
}

TEST(ByteSinkTest, MultiByteWriteDoesNotPartiallyWrite) {
  uint8_t buf[3] = {0xaa, 0xaa, 0xaa};

  ByteSink sink(absl::MakeSpan(buf));

  EXPECT_FALSE(sink.WriteU32BE(0x12345678));
  EXPECT_EQ(0u, sink.bytes_written());

  EXPECT_EQ(0xaa, buf[0]);
  EXPECT_EQ(0xaa, buf[1]);
  EXPECT_EQ(0xaa, buf[2]);
}

TEST(ByteSinkTest, WriteSpan) {
  uint8_t buf[5] = {};
  const uint8_t input[] = {1, 2, 3};

  ByteSink sink(absl::MakeSpan(buf));

  EXPECT_TRUE(sink.WriteSpan(absl::MakeConstSpan(input)));
  EXPECT_EQ(3u, sink.bytes_written());

  EXPECT_EQ(1, buf[0]);
  EXPECT_EQ(2, buf[1]);
  EXPECT_EQ(3, buf[2]);
}

TEST(ByteSinkTest, WriteSpanOutOfBounds) {
  uint8_t buf[2] = {0xaa, 0xaa};
  const uint8_t input[] = {1, 2, 3};

  ByteSink sink(absl::MakeSpan(buf));

  EXPECT_FALSE(sink.WriteSpan(absl::MakeConstSpan(input)));
  EXPECT_EQ(0u, sink.bytes_written());

  EXPECT_EQ(0xaa, buf[0]);
  EXPECT_EQ(0xaa, buf[1]);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
