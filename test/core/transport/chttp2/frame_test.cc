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

#include <algorithm>
#include <initializer_list>
#include <utility>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/slice/slice_buffer.h"

using grpc_core::http2::Http2ErrorCode;

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

template <typename... I>
Http2Frame ParseFrame(I... i) {
  SliceBuffer buffer;
  buffer.Append(Slice::FromCopiedBuffer(ByteVec(i...)));
  uint8_t hdr[9];
  buffer.MoveFirstNBytesIntoBuffer(9, hdr);
  auto frame_hdr = Http2FrameHeader::Parse(hdr);
  EXPECT_EQ(frame_hdr.length, buffer.Length())
      << "frame_hdr=" << frame_hdr.ToString();
  auto r = ParseFramePayload(frame_hdr, std::move(buffer));
  EXPECT_TRUE(r.ok()) << r.status();
  return std::move(r.value());
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

TEST(Frame, Http2DataFrameSerialization) {
  EXPECT_EQ(Serialize(Http2DataFrame{1, false, SliceBufferFromString("hello")}),
            ByteVec(0, 0, 5, 0, 0, 0, 0, 0, 1, 'h', 'e', 'l', 'l', 'o'));
  EXPECT_EQ(Serialize(Http2DataFrame{0x78381821, true,
                                     SliceBufferFromString("kids")}),
            ByteVec(0, 0, 4, 0, 1, 0x78, 0x38, 0x18, 0x21, 'k', 'i', 'd', 's'));
}

TEST(Frame, Http2HeaderFrameSerialization) {
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
}

TEST(Frame, Http2ContinuationFrameSerialization) {
  EXPECT_EQ(Serialize(Http2ContinuationFrame{1, false,
                                             SliceBufferFromString("hello")}),
            ByteVec(0, 0, 5, 9, 0, 0, 0, 0, 1, 'h', 'e', 'l', 'l', 'o'));
  EXPECT_EQ(Serialize(Http2ContinuationFrame{1, true,
                                             SliceBufferFromString("hello")}),
            ByteVec(0, 0, 5, 9, 4, 0, 0, 0, 1, 'h', 'e', 'l', 'l', 'o'));
}

TEST(Frame, Http2RstStreamFrameSerialization) {
  EXPECT_EQ(Serialize(Http2RstStreamFrame{
                1, static_cast<uint32_t>(Http2ErrorCode::kConnectError)}),
            ByteVec(0, 0, 4, 3, 0, 0, 0, 0, 1, 0, 0, 0, 0x0a));
}

TEST(Frame, Http2SettingsFrameSerialization) {
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
}

TEST(Frame, Http2PingFrameSerialization) {
  EXPECT_EQ(Serialize(Http2PingFrame{false, 0x123456789abcdef0}),
            ByteVec(0, 0, 8, 6, 0, 0, 0, 0, 0, 0x12, 0x34, 0x56, 0x78, 0x9a,
                    0xbc, 0xde, 0xf0));
  EXPECT_EQ(Serialize(Http2PingFrame{true, 0x123456789abcdef0}),
            ByteVec(0, 0, 8, 6, 1, 0, 0, 0, 0, 0x12, 0x34, 0x56, 0x78, 0x9a,
                    0xbc, 0xde, 0xf0));
}

TEST(Frame, Http2GoawayFrameSerialization) {
  EXPECT_EQ(
      Serialize(Http2GoawayFrame{
          0x12345678, static_cast<uint32_t>(Http2ErrorCode::kEnhanceYourCalm),
          Slice::FromCopiedString("hello")}),
      ByteVec(/* Length (24) */ 0, 0, 13,
              /* Type (8) */ 7,
              /* Unused Flags (8) */ 0,
              /* Reserved (1), Stream Identifier (31) */ 0, 0, 0, 0,
              /* Reserved (1), Last-Stream-ID (31) */ 0x12, 0x34, 0x56, 0x78,
              /* Error Code (32) */ 0, 0, 0, 0x0b,
              /* Additional Debug Data (..) */ 'h', 'e', 'l', 'l', 'o'));

  // Check if Reserved Bit is ignored
  EXPECT_EQ(
      Serialize(Http2GoawayFrame{0xffffffff, static_cast<uint32_t>(0xffffffff),
                                 Slice::FromCopiedString("hello")}),
      ByteVec(/* Length (24) */ 0, 0, 13,
              /* Type (8) */ 7,
              /* Unused Flags (8) */ 0,
              /* Reserved (1), Stream Identifier (31) */ 0, 0, 0, 0,
              /* Reserved (1), Last-Stream-ID (31) */ 0x7f, 0xff, 0xff, 0xff,
              /* Error Code (32) */ 0xff, 0xff, 0xff, 0xff,
              /* Additional Debug Data (..) */ 'h', 'e', 'l', 'l', 'o'));
}

TEST(Frame, Http2WindowUpdateFrameSerialization) {
  EXPECT_EQ(Serialize(Http2WindowUpdateFrame{1, 0x12345678}),
            ByteVec(/* Length (24) */ 0, 0, 4,
                    /* Type (8) */ 8,
                    /* Unused Flags (8) */ 0,
                    /* Reserved (1), Stream Identifier (31) */ 0, 0, 0, 1,
                    /* Reserved (1), Window Size Increment (31) */ 0x12, 0x34,
                    0x56, 0x78));

  // Check if Reserved Bit is ignored
  EXPECT_EQ(Serialize(Http2WindowUpdateFrame{1, 0xffffffff}),
            ByteVec(/* Length (24) */ 0, 0, 4,
                    /* Type (8) */ 8,
                    /* Unused Flags (8) */ 0,
                    /* Reserved (1), Stream Identifier (31) */ 0, 0, 0, 1,
                    /* Reserved (1), Window Size Increment (31) */ 0x7f, 0xff,
                    0xff, 0xff));
}

TEST(Frame, Http2SecurityFrameSerialization) {
  EXPECT_EQ(Serialize(Http2SecurityFrame{SliceBufferFromString("hello")}),
            ByteVec(0, 0, 5, 200, 0, 0, 0, 0, 0, 'h', 'e', 'l', 'l', 'o'));
}

constexpr uint8_t kFlagPadded = 8;
constexpr uint8_t kFlagPriority = 0x20;
constexpr uint8_t kAllOnes = 0xffu;

TEST(Frame, ParseHttp2DataFrame) {
  EXPECT_EQ(
      ParseFrame(/* Length (3 octets) */ 0, 0, 5,
                 /* Type (1 octet) */ 0,
                 /* Unused Flags (4), PADDED Flag (1), Unused Flags (2),
                    END_STREAM Flag (1)*/
                 0,
                 /* Reserved (1), Stream Identifier (31 bits) */ 0, 0, 0, 1,
                 /* Data */ 'h', 'e', 'l', 'l', 'o'),
      Http2Frame(Http2DataFrame{1, false, SliceBufferFromString("hello")}));
  EXPECT_EQ(ParseFrame(/* Length (3 octets) = */ 0, 0, 4,
                       /* Type (1 octet) */ 0,
                       /* Unused Flags (4), PADDED Flag (1), Unused Flags (2),
                          END_STREAM Flag (1)*/
                       1,
                       /* Reserved (1), Stream Identifier (31 bits) */ 0x18,
                       0x38, 0x18, 0x21,
                       /* Data */ 'k', 'i', 'd', 's'),
            Http2Frame(Http2DataFrame{0x18381821, true,
                                      SliceBufferFromString("kids")}));
  // Check if Reserved Bit is ignored and unused flags are ignored
  EXPECT_EQ(ParseFrame(/* Length (3 octets) = */ 0, 0, 4,
                       /* Type (1 octet) */ 0,
                       /* Unused Flags (4), PADDED Flag (1), Unused Flags (2),
                          END_STREAM Flag (1)*/
                       (kAllOnes - kFlagPadded),
                       /* Reserved (1), Stream Identifier (31 bits) */
                       /* Should get truncated to 0x7f*/ 0xff, 0x38, 0x18, 0x21,
                       /* Data */ 'k', 'i', 'd', 's'),
            Http2Frame(Http2DataFrame{0x7f381821, true,
                                      SliceBufferFromString("kids")}));
}

TEST(Frame, ParseHttp2HeaderFrame) {
  EXPECT_EQ(
      ParseFrame(
          /* Length (3 octets) */ 0, 0, 5,
          /* Type (1 octet) */ 1,
          /* Unused Flags (2),PRIORITY Flag (1),Unused Flag (1),PADDED Flag
             (1), END_HEADERS Flag (1),Unused Flag (1),END_STREAM Flag (1)*/
          0,
          /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
          /* Field Block Fragment */ 'h', 'e', 'l', 'l', 'o'),
      Http2Frame(Http2HeaderFrame{1, /*end_headers=*/false,
                                  /*end_stream=*/false,
                                  SliceBufferFromString("hello")}));
  EXPECT_EQ(
      ParseFrame(
          /* Length (3 octets) = */ 0, 0, 4,
          /* Type (1 octet) */ 1,
          /* Unused Flags (2),PRIORITY Flag (1),Unused Flag (1),PADDED Flag
             (1),END_HEADERS Flag (1),Unused Flag (1),END_STREAM Flag (1)*/
          4,
          /* Stream Identifier (31 bits) */ 0x78, 0x38, 0x18, 0x21,
          /* Field Block Fragment */ 'k', 'i', 'd', 's'),
      Http2Frame(Http2HeaderFrame{0x78381821, /*end_headers=*/true,
                                  /*end_stream=*/false,
                                  SliceBufferFromString("kids")}));
  EXPECT_EQ(
      ParseFrame(
          /* Length (3 octets) = */ 0, 0, 4,
          /* Type (1 octet) */ 1,
          /* Unused Flags (2),PRIORITY Flag (1),Unused Flag (1),PADDED Flag
             (1),END_HEADERS Flag (1),Unused Flag (1),END_STREAM Flag (1) */
          1,
          /* Stream Identifier (31 bits) */ 0x78, 0x38, 0x18, 0x21,
          /* Field Block Fragment */ 'k', 'i', 'd', 's'),
      Http2Frame(Http2HeaderFrame{0x78381821, /*end_headers=*/false,
                                  /*end_stream=*/true,
                                  SliceBufferFromString("kids")}));
  EXPECT_EQ(
      ParseFrame(
          /* Length (3 octets) = */ 0, 0, 4,
          /* Type (1 octet) */ 1,
          /* Unused Flags (2),PRIORITY Flag (1),Unused Flag (1),PADDED Flag
             (1),END_HEADERS Flag (1),Unused Flag (1),END_STREAM Flag (1)*/
          5,
          /* Stream Identifier (31 bits) */ 0x78, 0x38, 0x18, 0x21,
          /* Field Block Fragment */ 'k', 'i', 'd', 's'),
      Http2Frame(Http2HeaderFrame{0x78381821, /*end_headers=*/true,
                                  /*end_stream=*/true,
                                  SliceBufferFromString("kids")}));

  // Check if Reserved Bit is ignored and unused flags are ignored
  EXPECT_EQ(
      ParseFrame(
          /* Length (3 octets) */ 0, 0, 5,
          /* Type (1 octet) */ 1,
          /* Unused Flags (2),PRIORITY Flag (1),Unused Flag (1),PADDED Flag
             (1),END_HEADERS Flag (1),Unused Flag (1),END_STREAM Flag (1)*/
          (kAllOnes - kFlagPriority - kFlagPadded),
          /* Reserved (1), Stream Identifier (31 bits) */ 0xff, 0xff, 0xff,
          0xff,
          /* Field Block Fragment */ 'h', 'e', 'l', 'l', 'o'),
      Http2Frame(Http2HeaderFrame{0x7fffffff, /*end_headers=*/true,
                                  /*end_stream=*/true,
                                  SliceBufferFromString("hello")}));
}

TEST(Frame, ParseHttp2ContinuationFrame) {
  EXPECT_EQ(
      ParseFrame(
          /* Length (3 octets) */ 0, 0, 5,
          /* Type (1 octet) */ 9,
          /* Unused Flags (5), END_HEADERS Flag (1), Unused Flags (2) */ 0,
          /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
          /* Field Block Fragment */ 'h', 'e', 'l', 'l', 'o'),
      Http2Frame(Http2ContinuationFrame{1, /*end_stream=*/false,
                                        SliceBufferFromString("hello")}));
  EXPECT_EQ(
      ParseFrame(
          /* Length (3 octets) */ 0, 0, 5,
          /* Type (1 octet) */ 9,
          /* Unused Flags (5), END_HEADERS Flag (1), Unused Flags (2) */ 4,
          /* Stream Identifier (31 bits) */ 0x78, 0x38, 0x18, 0x21,
          /* Field Block Fragment */ 'h', 'e', 'l', 'l', 'o'),
      Http2Frame(Http2ContinuationFrame{0x78381821, /*end_stream=*/true,
                                        SliceBufferFromString("hello")}));

  // Check if Reserved Bit is ignored and unused flags are ignored
  EXPECT_EQ(
      ParseFrame(
          /* Length (3 octets) */ 0, 0, 5,
          /* Type (1 octet) */ 9,
          /* Unused Flags (5), END_HEADERS Flag (1), Unused Flags (2) */ 0xff,
          /* Stream Identifier (31 bits) */ 0xff, 0xff, 0xff, 0xff,
          /* Field Block Fragment */ 'h', 'e', 'l', 'l', 'o'),
      Http2Frame(Http2ContinuationFrame{0x7fffffff, /*end_stream=*/true,
                                        SliceBufferFromString("hello")}));
}

TEST(Frame, ParseHttp2RstStreamFrame) {
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 4,
                       /* Type (1 octet) */ 3,
                       /* Unused Flags (1 octet) */ 0,
                       /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
                       /* Error Code (4 octets) */ 0, 0, 0, 0x0a),
            Http2Frame(Http2RstStreamFrame{
                1, static_cast<uint32_t>(Http2ErrorCode::kConnectError)}));

  // Check if Reserved Bit is ignored and unused flags are ignored
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 4,
                       /* Type (1 octet) */ 3,
                       /* Unused Flags (1 octet) */ 0xff,
                       /* Stream Identifier (31 bits) */ 0xff, 0xff, 0xff, 0xff,
                       /* Error Code (4 octets) */ 0xff, 0xff, 0xff, 0xff),
            Http2Frame(Http2RstStreamFrame{0x7fffffff, 0xffffffff}));
}

