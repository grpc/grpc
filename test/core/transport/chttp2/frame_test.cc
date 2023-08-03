// Copyright 2023 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/frame.h"

#include <initializer_list>

#include "gtest/gtest.h"

#include "src/core/lib/transport/http2_errors.h"

namespace grpc_core {
namespace {

void DoTheseThings(std::initializer_list<int>) {}

template <typename... Frames>
std::vector<uint8_t> Serialize(Frames... f) {
  std::vector<Http2Frame> frames;
  DoTheseThings({(frames.emplace_back(std::move(f)), 1)...});
  SliceBuffer temp;
  Serialize(absl::Span<Http2Frame>(frames), temp);
  auto slice = temp.JoinIntoSlice();
  return std::vector<uint8_t>(slice.begin(), slice.end());
}

template <typename... I>
std::vector<uint8_t> ByteVec(I... i) {
  return std::vector<uint8_t>{static_cast<uint8_t>(i)...};
}

SliceBuffer SliceBufferFromString(absl::string_view s) {
  SliceBuffer temp;
  temp.Append(Slice::FromCopiedString(s));
  return temp;
}

TEST(Http2FrameTest, Serialization) {
  EXPECT_EQ(Serialize(Http2DataFrame{1, false, SliceBufferFromString("hello")}),
            ByteVec(0, 0, 5, 0, 0, 0, 0, 0, 1, 'h', 'e', 'l', 'l', 'o'));
  EXPECT_EQ(Serialize(Http2DataFrame{0x98381822, true,
                                     SliceBufferFromString("kids")}),
            ByteVec(0, 0, 4, 0, 1, 0x98, 0x38, 0x18, 0x22, 'k', 'i', 'd', 's'));
  EXPECT_EQ(Serialize(Http2HeaderFrame{1, false, false,
                                       SliceBufferFromString("hello")}),
            ByteVec(0, 0, 5, 1, 0, 0, 0, 0, 1, 'h', 'e', 'l', 'l', 'o'));
  EXPECT_EQ(Serialize(Http2HeaderFrame{1, true, false,
                                       SliceBufferFromString("hello")}),
            ByteVec(0, 0, 5, 1, 4, 0, 0, 0, 1, 'h', 'e', 'l', 'l', 'o'));
  EXPECT_EQ(Serialize(Http2HeaderFrame{1, false, true,
                                       SliceBufferFromString("hello")}),
            ByteVec(0, 0, 5, 1, 1, 0, 0, 0, 1, 'h', 'e', 'l', 'l', 'o'));
  EXPECT_EQ(Serialize(Http2HeaderFrame{1, true, true,
                                       SliceBufferFromString("hello")}),
            ByteVec(0, 0, 5, 1, 5, 0, 0, 0, 1, 'h', 'e', 'l', 'l', 'o'));
  EXPECT_EQ(Serialize(Http2ContinuationFrame{1, false,
                                             SliceBufferFromString("hello")}),
            ByteVec(0, 0, 5, 9, 0, 0, 0, 0, 1, 'h', 'e', 'l', 'l', 'o'));
  EXPECT_EQ(Serialize(Http2ContinuationFrame{1, true,
                                             SliceBufferFromString("hello")}),
            ByteVec(0, 0, 5, 9, 4, 0, 0, 0, 1, 'h', 'e', 'l', 'l', 'o'));
  EXPECT_EQ(Serialize(Http2RstStreamFrame{1, GRPC_HTTP2_CONNECT_ERROR}),
            ByteVec(0, 0, 4, 3, 0, 0, 0, 0, 1, 0, 0, 0, 0x0a));
  EXPECT_EQ(Serialize(Http2SettingsFrame{}),
            ByteVec(0, 0, 0, 4, 0, 0, 0, 0, 0));
  EXPECT_EQ(
      Serialize(Http2SettingsFrame{false, {{0x1234, 0x9abcdef0}}}),
      ByteVec(0, 0, 6, 4, 0, 0, 0, 0, 0, 0x12, 0x34, 0x9a, 0xbc, 0xde, 0xf0));
  EXPECT_EQ(Serialize(Http2SettingsFrame{
                false, {{0x1234, 0x9abcdef0}, {0x4321, 0x12345678}}}),
            ByteVec(0, 0, 12, 4, 0, 0, 0, 0, 0, 0x12, 0x34, 0x9a, 0xbc, 0xde,
                    0xf0, 0x43, 0x21, 0x12, 0x34, 0x56, 0x78));
  EXPECT_EQ(Serialize(Http2SettingsFrame{true, {}}),
            ByteVec(0, 0, 0, 4, 1, 0, 0, 0, 0));
  EXPECT_EQ(Serialize(Http2PingFrame{false, 0x123456789abcdef0}),
            ByteVec(0, 0, 8, 6, 0, 0, 0, 0, 0, 0x12, 0x34, 0x56, 0x78, 0x9a,
                    0xbc, 0xde, 0xf0));
  EXPECT_EQ(Serialize(Http2PingFrame{true, 0x123456789abcdef0}),
            ByteVec(0, 0, 8, 6, 1, 0, 0, 0, 0, 0x12, 0x34, 0x56, 0x78, 0x9a,
                    0xbc, 0xde, 0xf0));
  EXPECT_EQ(Serialize(Http2GoawayFrame{0x12345678, GRPC_HTTP2_ENHANCE_YOUR_CALM,
                                       Slice::FromCopiedString("hello")}),
            ByteVec(0, 0, 13, 7, 0, 0, 0, 0, 0, 0x12, 0x34, 0x56, 0x78, 0, 0, 0,
                    0x0b, 'h', 'e', 'l', 'l', 'o'));
  EXPECT_EQ(Serialize(Http2WindowUpdateFrame{1, 0x12345678}),
            ByteVec(0, 0, 4, 8, 0, 0, 0, 0, 1, 0x12, 0x34, 0x56, 0x78));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
