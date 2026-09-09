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

#include "test/core/test_util/test_config.h"

#include "src/core/lib/slice/byte_source.h"
#include "src/core/lib/slice/slice.h"
#include "gtest/gtest.h"

using grpc_core::ByteSource;
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
  EXPECT_EQ("abc", sp->data());
  EXPECT_EQ(5u, src.remaining());
}

TEST(ByteSourceTest, Skip) {
  auto s = Slice::FromCopiedBuffer("hello", 5);
  ByteSource src(s);
  EXPECT_TRUE(src.Skip(3));
  EXPECT_EQ(2u, src.remaining());
  EXPECT_EQ('o', *src.ReadU8());
  EXPECT_FALSE(src.Skip(3));
}

TEST(ByteSourceTest, CopyTo) {
  char buf[3] = {};
  auto s = Slice::FromCopiedBuffer("abcde", 5);
  ByteSource src(s);
  EXPECT_TRUE(src.CopyTo(buf, 3));
  EXPECT_EQ("abc", buf);
  EXPECT_EQ(2u, src.remaining());
}

TEST(ByteSourceTest, CopyAtMost) {
  char buf[3] = {};
  auto s = Slice::FromCopiedBuffer("abcde", 5);
  ByteSource src(s);
  EXPECT_EQ(3u, src.CopyAtMost(buf, 5));
  EXPECT_EQ("abc", buf);
  EXPECT_EQ(2u, src.remaining());
}

TEST(ByteSourceTest, OutOfBounds) {
  ByteSource src(nullptr, 0);
  EXPECT_FALSE(src.ReadU8());
  EXPECT_FALSE(src.ReadU16BE());
  EXPECT_FALSE(src.ReadSpan(1));
  EXPECT_FALSE(src.Skip(1));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