TEST(Frame, ParseHttp2SettingsFrame) {
  // This is a weird frame. Empty settings and no ack.
  // But RFC9113 does not say anything so we allow it.
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 0,
                       /* Type (1 octet) */ 4,
                       /* Unused Flags (7), ACK Flag (1) */ 0,
                       /* Stream Identifier (31 bits) */ 0, 0, 0, 0),
            Http2Frame(Http2SettingsFrame{}));

  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 6,
                       /* Type (1 octet) */ 4,
                       /* Unused Flags (7), ACK Flag (1) */ 0,
                       /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                       /* Setting (6 octets each) */ 0x12, 0x34, 0x9a, 0xbc,
                       0xde, 0xf0),
            Http2Frame(Http2SettingsFrame{false, {{0x1234, 0x9abcdef0}}}));
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 12,
                       /* Type (1 octet) */ 4,
                       /* Unused Flags (7), ACK Flag (1) */ 0,
                       /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                       /* Setting (6 octets each) */ 0x12, 0x34, 0x9a, 0xbc,
                       0xde, 0xf0,
                       /* Setting (6 octets each) */ 0x43, 0x21, 0x12, 0x34,
                       0x56, 0x78),
            Http2Frame(Http2SettingsFrame{
                false, {{0x1234, 0x9abcdef0}, {0x4321, 0x12345678}}}));
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 0,
                       /* Type (1 octet) */ 4,
                       /* Unused Flags (7), ACK Flag (1) */ 1,
                       /* Stream Identifier (31 bits) */ 0, 0, 0, 0),
            Http2Frame(Http2SettingsFrame{true, {}}));

  // Check if Reserved Bit is ignored and unused flags are ignored
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 0,
                       /* Type (1 octet) */ 4,
                       /* Unused Flags (7), ACK Flag (1) */ 0xff,
                       /* Stream Identifier (31 bits) */ 0x80, 0, 0, 0),
            Http2Frame(Http2SettingsFrame{true}));
}

