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
#include "src/core/lib/slice/slice_buffer.h"
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

template <typename... I>
Http2Frame ParseFrame(I... i) {
  SliceBuffer buffer;
  buffer.Append(Slice::FromCopiedBuffer(ByteVec(i...)));
  uint8_t hdr[9];
  buffer.MoveFirstNBytesIntoBuffer(9, hdr);
  auto frame_hdr = Http2FrameHeader::Parse(hdr);
  EXPECT_EQ(frame_hdr.length, buffer.Length())
      << "frame_hdr=" << frame_hdr.ToString();
  auto value = ParseFramePayload(frame_hdr, std::move(buffer));
  EXPECT_TRUE(std::holds_alternative<Http2Frame>(value));
  if (std::holds_alternative<Http2Error>(value)) {
    LOG(ERROR) << std::get<Http2Error>(value).absl_status();
  }
  return std::get<Http2Frame>(std::move(value));
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
  Http2StatusOr value = ParseFramePayload(frame_hdr, std::move(buffer));
  EXPECT_TRUE(std::holds_alternative<Http2Error>(value));
  return std::get<Http2Error>(value).absl_status();
}

#define FRAME_LENGTH(num) 0, 0, (num)

#define FRAME_FLAGS(num) (num)

#define PAD_LENGTH(num) (num)

#define STREAM_IDENTIFIER(num) 0, 0, 0, (num)
#define STREAM_IDENTIFIER_4(n1, n2, n3, n4) (n1), (n2), (n3), (n4)
#define STREAM_IDENTIFIER_MAX 0x7F, 0xFF, 0xFF, 0xFF
#define STREAM_IDENTIFIER_BAD 0x8, 0, 0, 0

#define WINDOW_SIZE_INCREMENT(n1, n2, n3, n4) (n1), (n2), (n3), (n4)

#define RANDOM_ZERO 0, 0, 0
#define RANDOM_NUM 1, 2, 3, 4, 5, 6, 7, 8, 9, 10

#define ERROR_CODE(num) 0, 0, 0, (num)

#define PAYLOAD_HELLO 'h', 'e', 'l', 'l', 'o'
#define PAYLOAD_KIDS 'k', 'i', 'd', 's'

#define OPAQUE_DATA_64_BIT 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0
#define OPAQUE_DATA_64_VALUE 0x123456789abcdef0

// HTTP2 Frame Types
enum class FrameType : uint8_t {
  kData = 0,
  kHeader = 1,
  // type 2 was Priority which has been deprecated.
  kRstStream = 3,
  kSettings = 4,
  kPushPromise = 5,
  kPing = 6,
  kGoaway = 7,
  kWindowUpdate = 8,
  kContinuation = 9,
  kCustomSecurity = 200,  // Custom Frame Type
};

constexpr uint8_t kFlagEndStream = 1;
constexpr uint8_t kFlagAck = 1;
constexpr uint8_t kFlagEndHeaders = 4;
constexpr uint8_t kFlagPadded = 8;
constexpr uint8_t kFlagPriority = 0x20;

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
  EXPECT_EQ(Serialize(Http2SecurityFrame{SliceBufferFromString("hello")}),
            ByteVec(0, 0, 5, 200, 0, 0, 0, 0, 0, 'h', 'e', 'l', 'l', 'o'));
}

TEST(Frame, ParseHttp2DataFrame) {
  EXPECT_EQ(
      ParseFrame(FRAME_LENGTH(5), FrameType::kData, FRAME_FLAGS(0),
                 STREAM_IDENTIFIER(1), PAYLOAD_HELLO),
      Http2Frame(Http2DataFrame{1, false, SliceBufferFromString("hello")}));

  EXPECT_EQ(ParseFrame(FRAME_LENGTH(4), FrameType::kData, FRAME_FLAGS(1),
                       STREAM_IDENTIFIER_MAX, PAYLOAD_KIDS),
            Http2Frame(Http2DataFrame{0x7FFFFFFF, true,
                                      SliceBufferFromString("kids")}));
}

