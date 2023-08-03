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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "src/core/lib/transport/http2_errors.h"

namespace grpc_core {
namespace {

MATCHER_P2(StatusIs, code, message, "") {
  return arg.code() == code && arg.message() == message;
}

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

std::vector<uint8_t> Serialize(const Http2FrameHeader& header) {
  uint8_t temp[9];
  header.Serialize(temp);
  return std::vector<uint8_t>(temp, temp + 9);
}

Http2FrameHeader ParseHeader(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3,
                             uint8_t b4, uint8_t b5, uint8_t b6, uint8_t b7,
                             uint8_t b8) {
  uint8_t temp[9] = {b0, b1, b2, b3, b4, b5, b6, b7, b8};
  return Http2FrameHeader::Parse(temp);
}

template <typename T, typename... I>
T ParseFrame(I... i) {
  SliceBuffer buffer;
  buffer.Append(Slice::FromCopiedBuffer(ByteVec(i...)));
  uint8_t hdr[9];
  buffer.MoveFirstNBytesIntoBuffer(9, hdr);
  auto frame_hdr = Http2FrameHeader::Parse(hdr);
  EXPECT_EQ(frame_hdr.length, buffer.Length())
      << "frame_hdr=" << frame_hdr.ToString();
  auto r = ParseFramePayload(frame_hdr, std::move(buffer));
  EXPECT_TRUE(r.ok()) << r.status();
  EXPECT_TRUE(absl::holds_alternative<T>(r.value()));
  return std::move(absl::get<T>(r.value()));
}

template <typename... I>
absl::Status ValidateFrame(I... i) {
  SliceBuffer buffer;
  buffer.Append(Slice::FromCopiedBuffer(ByteVec(i...)));
  uint8_t hdr[9];
  buffer.MoveFirstNBytesIntoBuffer(9, hdr);
  auto frame_hdr = Http2FrameHeader::Parse(hdr);
  EXPECT_EQ(frame_hdr.length, buffer.Length())
      << "frame_hdr=" << frame_hdr.ToString();
  return ParseFramePayload(frame_hdr, std::move(buffer)).status();
}

TEST(Header, Serialization) {
  EXPECT_EQ(Serialize(Http2FrameHeader{0, 0, 0, 0}),
            ByteVec(0, 0, 0, 0, 0, 0, 0, 0, 0));
  EXPECT_EQ(Serialize(Http2FrameHeader{0x123456, 0x9a, 0xbc, 0x12345678}),
            ByteVec(0x12, 0x34, 0x56, 0x9a, 0xbc, 0x12, 0x34, 0x56, 0x78));
}

TEST(Header, Parse) {
  EXPECT_EQ(ParseHeader(0, 0, 0, 0, 0, 0, 0, 0, 0),
            (Http2FrameHeader{0, 0, 0, 0}));
  EXPECT_EQ(ParseHeader(0x12, 0x34, 0x56, 0x9a, 0xbc, 0x12, 0x34, 0x56, 0x78),
            (Http2FrameHeader{0x123456, 0x9a, 0xbc, 0x12345678}));
}

TEST(Header, ToString) {
  EXPECT_EQ((Http2FrameHeader{0, 0, 0, 0}).ToString(),
            "{DATA: flags=0, stream_id=0, length=0}");
  EXPECT_EQ((Http2FrameHeader{0x123456, 0x9a, 0xbc, 0x12345678}).ToString(),
            "{UNKNOWN(154): flags=188, stream_id=305419896, length=1193046}");
}

TEST(Frame, Serialization) {
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

TEST(Frame, Parse) {
  EXPECT_EQ(ParseFrame<Http2DataFrame>(0, 0, 5, 0, 0, 0, 0, 0, 1, 'h', 'e', 'l',
                                       'l', 'o'),
            (Http2DataFrame{1, false, SliceBufferFromString("hello")}));
  EXPECT_EQ(ParseFrame<Http2DataFrame>(0, 0, 4, 0, 1, 0x98, 0x38, 0x18, 0x22,
                                       'k', 'i', 'd', 's'),
            (Http2DataFrame{0x98381822, true, SliceBufferFromString("kids")}));
  EXPECT_EQ(
      ParseFrame<Http2HeaderFrame>(0, 0, 5, 1, 0, 0, 0, 0, 1, 'h', 'e', 'l',
                                   'l', 'o'),
      (Http2HeaderFrame{1, false, false, SliceBufferFromString("hello")}));
  EXPECT_EQ(ParseFrame<Http2HeaderFrame>(0, 0, 4, 1, 4, 0x98, 0x38, 0x18, 0x22,
                                         'k', 'i', 'd', 's'),
            (Http2HeaderFrame{0x98381822, true, false,
                              SliceBufferFromString("kids")}));
  EXPECT_EQ(ParseFrame<Http2HeaderFrame>(0, 0, 4, 1, 1, 0x98, 0x38, 0x18, 0x22,
                                         'k', 'i', 'd', 's'),
            (Http2HeaderFrame{0x98381822, false, true,
                              SliceBufferFromString("kids")}));
  EXPECT_EQ(ParseFrame<Http2ContinuationFrame>(0, 0, 5, 9, 0, 0, 0, 0, 1, 'h',
                                               'e', 'l', 'l', 'o'),
            (Http2ContinuationFrame{1, false, SliceBufferFromString("hello")}));
  EXPECT_EQ(ParseFrame<Http2ContinuationFrame>(0, 0, 5, 9, 4, 0, 0, 0, 1, 'h',
                                               'e', 'l', 'l', 'o'),
            (Http2ContinuationFrame{1, true, SliceBufferFromString("hello")}));
  EXPECT_EQ(
      ParseFrame<Http2RstStreamFrame>(0, 0, 4, 3, 0, 0, 0, 0, 1, 0, 0, 0, 0x0a),
      (Http2RstStreamFrame{1, GRPC_HTTP2_CONNECT_ERROR}));
  EXPECT_EQ(ParseFrame<Http2SettingsFrame>(0, 0, 0, 4, 0, 0, 0, 0, 0),
            (Http2SettingsFrame{}));
  EXPECT_EQ(ParseFrame<Http2SettingsFrame>(0, 0, 6, 4, 0, 0, 0, 0, 0, 0x12,
                                           0x34, 0x9a, 0xbc, 0xde, 0xf0),
            (Http2SettingsFrame{false, {{0x1234, 0x9abcdef0}}}));
  EXPECT_EQ(ParseFrame<Http2SettingsFrame>(0, 0, 12, 4, 0, 0, 0, 0, 0, 0x12,
                                           0x34, 0x9a, 0xbc, 0xde, 0xf0, 0x43,
                                           0x21, 0x12, 0x34, 0x56, 0x78),
            (Http2SettingsFrame{false,
                                {{0x1234, 0x9abcdef0}, {0x4321, 0x12345678}}}));
  EXPECT_EQ(ParseFrame<Http2SettingsFrame>(0, 0, 0, 4, 1, 0, 0, 0, 0),
            (Http2SettingsFrame{true, {}}));
  EXPECT_EQ(ParseFrame<Http2PingFrame>(0, 0, 8, 6, 0, 0, 0, 0, 0, 0x12, 0x34,
                                       0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0),
            (Http2PingFrame{false, 0x123456789abcdef0}));
  EXPECT_EQ(ParseFrame<Http2PingFrame>(0, 0, 8, 6, 1, 0, 0, 0, 0, 0x12, 0x34,
                                       0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0),
            (Http2PingFrame{true, 0x123456789abcdef0}));
  EXPECT_EQ(ParseFrame<Http2GoawayFrame>(0, 0, 13, 7, 0, 0, 0, 0, 0, 0x12, 0x34,
                                         0x56, 0x78, 0, 0, 0, 0x0b, 'h', 'e',
                                         'l', 'l', 'o'),
            (Http2GoawayFrame{0x12345678, GRPC_HTTP2_ENHANCE_YOUR_CALM,
                              Slice::FromCopiedString("hello")}));
  EXPECT_EQ(ParseFrame<Http2WindowUpdateFrame>(0, 0, 4, 8, 0, 0, 0, 0, 1, 0x12,
                                               0x34, 0x56, 0x78),
            (Http2WindowUpdateFrame{1, 0x12345678}));
}

TEST(Frame, ParsePadded) {
  EXPECT_EQ(ParseFrame<Http2DataFrame>(0, 0, 9, 0, 8, 0, 0, 0, 1, 3, 'h', 'e',
                                       'l', 'l', 'o', 1, 2, 3),
            (Http2DataFrame{1, false, SliceBufferFromString("hello")}));
  EXPECT_EQ(
      ParseFrame<Http2HeaderFrame>(0, 0, 8, 1, 8, 0, 0, 0, 1, 2, 'h', 'e', 'l',
                                   'l', 'o', 1, 2),
      (Http2HeaderFrame{1, false, false, SliceBufferFromString("hello")}));
  EXPECT_EQ(
      ParseFrame<Http2HeaderFrame>(0, 0, 10, 1, 32, 0, 0, 0, 1, 1, 2, 3, 4, 5,
                                   'h', 'e', 'l', 'l', 'o'),
      (Http2HeaderFrame{1, false, false, SliceBufferFromString("hello")}));
  EXPECT_EQ(
      ParseFrame<Http2HeaderFrame>(0, 0, 13, 1, 40, 0, 0, 0, 1, 2, 1, 2, 3, 4,
                                   5, 'h', 'e', 'l', 'l', 'o', 1, 2),
      (Http2HeaderFrame{1, false, false, SliceBufferFromString("hello")}));
}

TEST(Frame, ParseRejects) {
  EXPECT_THAT(
      ValidateFrame(0, 0, 0, 0, 2, 0, 0, 0, 1),
      StatusIs(
          absl::StatusCode::kInternal,
          "unsupported data flags: {DATA: flags=2, stream_id=1, length=0}"));
  EXPECT_THAT(
      ValidateFrame(0, 0, 0, 0, 4, 0, 0, 0, 10),
      StatusIs(
          absl::StatusCode::kInternal,
          "unsupported data flags: {DATA: flags=4, stream_id=10, length=0}"));
  EXPECT_THAT(
      ValidateFrame(0, 0, 0, 0, 16, 0, 0, 0, 1),
      StatusIs(
          absl::StatusCode::kInternal,
          "unsupported data flags: {DATA: flags=16, stream_id=1, length=0}"));
  EXPECT_THAT(
      ValidateFrame(0, 0, 0, 0, 32, 0, 0, 0, 1),
      StatusIs(
          absl::StatusCode::kInternal,
          "unsupported data flags: {DATA: flags=32, stream_id=1, length=0}"));
  EXPECT_THAT(
      ValidateFrame(0, 0, 0, 0, 64, 0, 0, 0, 1),
      StatusIs(
          absl::StatusCode::kInternal,
          "unsupported data flags: {DATA: flags=64, stream_id=1, length=0}"));
  EXPECT_THAT(
      ValidateFrame(0, 0, 0, 0, 128, 0, 0, 0, 1),
      StatusIs(
          absl::StatusCode::kInternal,
          "unsupported data flags: {DATA: flags=128, stream_id=1, length=0}"));
  EXPECT_THAT(
      ValidateFrame(0, 0, 0, 0, 255, 0, 0, 0, 1),
      StatusIs(
          absl::StatusCode::kInternal,
          "unsupported data flags: {DATA: flags=255, stream_id=1, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 0, 0, 0, 0, 0, 0),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid stream id: {DATA: flags=0, "
                       "stream_id=0, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 1, 2, 0, 0, 0, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "unsupported header flags: {HEADER: flags=2, "
                       "stream_id=1, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 1, 16, 0, 0, 0, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "unsupported header flags: {HEADER: flags=16, "
                       "stream_id=1, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 1, 64, 0, 0, 0, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "unsupported header flags: {HEADER: flags=64, "
                       "stream_id=1, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 1, 128, 0, 0, 0, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "unsupported header flags: {HEADER: flags=128, "
                       "stream_id=1, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 1, 255, 0, 0, 0, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "unsupported header flags: {HEADER: flags=255, "
                       "stream_id=1, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 1, 0, 0, 0, 0, 0),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid stream id: {HEADER: flags=0, "
                       "stream_id=0, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 9, 255, 0, 0, 0, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "unsupported header flags: {CONTINUATION: flags=255, "
                       "stream_id=1, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 9, 0, 0, 0, 0, 0),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid stream id: {CONTINUATION: flags=0, "
                       "stream_id=0, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 4, 3, 255, 0, 0, 0, 1, 100, 100, 100, 100),
              StatusIs(absl::StatusCode::kInternal,
                       "unsupported rst stream flags: {RST_STREAM: flags=255, "
                       "stream_id=1, length=4}"));
  EXPECT_THAT(ValidateFrame(0, 0, 4, 3, 2, 0, 0, 0, 1, 100, 100, 100, 100),
              StatusIs(absl::StatusCode::kInternal,
                       "unsupported rst stream flags: {RST_STREAM: flags=2, "
                       "stream_id=1, length=4}"));
  EXPECT_THAT(ValidateFrame(0, 0, 3, 3, 0, 0, 0, 0, 1, 100, 100, 100),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid rst stream payload: {RST_STREAM: flags=0, "
                       "stream_id=1, length=3}"));
  EXPECT_THAT(ValidateFrame(0, 0, 4, 3, 0, 0, 0, 0, 0, 100, 100, 100, 100),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid stream id: {RST_STREAM: flags=0, "
                       "stream_id=0, length=4}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 4, 244, 0, 0, 0, 0),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid settings flags: {SETTINGS: flags=244, "
                       "stream_id=0, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 1, 4, 0, 0, 0, 0, 0, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid settings payload: {SETTINGS: flags=0, "
                       "stream_id=0, length=1} -- settings must be multiples "
                       "of 6 bytes long"));
  EXPECT_THAT(ValidateFrame(0, 0, 2, 4, 0, 0, 0, 0, 0, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid settings payload: {SETTINGS: flags=0, "
                       "stream_id=0, length=2} -- settings must be multiples "
                       "of 6 bytes long"));
  EXPECT_THAT(ValidateFrame(0, 0, 3, 4, 0, 0, 0, 0, 0, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid settings payload: {SETTINGS: flags=0, "
                       "stream_id=0, length=3} -- settings must be multiples "
                       "of 6 bytes long"));
  EXPECT_THAT(ValidateFrame(0, 0, 4, 4, 0, 0, 0, 0, 0, 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid settings payload: {SETTINGS: flags=0, "
                       "stream_id=0, length=4} -- settings must be multiples "
                       "of 6 bytes long"));
  EXPECT_THAT(ValidateFrame(0, 0, 5, 4, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid settings payload: {SETTINGS: flags=0, "
                       "stream_id=0, length=5} -- settings must be multiples "
                       "of 6 bytes long"));
  EXPECT_THAT(ValidateFrame(0, 0, 7, 4, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid settings payload: {SETTINGS: flags=0, "
                       "stream_id=0, length=7} -- settings must be multiples "
                       "of 6 bytes long"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 4, 0, 0, 0, 0, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid stream id: {SETTINGS: flags=0, "
                       "stream_id=1, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 6, 0, 0, 0, 0, 0),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid ping payload: {PING: flags=0, "
                       "stream_id=0, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 8, 6, 0, 0, 0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid ping stream id: {PING: flags=0, "
                       "stream_id=1, length=8}"));
  EXPECT_THAT(ValidateFrame(0, 0, 8, 6, 2, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid ping flags: {PING: flags=2, "
                       "stream_id=0, length=8}"));
  EXPECT_THAT(
      ValidateFrame(0, 0, 8, 6, 255, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8),
      StatusIs(absl::StatusCode::kInternal,
               "invalid ping flags: {PING: flags=255, "
               "stream_id=0, length=8}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 7, 0, 0, 0, 0, 0),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid goaway payload: {GOAWAY: flags=0, "
                       "stream_id=0, length=0} -- must be at least 8 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 1, 7, 0, 0, 0, 0, 0, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid goaway payload: {GOAWAY: flags=0, "
                       "stream_id=0, length=1} -- must be at least 8 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 2, 7, 0, 0, 0, 0, 0, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid goaway payload: {GOAWAY: flags=0, "
                       "stream_id=0, length=2} -- must be at least 8 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 3, 7, 0, 0, 0, 0, 0, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid goaway payload: {GOAWAY: flags=0, "
                       "stream_id=0, length=3} -- must be at least 8 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 4, 7, 0, 0, 0, 0, 0, 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid goaway payload: {GOAWAY: flags=0, "
                       "stream_id=0, length=4} -- must be at least 8 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 5, 7, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid goaway payload: {GOAWAY: flags=0, "
                       "stream_id=0, length=5} -- must be at least 8 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 6, 7, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid goaway payload: {GOAWAY: flags=0, "
                       "stream_id=0, length=6} -- must be at least 8 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 7, 7, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid goaway payload: {GOAWAY: flags=0, "
                       "stream_id=0, length=7} -- must be at least 8 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 8, 7, 0, 0, 0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid goaway stream id: {GOAWAY: flags=0, "
                       "stream_id=1, length=8}"));
  EXPECT_THAT(ValidateFrame(0, 0, 8, 7, 1, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid goaway flags: {GOAWAY: flags=1, "
                       "stream_id=0, length=8}"));
  EXPECT_THAT(
      ValidateFrame(0, 0, 8, 7, 255, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8),
      StatusIs(absl::StatusCode::kInternal,
               "invalid goaway flags: {GOAWAY: flags=255, "
               "stream_id=0, length=8}"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 8, 0, 0, 0, 0, 0),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid window update payload: {WINDOW_UPDATE: "
                       "flags=0, stream_id=0, length=0} -- must be 4 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 1, 8, 0, 0, 0, 0, 0, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid window update payload: {WINDOW_UPDATE: "
                       "flags=0, stream_id=0, length=1} -- must be 4 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 2, 8, 0, 0, 0, 0, 0, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid window update payload: {WINDOW_UPDATE: "
                       "flags=0, stream_id=0, length=2} -- must be 4 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 3, 8, 0, 0, 0, 0, 0, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid window update payload: {WINDOW_UPDATE: "
                       "flags=0, stream_id=0, length=3} -- must be 4 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 5, 8, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid window update payload: {WINDOW_UPDATE: "
                       "flags=0, stream_id=0, length=5} -- must be 4 bytes"));
  EXPECT_THAT(ValidateFrame(0, 0, 4, 8, 1, 0, 0, 0, 0, 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid window update flags: {WINDOW_UPDATE: flags=1, "
                       "stream_id=0, length=4}"));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