TEST(Frame, ParseHttp2PingFrame) {
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 8,
                       /* Type (1 octet) */ 6,
                       /* Unused Flags (7), ACK Flag (1) */ 0,
                       /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                       /* Opaque Data (8 octets) */ 0x12, 0x34, 0x56, 0x78,
                       0x9a, 0xbc, 0xde, 0xf0),
            Http2Frame(Http2PingFrame{false, 0x123456789abcdef0}));
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 8,
                       /* Type (1 octet) */ 6,
                       /* Unused Flags (7), ACK Flag (1) */ 1,
                       /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                       /* Opaque Data (8 octets) */ 0x12, 0x34, 0x56, 0x78,
                       0x9a, 0xbc, 0xde, 0xf0),
            Http2Frame(Http2PingFrame{true, 0x123456789abcdef0}));

  // Check if Reserved Bit is ignored and unused flags are ignored
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 8,
                       /* Type (1 octet) */ 6,
                       /* Unused Flags (7), ACK Flag (1) */ 0xff,
                       /* Stream Identifier (31 bits) */ 0x80, 0, 0, 0,
                       /* Opaque Data (8 octets) */ 0xff, 0xff, 0xff, 0xff,
                       0xff, 0xff, 0xff, 0xff),
            Http2Frame(Http2PingFrame{true, 0xffffffffffffffff}));
}