TEST(Frame, ParseHttp2HeaderFrame) {
  EXPECT_EQ(ParseFrame(FRAME_LENGTH(5), FrameType::kHeader, FRAME_FLAGS(0),
                       STREAM_IDENTIFIER(1), PAYLOAD_HELLO),
            Http2Frame(Http2HeaderFrame{1, false, false,
                                        SliceBufferFromString("hello")}));

  EXPECT_EQ(ParseFrame(FRAME_LENGTH(4), FrameType::kHeader, FRAME_FLAGS(4),
                       STREAM_IDENTIFIER_MAX, PAYLOAD_KIDS),
            Http2Frame(Http2HeaderFrame{0x7FFFFFFF, true, false,
                                        SliceBufferFromString("kids")}));

  EXPECT_EQ(ParseFrame(FRAME_LENGTH(4), FrameType::kHeader, FRAME_FLAGS(1),
                       STREAM_IDENTIFIER_MAX, PAYLOAD_KIDS),
            Http2Frame(Http2HeaderFrame{0x7FFFFFFF, false, true,
                                        SliceBufferFromString("kids")}));
}

TEST(Frame, DISABLED_ParseHttp2HeaderFrameWithPriority) {
  EXPECT_EQ(
      ParseFrame(FRAME_LENGTH(13), kFrameTypeHeader, FRAME_FLAGS(kFlagPriority),
                 STREAM_IDENTIFIER(1), STREAM_IDENTIFIER_4(1, 2, 3, 4),
                 /*Weight*/ 5, PAYLOAD_HELLO, 0, 0, 0),
      Http2Frame(
          Http2HeaderFrame{1, false, false, SliceBufferFromString("hello")}));
}

TEST(Frame, ParseHttp2ContinuationFrame) {
  EXPECT_EQ(ParseFrame(FRAME_LENGTH(5), kFrameTypeContinuation, FRAME_FLAGS(0),
                       STREAM_IDENTIFIER(1), PAYLOAD_HELLO),
            Http2Frame(Http2ContinuationFrame{1, false,
                                              SliceBufferFromString("hello")}));
  EXPECT_EQ(ParseFrame(FRAME_LENGTH(5), kFrameTypeContinuation, FRAME_FLAGS(4),
                       STREAM_IDENTIFIER(1), PAYLOAD_HELLO),
            Http2Frame(Http2ContinuationFrame{1, true,
                                              SliceBufferFromString("hello")}));
}

TEST(Frame, ParseHttp2Http2RstStreamFrame) {
  EXPECT_EQ(ParseFrame(FRAME_LENGTH(4), kFrameTypeRstStream, FRAME_FLAGS(0),
                       STREAM_IDENTIFIER(1), ERROR_CODE(0x0a)),
            Http2Frame(Http2RstStreamFrame{1, GRPC_HTTP2_CONNECT_ERROR}));
}

TEST(Frame, ParseHttp2Http2SettingsFrame) {
  EXPECT_EQ(
      ParseFrame(FRAME_LENGTH(0), FrameType::kSettings, FRAME_FLAGS(0),
                 STREAM_IDENTIFIER(0)),
      Http2Frame(Http2SettingsFrame{}));  // Q - Is this even a valid frame?

  EXPECT_EQ(
      ParseFrame(FRAME_LENGTH(6), FrameType::kSettings, FRAME_FLAGS(0),
                 STREAM_IDENTIFIER(0), 0x12, 0x34, 0x9a, 0xbc, 0xde, 0xf0),
      Http2Frame(Http2SettingsFrame{false, {{0x1234, 0x9abcdef0}}}));

  EXPECT_EQ(ParseFrame(FRAME_LENGTH(12), FrameType::kSettings, FRAME_FLAGS(0),
                       STREAM_IDENTIFIER(0), 0x12, 0x34, 0x9a, 0xbc, 0xde, 0xf0,
                       0x43, 0x21, 0x12, 0x34, 0x56, 0x78),
            Http2Frame(Http2SettingsFrame{
                false, {{0x1234, 0x9abcdef0}, {0x4321, 0x12345678}}}));

  EXPECT_EQ(ParseFrame(FRAME_LENGTH(0), FrameType::kSettings, FRAME_FLAGS(1),
                       STREAM_IDENTIFIER(0)),
            Http2Frame(Http2SettingsFrame{true, {}}));
}