TEST(Frame, ParseHttp2GoawayFrame) {
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 8,
                       /* Type (1 octet) */ 7,
                       /* Unused Flags (1 octet) */ 0,
                       /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                       /* Reserved (1 bit) Last-Stream-ID (31 bits) */
                       0x12, 0x34, 0x56, 0x78,
                       /* Error Code (4 octets) */ 0, 0, 0, 0x0b
                       /* Additional Debug Data */),
            Http2Frame(Http2GoawayFrame{
                0x12345678,
                static_cast<uint32_t>(Http2ErrorCode::kEnhanceYourCalm)}));
  EXPECT_EQ(
      ParseFrame(/* Length (3 octets) */ 0, 0, 13,
                 /* Type (1 octet) */ 7,
                 /* Unused Flags (1 octet) */ 0,
                 /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                 /* Reserved (1 bit) Last-Stream-ID (31 bits) */
                 0x12, 0x34, 0x56, 0x78,
                 /* Error Code (4 octets) */ 0, 0, 0, 0x0b,
                 /* Additional Debug Data */ 'h', 'e', 'l', 'l', 'o'),
      Http2Frame(Http2GoawayFrame{
          0x12345678, static_cast<uint32_t>(Http2ErrorCode::kEnhanceYourCalm),
          Slice::FromCopiedString("hello")}));

  // Check if Reserved Bit is ignored and unused flags are ignored
  EXPECT_EQ(
      ParseFrame(/* Length (3 octets) */ 0, 0, 13,
                 /* Type (1 octet) */ 7,
                 /* Unused Flags (1 octet) */ 0xff,
                 /* Stream Identifier (31 bits) */ 0x80, 0, 0, 0,
                 /* Reserved (1 bit) Last-Stream-ID (31 bits) */
                 0xff, 0xff, 0xff, 0xff,
                 /* Error Code (4 octets) */ 0, 0, 0, 0x0b,
                 /* Additional Debug Data */ 'h', 'e', 'l', 'l', 'o'),
      Http2Frame(Http2GoawayFrame{
          0x7fffffff, static_cast<uint32_t>(Http2ErrorCode::kEnhanceYourCalm),
          Slice::FromCopiedString("hello")}));
}

TEST(Frame, ParseHttp2WindowUpdateFrame) {
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 4,
                       /* Type (1 octet) */ 8,
                       /* Unused Flags (1 octet) */ 0,
                       /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
                       /* Reserved (1 bit) Window Size Increment (31 bits) */
                       0x12, 0x34, 0x56, 0x78),
            Http2Frame(Http2WindowUpdateFrame{1, 0x12345678}));

  // Check if Reserved Bit is ignored and unused flags are ignored
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 4,
                       /* Type (1 octet) */ 8,
                       /* Unused Flags (1 octet) */ 0xff,
                       /* Stream Identifier (31 bits) */ 0xff, 0xff, 0xff, 0xff,
                       /* Reserved (1 bit) Window Size Increment (31 bits) */
                       0xff, 0xff, 0xff, 0xff),
            Http2Frame(Http2WindowUpdateFrame{0x7fffffff, 0x7fffffff}));
}

TEST(Frame, ParseHttp2SecurityFrame) {
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 5,
                       /* Type (1 octet) */ 200,
                       /* Unused Flags (1 octet) */ 0,
                       /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                       /* Payload */ 'h', 'e', 'l', 'l', 'o'),
            Http2Frame(Http2SecurityFrame{SliceBufferFromString("hello")}));
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 5,
                       /* Type (1 octet) */ 200,
                       /* Unused Flags (1 octet) */ 0xff,
                       /* Stream Identifier (31 bits) */ 0xff, 0xff, 0xff, 0xff,
                       /* Payload */ 'h', 'e', 'l', 'l', 'o'),
            Http2Frame(Http2SecurityFrame{SliceBufferFromString("hello")}));
}

TEST(Frame, ParseHttp2DataFramePadded) {
  EXPECT_EQ(
      ParseFrame(/* Length (3 octets) */ 0, 0, 9,
                 /* Type (1 octet) */ 0,
                 /* Unused Flags (4), PADDED Flag (1), Unused Flags (2),
                    END_STREAM Flag (1)*/
                 8,
                 /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
                 /* Pad Length */ 3,
                 /* Data */ 'h', 'e', 'l', 'l', 'o',
                 /* Padding */ 0, 0, 0),
      Http2Frame(Http2DataFrame{1, false, SliceBufferFromString("hello")}));
}

TEST(Frame, ParseHttp2HeaderFramePadded) {
  // TODO(tjagtap) : [PH2][P4] : Look at permutations of flags and types of
  // frames we can have. HEADERS has multiple permutations that have not been
  // tested.
  EXPECT_EQ(
      ParseFrame(
          /* Length (3 octets) */ 0, 0, 8,
          /* Type (1 octet) */ 1,
          /* Unused Flags (2),PRIORITY Flag (1),Unused Flag (1),PADDED Flag
             (1),END_HEADERS Flag (1),Unused Flag (1),END_STREAM Flag (1)*/
          8,
          /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
          /* Pad Length (1 octet) */ 2,
          /* Field Block Fragment */ 'h', 'e', 'l', 'l', 'o',
          /* Padding*/ 0, 0),
      Http2Frame(
          Http2HeaderFrame{1, false, false, SliceBufferFromString("hello")}));
  EXPECT_EQ(
      ParseFrame(
          /* Length (3 octets) */ 0, 0, 10,
          /* Type (1 octet) */ 1,
          /* Unused Flags (2),PRIORITY Flag (1),Unused Flag (1),PADDED Flag
             (1),END_HEADERS Flag (1),Unused Flag (1),END_STREAM Flag (1)*/
          32,
          /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
          /* Not sure what this is but we discard */ 1, 2, 3, 4, 5,
          /* Field Block Fragment */ 'h', 'e', 'l', 'l', 'o'),
      Http2Frame(
          Http2HeaderFrame{1, false, false, SliceBufferFromString("hello")}));
  EXPECT_EQ(
      ParseFrame(
          /* Length (3 octets) */ 0, 0, 13,
          /* Type (1 octet) */ 1,
          /* Unused Flags (2),PRIORITY Flag (1),Unused Flag (1),PADDED Flag
             (1),END_HEADERS Flag (1),Unused Flag (1),END_STREAM Flag (1)*/
          40,
          /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
          /* Not sure what this is but we discard */ 2, 1, 2, 3, 4, 5,
          /* Field Block Fragment */ 'h', 'e', 'l', 'l', 'o',
          /* Padding*/ 1, 2),
      Http2Frame(
          Http2HeaderFrame{1, false, false, SliceBufferFromString("hello")}));
}

TEST(Frame, UnknownIgnored) {
  // 77 = some random undefined frame
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 10,
                       /* Type (1 octet) */ 77,
                       /* Unused Flags (1 octet) */ 0,
                       /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
                       /* */ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10),
            Http2Frame(Http2UnknownFrame{}));

  // 2 = PRIORITY, we just ignore it
  EXPECT_EQ(ParseFrame(/* Length (3 octets) */ 0, 0, 10,
                       /* Type (1 octet) */ 2,
                       /* Unused Flags (1 octet) */ 0xff,
                       /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
                       /* */ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10),
            Http2Frame(Http2UnknownFrame{}));
}