TEST(Frame, ParseHttp2Http2PingFrame) {
  EXPECT_EQ(ParseFrame(FRAME_LENGTH(8), FrameType::kPing, FRAME_FLAGS(0),
                       STREAM_IDENTIFIER(0), OPAQUE_DATA_64_BIT),
            Http2Frame(Http2PingFrame{false, OPAQUE_DATA_64_VALUE}));

  EXPECT_EQ(ParseFrame(FRAME_LENGTH(8), FrameType::kPing, FRAME_FLAGS(1),
                       STREAM_IDENTIFIER(0), OPAQUE_DATA_64_BIT),
            Http2Frame(Http2PingFrame{true, OPAQUE_DATA_64_VALUE}));
}

TEST(Frame, ParseHttp2GoawayFrame) {
  EXPECT_EQ(
      ParseFrame(FRAME_LENGTH(13), kFrameTypeGoaway, FRAME_FLAGS(0),
                 STREAM_IDENTIFIER(0),
                 STREAM_IDENTIFIER_4(0x12, 0x34, 0x56, 0x78), ERROR_CODE(0x0b),
                 PAYLOAD_HELLO),
      Http2Frame(Http2GoawayFrame{0x12345678, GRPC_HTTP2_ENHANCE_YOUR_CALM,
                                  Slice::FromCopiedString("hello")}));
}

TEST(Frame, ParseHttp2WindowUpdateFrame) {
  EXPECT_EQ(ParseFrame(FRAME_LENGTH(4), FrameType::kWindowUpdate,
                       FRAME_FLAGS(0), STREAM_IDENTIFIER(1),
                       WINDOW_SIZE_INCREMENT(0x12, 0x34, 0x56, 0x78)),
            Http2Frame(Http2WindowUpdateFrame{1, 0x12345678}));
}

TEST(Frame, ParseHttp2Http2SecurityFrame) {
  EXPECT_EQ(ParseFrame(FRAME_LENGTH(5), kFrameTypeSecurity, FRAME_FLAGS(0),
                       STREAM_IDENTIFIER(0), PAYLOAD_HELLO),
            Http2Frame(Http2SecurityFrame{SliceBufferFromString("hello")}));
}

TEST(Frame, ParseHttp2DataFramePadded) {
  // RFC9113 : Padding octets MUST be set to zero when sending.
  EXPECT_EQ(
      ParseFrame(FRAME_LENGTH(9), kFrameTypeData, FRAME_FLAGS(9),
                 STREAM_IDENTIFIER(1), PAD_LENGTH(3), PAYLOAD_HELLO, 0, 0, 0),
      Http2Frame(Http2DataFrame{1, false, SliceBufferFromString("hello")}));
}

TEST(Frame, ParseHttp2HeaderFramePadded) {
  EXPECT_EQ(
      ParseFrame(FRAME_LENGTH(8), kFrameTypeHeader, FRAME_FLAGS(kFlagPadded),
                 STREAM_IDENTIFIER(1), PAD_LENGTH(2), PAYLOAD_HELLO, 0, 0),
      Http2Frame(
          Http2HeaderFrame{1, false, false, SliceBufferFromString("hello")}));
  EXPECT_EQ(
      ParseFrame(FRAME_LENGTH(13), kFrameTypeHeader,
                 FRAME_FLAGS(kFlagPriority & kFlagPadded), STREAM_IDENTIFIER(1),
                 PAD_LENGTH(2), STREAM_IDENTIFIER_4(1, 2, 3, 4),
                 /*Weight*/ 5, PAYLOAD_HELLO, 0, 0),
      Http2Frame(
          Http2HeaderFrame{1, false, false, SliceBufferFromString("hello")}));
}