TEST(Frame, ParseRejectsPushPromise) {
  EXPECT_THAT(ValidateFrame(/* Length (3 octets) */ 0, 0, 10,
                            /* Type (1 octet) */ 5,
                            /* Unused Flags (1 octet) */ 0,
                            /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
                            /* */ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10),
              StatusIs(absl::StatusCode::kInternal,
                       absl::StrCat(
                           RFC9113::kNoPushPromise,
                           "{PUSH_PROMISE: flags=0, stream_id=1, length=10}")));
}

TEST(Frame, ParseRejectsDataFrame) {
  EXPECT_THAT(ValidateFrame(/* Length (3 octets) */ 0, 0, 0,
                            /* Type (1 octet) */ 0,
                            /* Unused Flags (4), PADDED Flag (1), Unused Flags
   (2), END_STREAM Flag (1)*/
                            0,
                            /* Stream Identifier (31 bits) */ 0, 0, 0, 0),
              StatusIs(absl::StatusCode::kInternal, /*Connection Error*/
                       absl::StrCat(RFC9113::kDataStreamIdMustBeNonZero,
                                    "{DATA: flags=0, stream_id=0, length=0}")));
  EXPECT_THAT(ValidateFrame(/* Length (3 octets) */ 0, 0, 0,
                            /* Type (1 octet) */ 0,
                            /* Unused Flags (4), PADDED Flag (1), Unused Flags
   (2), END_STREAM Flag (1)*/
                            0,
                            /* Stream Identifier (31 bits) */ 0, 0, 0, 2),
              StatusIs(absl::StatusCode::kInternal, /*Connection Error*/
                       absl::StrCat(RFC9113::kStreamIdMustBeOdd,
                                    "{DATA: flags=0, stream_id=2, length=0}")));

  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 9,
                    /* Type (1 octet) */ 0,
                    /* Unused Flags (4), PADDED Flag (1), Unused Flags
                       (2), END_STREAM Flag (1)*/
                    9,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
                    /* Pad Length */ 0xff,
                    /* Data */ 'h', 'e', 'l', 'l', 'o',
                    /* Padding */ 0, 0, 0),
      StatusIs(absl::StatusCode::kInternal, /*Connection Error*/
               absl::StrCat(RFC9113::kPaddingLengthLargerThanFrameLength,
                            "{DATA: flags=9, stream_id=1, length=9}")));
}

TEST(Frame, ParseRejectsHeaderFrame) {
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 0,
                    /* Type (1 octet) */ 1,
                    /* Unused Flags (2),PRIORITY Flag (1),Unused Flag (1),PADDED
             Flag (1),END_HEADERS Flag (1),Unused Flag (1),END_STREAM Flag (1)*/
                    0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kHeaderStreamIdMustBeNonZero,
                            "{HEADER: flags=0, stream_id=0, length=0}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 0,
                    /* Type (1 octet) */ 1,
                    /* Unused Flags (2),PRIORITY Flag (1),Unused Flag (1),PADDED
             Flag (1),END_HEADERS Flag (1),Unused Flag (1),END_STREAM Flag (1)*/
                    0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 2),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kStreamIdMustBeOdd,
                            "{HEADER: flags=0, stream_id=2, length=0}")));

  // TODO(tjagtap) : [PH2][P5] : Add more test cases. The parser has a lot of
  // cases.
}

TEST(Frame, ParseRejectsContinuationFrame) {
  EXPECT_THAT(
      ValidateFrame(
          /* Length (3 octets) */ 0, 0, 0,
          /* Type (1 octet) */ 9,
          /* Unused Flags (5), END_HEADERS Flag (1), Unused Flags (2) */ 0,
          /* Stream Identifier (31 bits) */ 0, 0, 0, 0),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kContinuationStreamIdMustBeNonZero,
                            "{CONTINUATION: flags=0, stream_id=0, length=0}")));
  EXPECT_THAT(
      ValidateFrame(
          /* Length (3 octets) */ 0, 0, 0,
          /* Type (1 octet) */ 9,
          /* Unused Flags (5), END_HEADERS Flag (1), Unused Flags (2) */ 0,
          /* Stream Identifier (31 bits) */ 0, 0, 0, 2),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kStreamIdMustBeOdd,
                            "{CONTINUATION: flags=0, stream_id=2, length=0}")));
}

TEST(Frame, ParseRejectsRstStreamFrame) {
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 3,
                    /* Type (1 octet) */ 3,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
                    /* */ 100, 100, 100),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kRstStreamLength4,
                            "{RST_STREAM: flags=0, stream_id=1, length=3}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 4,
                    /* Type (1 octet) */ 3,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* */ 100, 100, 100, 100),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kRstStreamStreamIdMustBeNonZero,
                            "{RST_STREAM: flags=0, stream_id=0, length=4}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 4,
                    /* Type (1 octet) */ 3,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 2,
                    /* */ 100, 100, 100, 100),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kStreamIdMustBeOdd,
                            "{RST_STREAM: flags=0, stream_id=2, length=4}")));
}

TEST(Frame, ParseRejectsSettingsFrame) {
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 1,
                    /* Type (1 octet) */ 4,
                    /* Unused Flags (7), ACK Flag (1) */ 1,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* */ 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kSettingsLength0,
                            "{SETTINGS: flags=1, stream_id=0, length=1}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 1,
                    /* Type (1 octet) */ 4,
                    /* Unused Flags (7), ACK Flag (1) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* */ 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kSettingsLength6x,
                            "{SETTINGS: flags=0, stream_id=0, length=1}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 2,
                    /* Type (1 octet) */ 4,
                    /* Unused Flags (7), ACK Flag (1) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* Setting */ 1, 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kSettingsLength6x,
                            "{SETTINGS: flags=0, stream_id=0, length=2}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 3,
                    /* Type (1 octet) */ 4,
                    /* Unused Flags (7), ACK Flag (1) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* Setting */ 1, 1, 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kSettingsLength6x,
                            "{SETTINGS: flags=0, stream_id=0, length=3}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 4,
                    /* Type (1 octet) */ 4,
                    /* Unused Flags (7), ACK Flag (1) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* Setting */ 1, 1, 1, 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kSettingsLength6x,
                            "{SETTINGS: flags=0, stream_id=0, length=4}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 5,
                    /* Type (1 octet) */ 4,
                    /* Unused Flags (7), ACK Flag (1) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* Setting */ 1, 1, 1, 1, 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kSettingsLength6x,
                            "{SETTINGS: flags=0, stream_id=0, length=5}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 7,
                    /* Type (1 octet) */ 4,
                    /* Unused Flags (7), ACK Flag (1) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* Setting */ 1, 1, 1, 1, 1, 1, 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kSettingsLength6x,
                            "{SETTINGS: flags=0, stream_id=0, length=7}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 0,
                    /* Type (1 octet) */ 4,
                    /* Unused Flags (7), ACK Flag (1) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kSettingsStreamIdMustBeZero,
                            "{SETTINGS: flags=0, stream_id=1, length=0}")));
}

TEST(Frame, ParseRejectsPingFrame) {
  EXPECT_THAT(ValidateFrame(/* Length (3 octets) */ 0, 0, 0,
                            /* Type (1 octet) */ 6,
                            /* Unused Flags (7), ACK Flag (1) */ 0,
                            /* Stream Identifier (31 bits) */ 0, 0, 0, 0),
              StatusIs(absl::StatusCode::kInternal,
                       absl::StrCat(RFC9113::kPingLength8,
                                    "{PING: flags=0, stream_id=0, length=0}")));
  EXPECT_THAT(ValidateFrame(/* Length (3 octets) */ 0, 0, 8,
                            /* Type (1 octet) */ 6,
                            /* Unused Flags (7), ACK Flag (1) */ 0,
                            /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
                            /* */ 1, 2, 3, 4, 5, 6, 7, 8),
              StatusIs(absl::StatusCode::kInternal,
                       absl::StrCat(RFC9113::kPingStreamIdMustBeZero,
                                    "{PING: flags=0, stream_id=1, length=8}")));
}

TEST(Frame, ParseRejectsGoawayFrame) {
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 0,
                    /* Type (1 octet) */ 7,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kGoAwayLength8,
                            "{GOAWAY: flags=0, stream_id=0, length=0}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 1,
                    /* Type (1 octet) */ 7,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* */ 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kGoAwayLength8,
                            "{GOAWAY: flags=0, stream_id=0, length=1}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 2,
                    /* Type (1 octet) */ 7,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* */ 1, 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kGoAwayLength8,
                            "{GOAWAY: flags=0, stream_id=0, length=2}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 3,
                    /* Type (1 octet) */ 7,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* */ 1, 1, 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kGoAwayLength8,
                            "{GOAWAY: flags=0, stream_id=0, length=3}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 4,
                    /* Type (1 octet) */ 7,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* */ 1, 1, 1, 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kGoAwayLength8,
                            "{GOAWAY: flags=0, stream_id=0, length=4}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 5,
                    /* Type (1 octet) */ 7,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* */ 1, 1, 1, 1, 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kGoAwayLength8,
                            "{GOAWAY: flags=0, stream_id=0, length=5}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 6,
                    /* Type (1 octet) */ 7,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* */ 1, 1, 1, 1, 1, 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kGoAwayLength8,
                            "{GOAWAY: flags=0, stream_id=0, length=6}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 7,
                    /* Type (1 octet) */ 7,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* */ 1, 1, 1, 1, 1, 1, 1),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kGoAwayLength8,
                            "{GOAWAY: flags=0, stream_id=0, length=7}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 8,
                    /* Type (1 octet) */ 7,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 1,
                    /* */ 1, 2, 3, 4, 5, 6, 7, 8),
      StatusIs(absl::StatusCode::kInternal,
               absl::StrCat(RFC9113::kGoAwayStreamIdMustBeZero,
                            "{GOAWAY: flags=0, stream_id=1, length=8}")));
}