TEST(Frame, UnknownIgnored) {
  // 77 = some random undefined frame
  EXPECT_EQ(ParseFrame(FRAME_LENGTH(10), 77, FRAME_FLAGS(0),
                       STREAM_IDENTIFIER(1), RANDOM_NUM),
            Http2Frame(Http2UnknownFrame{}));

  // 2 = PRIORITY, we just ignore it
  EXPECT_EQ(ParseFrame(FRAME_LENGTH(10), 2, FRAME_FLAGS(0),
                       STREAM_IDENTIFIER(1), RANDOM_NUM),
            Http2Frame(Http2UnknownFrame{}));
}

TEST(Frame, ParseRejectsPushPromise) {
  EXPECT_THAT(
      ValidateFrame(FRAME_LENGTH(10), FrameType::kPushPromise, FRAME_FLAGS(0),
                    STREAM_IDENTIFIER(1), RANDOM_NUM),
      StatusIs(absl::StatusCode::kInternal,
               "PUSH_PROMISE MUST NOT be sent if the SETTINGS_ENABLE_PUSH "
               "setting of the peer endpoint is set to 0"));
}

TEST(Frame, ParseRejectsDataFrame) {
  EXPECT_THAT(ValidateFrame(FRAME_LENGTH(0), kFrameTypeData, FRAME_FLAGS(0),
  STREAM_IDENTIFIER(0), RANDOM_ZERO), StatusIs(absl::StatusCode::kInternal,
                       "RFC9113 : DATA frames MUST be associated with a
  stream"));

  EXPECT_THAT(ValidateFrame(FRAME_LENGTH(0), kFrameTypeData,
  FRAME_FLAGS(0), STREAM_IDENTIFIER(1), RANDOM_ZERO),
              StatusIs(absl::StatusCode::kInternal,
                       "RFC9113 : Streams initiated by a client MUST use
  odd-numbered stream identifiers"));
}

TEST(Frame, ParseRejectsHeaderFrame) {
  EXPECT_THAT(ValidateFrame(FRAME_LENGTH(0), kFrameTypeHeader, FRAME_FLAGS(0),
  STREAM_IDENTIFIER(0), RANDOM_ZERO), StatusIs(absl::StatusCode::kInternal,
                       "RFC9113 : HEADERS frames MUST be associated with a
  stream"));

  EXPECT_THAT(ValidateFrame(FRAME_LENGTH(0), kFrameTypeHeader,
  FRAME_FLAGS(0), STREAM_IDENTIFIER(2), RANDOM_ZERO),
              StatusIs(absl::StatusCode::kInternal,
                       "RFC9113 : Streams initiated by a client MUST use
  odd-numbered stream identifiers"));
}

TEST(Frame, ParseRejectsContinuationFrame) {
  EXPECT_THAT(
      ValidateFrame(0, 0, 0, 9, 0, 0, 0, 0, 0),
      StatusIs(
          absl::StatusCode::kInternal,
          "RFC9113 : CONTINUATION frames MUST be associated with a stream"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 9, 0, 0, 0, 0, 0), // Fix
              StatusIs(absl::StatusCode::kInternal,
                       "RFC9113 : Streams initiated by a client MUST use
  odd-numbered stream identifiers"));
}

TEST(Frame, ParseRejectsRstStreamFrame) {
  EXPECT_THAT(ValidateFrame(FRAME_LENGTH(0), kFrameTypeRstStream, FRAME_FLAGS(0),
  STREAM_IDENTIFIER(0), ERROR_CODE(0)), StatusIs(absl::StatusCode::kInternal,
                       "RFC9113 : RST_STREAM frames MUST be associated with a
  stream"));

  EXPECT_THAT(ValidateFrame(FRAME_LENGTH(0), kFrameTypeRstStream,
  FRAME_FLAGS(0), STREAM_IDENTIFIER(2), ERROR_CODE(0)),
              StatusIs(absl::StatusCode::kInternal,
                       "RFC9113 : Streams initiated by a client MUST use
  odd-numbered stream identifiers"));

  EXPECT_THAT(ValidateFrame(FRAME_LENGTH(3),
  kFrameTypeRstStream, FRAME_FLAGS(0), STREAM_IDENTIFIER(1), ERROR_CODE(0)),
              StatusIs(absl::StatusCode::kInternal,
                       "RFC9113 : A RST_STREAM frame with a length other than 4
  octets MUST be treated as a connection error"));

  EXPECT_THAT(ValidateFrame(FRAME_LENGTH(4), kFrameTypeRstStream, FRAME_FLAGS(0),
  STREAM_IDENTIFIER(1), ERROR_CODE(0)), StatusIs(absl::StatusCode::kInternal,
                       "RFC9113 : A RST_STREAM frame with a length other than 4
  octets MUST be treated as a connection error"));


  EXPECT_THAT(ValidateFrame(0, 0, 4, 3, 0, 0, 0, 0, 0, 100, 100, 100, 100),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid stream id: {RST_STREAM: flags=0, "
                       "stream_id=0, length=4}"));
}

TEST(Frame, ParseRejectsSettingsFrame) {
  EXPECT_THAT(ValidateFrame(0, 0, 2, 3, 0, 0, 0, 0, 0, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid settings payload: {SETTINGS: flags=0, "
                       "stream_id=0, length=2} -- settings must be multiples "
                       "of 6 bytes long"));
  EXPECT_THAT(ValidateFrame(0, 0, 3, 3, 0, 0, 0, 0, 0, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid settings payload: {SETTINGS: flags=0, "
                       "stream_id=0, length=3} -- settings must be multiples "
                       "of 6 bytes long"));
  EXPECT_THAT(ValidateFrame(0, 0, 4, 3, 0, 0, 0, 0, 0, 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid settings payload: {SETTINGS: flags=0, "
                       "stream_id=0, length=4} -- settings must be multiples "
                       "of 6 bytes long"));
  EXPECT_THAT(ValidateFrame(0, 0, 5, 3, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid settings payload: {SETTINGS: flags=0, "
                       "stream_id=0, length=5} -- settings must be multiples "
                       "of 6 bytes long"));
  EXPECT_THAT(ValidateFrame(0, 0, 7, 3, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid settings payload: {SETTINGS: flags=0, "
                       "stream_id=0, length=7} -- settings must be multiples "
                       "of 6 bytes long"));
  EXPECT_THAT(ValidateFrame(0, 0, 0, 3, 0, 0, 0, 0, 1),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid stream id: {SETTINGS: flags=0, "
                       "stream_id=1, length=0}"));
}

TEST(Frame, ParseRejectsPingFrame) {
  EXPECT_THAT(ValidateFrame(0, 0, 0, 6, 0, 0, 0, 0, 0),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid ping payload: {PING: flags=0, "
                       "stream_id=0, length=0}"));
  EXPECT_THAT(ValidateFrame(0, 0, 8, 6, 0, 0, 0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8),
              StatusIs(absl::StatusCode::kInternal,
                       "invalid ping stream id: {PING: flags=0, "
                       "stream_id=1, length=8}"));
}

TEST(Frame, ParseRejectsGoawayFrame) {
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
}

TEST(Frame, ParseRejectsWindowUpdateFrame) {
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

TEST(Frame, GrpcHeaderTest) {
  constexpr uint8_t kFlags = 15;
  constexpr uint32_t kLength = 1111111;

  SliceBuffer payload;
  EXPECT_EQ(payload.Length(), 0);

  AppendGrpcHeaderToSliceBuffer(payload, kFlags, kLength);
  EXPECT_EQ(payload.Length(), kGrpcHeaderSizeInBytes);

  GrpcMessageHeader header = ExtractGrpcHeader(payload);
  EXPECT_EQ(payload.Length(), 0);
  EXPECT_EQ(header.flags, kFlags);
  EXPECT_EQ(header.length, kLength);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}