TEST(Frame, ParseRejectsWindowUpdateFrame) {
  EXPECT_THAT(ValidateFrame(/* Length (3 octets) */ 0, 0, 0,
                            /* Type (1 octet) */ 8,
                            /* Unused Flags (1 octet) */ 0,
                            /* Stream Identifier (31 bits) */ 0, 0, 0, 0),
              StatusIs(absl::StatusCode::kInternal,
                       absl::StrCat(
                           RFC9113::kWindowUpdateLength4,
                           "{WINDOW_UPDATE: flags=0, stream_id=0, length=0}")));
  EXPECT_THAT(ValidateFrame(/* Length (3 octets) */ 0, 0, 1,
                            /* Type (1 octet) */ 8,
                            /* Unused Flags (1 octet) */ 0,
                            /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                            /* Window Size Increment (31 bits) */ 1),
              StatusIs(absl::StatusCode::kInternal,
                       absl::StrCat(
                           RFC9113::kWindowUpdateLength4,
                           "{WINDOW_UPDATE: flags=0, stream_id=0, length=1}")));
  EXPECT_THAT(ValidateFrame(
                  /* Length (3 octets) */ 0, 0, 2,
                  /* Type (1 octet) */ 8,
                  /* Unused Flags (1 octet) */ 0,
                  /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                  /* Window Size Increment (31 bits) */ 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       absl::StrCat(
                           RFC9113::kWindowUpdateLength4,
                           "{WINDOW_UPDATE: flags=0, stream_id=0, length=2}")));
  EXPECT_THAT(ValidateFrame(/* Length (3 octets) */ 0, 0, 3,
                            /* Type (1 octet) */ 8,
                            /* Unused Flags (1 octet) */ 0,
                            /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                            /* Window Size Increment (31 bits) */ 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       absl::StrCat(
                           RFC9113::kWindowUpdateLength4,
                           "{WINDOW_UPDATE: flags=0, stream_id=0, length=3}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 5,
                    /* Type (1 octet) */ 8,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* Window Size Increment (31 bits) */ 1, 1, 1, 1, 1),
      StatusIs(
          absl::StatusCode::kInternal,
          absl::StrCat(RFC9113::kWindowUpdateLength4,
                       "{WINDOW_UPDATE: flags=0, stream_id=0, length=5}")));
  EXPECT_THAT(ValidateFrame(/* Length (3 octets) */ 0, 0, 4,
                            /* Type (1 octet) */ 8,
                            /* Unused Flags (1 octet) */ 1,
                            /* Stream Identifier (31 bits) */ 0, 0, 0, 4,
                            /* Window Size Increment (31 bits) */ 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       absl::StrCat(
                           RFC9113::kStreamIdMustBeOdd,
                           "{WINDOW_UPDATE: flags=1, stream_id=4, length=4}")));
}

TEST(Frame, ParseRejectsWindowUpdateFrameZeroIncrement) {
  // Window Size Increment MUST be non zero
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 4,
                    /* Type (1 octet) */ 8,
                    /* Unused Flags (1 octet) */ 0xff,
                    /* Stream Identifier (31 bits) */ 0, 0, 0, 0,
                    /* Window Size Increment (31 bits) */ 0, 0, 0, 0),
      StatusIs(
          absl::StatusCode::kInternal,
          absl::StrCat(RFC9113::kWindowSizeIncrement,
                       "{WINDOW_UPDATE: flags=255, stream_id=0, length=4}")));
  EXPECT_THAT(
      ValidateFrame(/* Length (3 octets) */ 0, 0, 4,
                    /* Type (1 octet) */ 8,
                    /* Unused Flags (1 octet) */ 0,
                    /* Stream Identifier (31 bits) */ 0x7f, 0xff, 0xff, 0xff,
                    /* Window Size Increment (31 bits) */ 0, 0, 0, 0),
      StatusIs(
          absl::StatusCode::kInternal,
          absl::StrCat(
              RFC9113::kWindowSizeIncrement,
              "{WINDOW_UPDATE: flags=0, stream_id=2147483647, length=4}")));
}

TEST(Frame, GrpcHeaderTest) {
  constexpr uint8_t kFlags = 15;
  constexpr uint32_t kLength = 1111111;

  SliceBuffer payload;
  EXPECT_EQ(payload.Length(), 0);

  AppendGrpcHeaderToSliceBuffer(payload, kFlags, kLength);
  EXPECT_EQ(payload.Length(), kGrpcHeaderSizeInBytes);

  GrpcMessageHeader header = ExtractGrpcHeader(payload);
  EXPECT_EQ(payload.Length(), kGrpcHeaderSizeInBytes);
  EXPECT_EQ(header.flags, kFlags);
  EXPECT_EQ(header.length, kLength);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
