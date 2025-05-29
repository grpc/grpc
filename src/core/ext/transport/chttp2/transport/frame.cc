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

#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <cstdint>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/crash.h"

using grpc_core::http2::Http2ErrorCode;
using grpc_core::http2::Http2Status;
using grpc_core::http2::ValueOrHttp2Status;

namespace grpc_core {

namespace {

// TODO(tjagtap) TODO(akshitpatel): [PH2][P3] : Write micro benchmarks for
// framing code

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

void Write2b(uint16_t x, uint8_t* output) {
  output[0] = static_cast<uint8_t>(x >> 8);
  output[1] = static_cast<uint8_t>(x);
}

uint16_t Read2b(const uint8_t* input) {
  return static_cast<uint16_t>(input[0]) << 8 | static_cast<uint16_t>(input[1]);
}

void Write3b(uint32_t x, uint8_t* output) {
  CHECK_LT(x, 16777216u);
  output[0] = static_cast<uint8_t>(x >> 16);
  output[1] = static_cast<uint8_t>(x >> 8);
  output[2] = static_cast<uint8_t>(x);
}

uint32_t Read3b(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) << 16 |
         static_cast<uint32_t>(input[1]) << 8 | static_cast<uint32_t>(input[2]);
}

constexpr uint32_t k8BitMask = 0x7f;

void Write31bits(uint32_t x, uint8_t* output) {
  output[0] = static_cast<uint8_t>(k8BitMask & (x >> 24));
  output[1] = static_cast<uint8_t>(x >> 16);
  output[2] = static_cast<uint8_t>(x >> 8);
  output[3] = static_cast<uint8_t>(x);
}

uint32_t Read31bits(const uint8_t* input) {
  return (k8BitMask & static_cast<uint32_t>(input[0])) << 24 |
         static_cast<uint32_t>(input[1]) << 16 |
         static_cast<uint32_t>(input[2]) << 8 | static_cast<uint32_t>(input[3]);
}

void Write4b(uint32_t x, uint8_t* output) {
  output[0] = static_cast<uint8_t>(x >> 24);
  output[1] = static_cast<uint8_t>(x >> 16);
  output[2] = static_cast<uint8_t>(x >> 8);
  output[3] = static_cast<uint8_t>(x);
}

uint32_t Read4b(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) << 24 |
         static_cast<uint32_t>(input[1]) << 16 |
         static_cast<uint32_t>(input[2]) << 8 | static_cast<uint32_t>(input[3]);
}

void Write8b(uint64_t x, uint8_t* output) {
  output[0] = static_cast<uint8_t>(x >> 56);
  output[1] = static_cast<uint8_t>(x >> 48);
  output[2] = static_cast<uint8_t>(x >> 40);
  output[3] = static_cast<uint8_t>(x >> 32);
  output[4] = static_cast<uint8_t>(x >> 24);
  output[5] = static_cast<uint8_t>(x >> 16);
  output[6] = static_cast<uint8_t>(x >> 8);
  output[7] = static_cast<uint8_t>(x);
}

uint64_t Read8b(const uint8_t* input) {
  return static_cast<uint64_t>(input[0]) << 56 |
         static_cast<uint64_t>(input[1]) << 48 |
         static_cast<uint64_t>(input[2]) << 40 |
         static_cast<uint64_t>(input[3]) << 32 |
         static_cast<uint64_t>(input[4]) << 24 |
         static_cast<uint64_t>(input[5]) << 16 |
         static_cast<uint64_t>(input[6]) << 8 | static_cast<uint64_t>(input[7]);
}

uint8_t MaybeFlag(bool condition, uint8_t flag_mask) {
  return condition ? flag_mask : 0;
}

bool ExtractFlag(uint8_t flags, uint8_t flag_mask) {
  return (flags & flag_mask) != 0;
}

class SerializeExtraBytesRequired {
 public:
  size_t operator()(const Http2DataFrame&) { return 0; }
  size_t operator()(const Http2HeaderFrame&) { return 0; }
  size_t operator()(const Http2ContinuationFrame&) { return 0; }
  size_t operator()(const Http2RstStreamFrame&) { return 4; }
  size_t operator()(const Http2SettingsFrame& f) {
    return 6 * f.settings.size();
  }
  size_t operator()(const Http2PingFrame&) { return 8; }
  size_t operator()(const Http2GoawayFrame&) { return 8; }
  size_t operator()(const Http2WindowUpdateFrame&) { return 4; }
  size_t operator()(const Http2SecurityFrame&) { return 0; }
  size_t operator()(const Http2UnknownFrame&) { Crash("unreachable"); }
  size_t operator()(const Http2EmptyFrame&) { return 0; }
};

class SerializeHeaderAndPayload {
 public:
  SerializeHeaderAndPayload(size_t extra_bytes, SliceBuffer& out)
      : out_(out),
        extra_bytes_(MutableSlice::CreateUninitialized(extra_bytes)) {}

  void operator()(Http2DataFrame& frame) {
    auto hdr = extra_bytes_.TakeFirst(kFrameHeaderSize);
    Http2FrameHeader{static_cast<uint32_t>(frame.payload.Length()),
                     static_cast<uint8_t>(FrameType::kData),
                     MaybeFlag(frame.end_stream, kFlagEndStream),
                     frame.stream_id}
        .Serialize(hdr.begin());
    out_.AppendIndexed(Slice(std::move(hdr)));
    out_.TakeAndAppend(frame.payload);
  }

  void operator()(Http2HeaderFrame& frame) {
    auto hdr = extra_bytes_.TakeFirst(kFrameHeaderSize);
    Http2FrameHeader{
        static_cast<uint32_t>(frame.payload.Length()),
        static_cast<uint8_t>(FrameType::kHeader),
        static_cast<uint8_t>(MaybeFlag(frame.end_headers, kFlagEndHeaders) |
                             MaybeFlag(frame.end_stream, kFlagEndStream)),
        frame.stream_id}
        .Serialize(hdr.begin());
    out_.AppendIndexed(Slice(std::move(hdr)));
    out_.TakeAndAppend(frame.payload);
  }

  void operator()(Http2ContinuationFrame& frame) {
    auto hdr = extra_bytes_.TakeFirst(kFrameHeaderSize);
    Http2FrameHeader{
        static_cast<uint32_t>(frame.payload.Length()),
        static_cast<uint8_t>(FrameType::kContinuation),
        static_cast<uint8_t>(MaybeFlag(frame.end_headers, kFlagEndHeaders)),
        frame.stream_id}
        .Serialize(hdr.begin());
    out_.AppendIndexed(Slice(std::move(hdr)));
    out_.TakeAndAppend(frame.payload);
  }

  void operator()(Http2RstStreamFrame& frame) {
    auto hdr_and_payload = extra_bytes_.TakeFirst(kFrameHeaderSize + 4);
    Http2FrameHeader{4, static_cast<uint8_t>(FrameType::kRstStream), 0,
                     frame.stream_id}
        .Serialize(hdr_and_payload.begin());
    Write4b(frame.error_code, hdr_and_payload.begin() + kFrameHeaderSize);
    out_.AppendIndexed(Slice(std::move(hdr_and_payload)));
  }

  void operator()(Http2SettingsFrame& frame) {
    // Six bytes per setting (u16 id, u32 value)
    const size_t payload_size = 6 * frame.settings.size();
    auto hdr_and_payload =
        extra_bytes_.TakeFirst(kFrameHeaderSize + payload_size);
    Http2FrameHeader{static_cast<uint32_t>(payload_size),
                     static_cast<uint8_t>(FrameType::kSettings),
                     MaybeFlag(frame.ack, kFlagAck), 0}
        .Serialize(hdr_and_payload.begin());
    size_t offset = kFrameHeaderSize;
    for (auto& setting : frame.settings) {
      Write2b(setting.id, hdr_and_payload.begin() + offset);
      Write4b(setting.value, hdr_and_payload.begin() + offset + 2);
      offset += 6;
    }
    out_.AppendIndexed(Slice(std::move(hdr_and_payload)));
  }

  void operator()(Http2PingFrame& frame) {
    auto hdr_and_payload = extra_bytes_.TakeFirst(kFrameHeaderSize + 8);
    Http2FrameHeader{8, static_cast<uint8_t>(FrameType::kPing),
                     MaybeFlag(frame.ack, kFlagAck), 0}
        .Serialize(hdr_and_payload.begin());
    Write8b(frame.opaque, hdr_and_payload.begin() + kFrameHeaderSize);
    out_.AppendIndexed(Slice(std::move(hdr_and_payload)));
  }

  void operator()(Http2GoawayFrame& frame) {
    auto hdr_and_fixed_payload = extra_bytes_.TakeFirst(kFrameHeaderSize + 8);
    Http2FrameHeader{static_cast<uint32_t>(8 + frame.debug_data.length()),
                     static_cast<uint8_t>(FrameType::kGoaway), 0, 0}
        .Serialize(hdr_and_fixed_payload.begin());
    if (GPR_UNLIKELY(frame.last_stream_id > RFC9113::kMaxStreamId31Bit)) {
      LOG(ERROR) << "Stream ID will be truncated. The MSB will be set to 0 "
                 << frame.last_stream_id;
    }
    Write31bits(frame.last_stream_id,
                hdr_and_fixed_payload.begin() + kFrameHeaderSize);
    Write4b(frame.error_code,
            hdr_and_fixed_payload.begin() + kFrameHeaderSize + 4);
    out_.AppendIndexed(Slice(std::move(hdr_and_fixed_payload)));
    out_.AppendIndexed(std::move(frame.debug_data));
  }

  void operator()(Http2WindowUpdateFrame& frame) {
    auto hdr_and_payload = extra_bytes_.TakeFirst(kFrameHeaderSize + 4);
    Http2FrameHeader{4, static_cast<uint8_t>(FrameType::kWindowUpdate), 0,
                     frame.stream_id}
        .Serialize(hdr_and_payload.begin());
    if (GPR_UNLIKELY(frame.increment > RFC9113::kMaxStreamId31Bit)) {
      LOG(ERROR) << "Http2WindowUpdateFrame increment will be truncated to 31 "
                    "bits. The MSB will be set to 0 "
                 << frame.increment;
    }
    Write31bits(frame.increment, hdr_and_payload.begin() + kFrameHeaderSize);
    out_.AppendIndexed(Slice(std::move(hdr_and_payload)));
  }

  void operator()(Http2SecurityFrame& frame) {
    auto hdr = extra_bytes_.TakeFirst(kFrameHeaderSize);
    Http2FrameHeader{static_cast<uint32_t>(frame.payload.Length()),
                     static_cast<uint8_t>(FrameType::kCustomSecurity), 0, 0}
        .Serialize(hdr.begin());
    out_.AppendIndexed(Slice(std::move(hdr)));
    out_.TakeAndAppend(frame.payload);
  }

  void operator()(Http2UnknownFrame&) { Crash("unreachable"); }

  void operator()(Http2EmptyFrame&) {}

 private:
  SliceBuffer& out_;
  MutableSlice extra_bytes_;
};

Http2Status StripPadding(const Http2FrameHeader& hdr, SliceBuffer& payload) {
  if (GPR_UNLIKELY(payload.Length() < 1)) {
    return Http2Status::Http2ConnectionError(
        Http2ErrorCode::kProtocolError,
        absl::StrCat(RFC9113::kFrameParserIncorrectPadding, hdr.ToString()));
  }
  const size_t payload_size = payload.Length();
  uint8_t padding_bytes;
  payload.MoveFirstNBytesIntoBuffer(1, &padding_bytes);

  if (GPR_UNLIKELY(payload_size <= padding_bytes)) {
    return Http2Status::Http2ConnectionError(
        Http2ErrorCode::kProtocolError,
        absl::StrCat(RFC9113::kPaddingLengthLargerThanFrameLength,
                     hdr.ToString()));
  }

  // We dont check for padding being zero.
  // No point checking bytes that will be discarded.
  // RFC9113 : A receiver is not obligated to verify padding but MAY treat
  // non-zero padding as a connection error of type PROTOCOL_ERROR.
  payload.RemoveLastNBytes(padding_bytes);
  return Http2Status::Ok();
}

ValueOrHttp2Status<Http2Frame> ParseDataFrame(const Http2FrameHeader& hdr,
                                              SliceBuffer& payload) {
  if (GPR_UNLIKELY((hdr.stream_id % 2) == 0)) {
    if ((hdr.stream_id == 0)) {
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          absl::StrCat(RFC9113::kDataStreamIdMustBeNonZero, hdr.ToString()));
    } else {
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          absl::StrCat(RFC9113::kStreamIdMustBeOdd, hdr.ToString()));
    }
  }

  if (hdr.flags & kFlagPadded) {
    Http2Status s = StripPadding(hdr, payload);
    if (GPR_UNLIKELY(!s.IsOk())) {
      return ValueOrHttp2Status<Http2Frame>(std::move(s));
    }
  }

  return ValueOrHttp2Status<Http2Frame>(
      Http2DataFrame{hdr.stream_id, ExtractFlag(hdr.flags, kFlagEndStream),
                     std::move(payload)});
}

ValueOrHttp2Status<Http2Frame> ParseHeaderFrame(const Http2FrameHeader& hdr,
                                                SliceBuffer& payload) {
  if (GPR_UNLIKELY((hdr.stream_id % 2) == 0)) {
    if (hdr.stream_id == 0) {
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          absl::StrCat(RFC9113::kHeaderStreamIdMustBeNonZero, hdr.ToString()));
    } else {
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          absl::StrCat(RFC9113::kStreamIdMustBeOdd, hdr.ToString()));
    }
  }

  if (hdr.flags & kFlagPadded) {
    Http2Status s = StripPadding(hdr, payload);
    if (GPR_UNLIKELY(!s.IsOk())) {
      return ValueOrHttp2Status<Http2Frame>(std::move(s));
    }
  }

  if (GPR_UNLIKELY(hdr.flags & kFlagPriority)) {
    if (GPR_UNLIKELY(payload.Length() < 5)) {
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          absl::StrCat(RFC9113::kIncorrectFrame, hdr.ToString()));
    }
    uint8_t trash[5];
    payload.MoveFirstNBytesIntoBuffer(5, trash);
  }

  return ValueOrHttp2Status<Http2Frame>(Http2HeaderFrame{
      hdr.stream_id, ExtractFlag(hdr.flags, kFlagEndHeaders),
      ExtractFlag(hdr.flags, kFlagEndStream), std::move(payload)});
}

ValueOrHttp2Status<Http2Frame> ParseContinuationFrame(
    const Http2FrameHeader& hdr, SliceBuffer& payload) {
  if (GPR_UNLIKELY((hdr.stream_id % 2) == 0)) {
    if (hdr.stream_id == 0) {
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          absl::StrCat(RFC9113::kContinuationStreamIdMustBeNonZero,
                       hdr.ToString()));
    } else {
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          absl::StrCat(RFC9113::kStreamIdMustBeOdd, hdr.ToString()));
    }
  }

  return ValueOrHttp2Status<Http2Frame>(Http2ContinuationFrame{
      hdr.stream_id, ExtractFlag(hdr.flags, kFlagEndHeaders),
      std::move(payload)});
}

ValueOrHttp2Status<Http2Frame> ParseRstStreamFrame(const Http2FrameHeader& hdr,
                                                   SliceBuffer& payload) {
  if (GPR_UNLIKELY(payload.Length() != 4)) {
    return Http2Status::Http2ConnectionError(
        Http2ErrorCode::kFrameSizeError,
        absl::StrCat(RFC9113::kRstStreamLength4, hdr.ToString()));
  }

  if (GPR_UNLIKELY((hdr.stream_id % 2) == 0)) {
    if ((hdr.stream_id == 0)) {
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          absl::StrCat(RFC9113::kRstStreamStreamIdMustBeNonZero,
                       hdr.ToString()));
    } else {
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          absl::StrCat(RFC9113::kStreamIdMustBeOdd, hdr.ToString()));
    }
  }

  uint8_t buffer[4];
  payload.CopyToBuffer(buffer);

  return ValueOrHttp2Status<Http2Frame>(
      Http2RstStreamFrame{hdr.stream_id, Read4b(buffer)});
}

ValueOrHttp2Status<Http2Frame> ParseSettingsFrame(const Http2FrameHeader& hdr,
                                                  SliceBuffer& payload) {
  if (GPR_UNLIKELY(hdr.stream_id != 0)) {
    return Http2Status::Http2ConnectionError(
        Http2ErrorCode::kProtocolError,
        absl::StrCat(RFC9113::kSettingsStreamIdMustBeZero, hdr.ToString()));
  }

  if (hdr.flags & kFlagAck) {
    if (GPR_UNLIKELY(payload.Length() != 0)) {
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kFrameSizeError,
          absl::StrCat(RFC9113::kSettingsLength0, hdr.ToString()));
    }
    return ValueOrHttp2Status<Http2Frame>(Http2SettingsFrame{true, {}});
  }

  if (GPR_UNLIKELY(payload.Length() % 6 != 0)) {
    return Http2Status::Http2ConnectionError(
        Http2ErrorCode::kFrameSizeError,
        absl::StrCat(RFC9113::kSettingsLength6x, hdr.ToString()));
  }

  Http2SettingsFrame frame{false, {}};
  while (payload.Length() != 0) {
    uint8_t buffer[6];
    payload.MoveFirstNBytesIntoBuffer(6, buffer);
    frame.settings.push_back({
        Read2b(buffer),
        Read4b(buffer + 2),
    });
  }
  return ValueOrHttp2Status<Http2Frame>(std::move(frame));
}

ValueOrHttp2Status<Http2Frame> ParsePingFrame(const Http2FrameHeader& hdr,
                                              SliceBuffer& payload) {
  if (GPR_UNLIKELY(payload.Length() != 8)) {
    return Http2Status::Http2ConnectionError(
        Http2ErrorCode::kFrameSizeError,
        absl::StrCat(RFC9113::kPingLength8, hdr.ToString()));
  }

  if (GPR_UNLIKELY(hdr.stream_id != 0)) {
    return Http2Status::Http2ConnectionError(
        Http2ErrorCode::kProtocolError,
        absl::StrCat(RFC9113::kPingStreamIdMustBeZero, hdr.ToString()));
  }

  // RFC9113 : Unused flags MUST be ignored on receipt and MUST be left unset
  // (0x00) when sending.
  bool ack = ((hdr.flags & kFlagAck) == kFlagAck);

  uint8_t buffer[8];
  payload.CopyToBuffer(buffer);

  return ValueOrHttp2Status<Http2Frame>(Http2PingFrame{ack, Read8b(buffer)});
}

ValueOrHttp2Status<Http2Frame> ParseGoawayFrame(const Http2FrameHeader& hdr,
                                                SliceBuffer& payload) {
  if (GPR_UNLIKELY(payload.Length() < 8)) {
    return Http2Status::Http2ConnectionError(
        Http2ErrorCode::kFrameSizeError,
        absl::StrCat(RFC9113::kGoAwayLength8, hdr.ToString()));
  }

  if (GPR_UNLIKELY(hdr.stream_id != 0)) {
    return Http2Status::Http2ConnectionError(
        Http2ErrorCode::kProtocolError,
        absl::StrCat(RFC9113::kGoAwayStreamIdMustBeZero, hdr.ToString()));
  }

  uint8_t buffer[8];
  payload.MoveFirstNBytesIntoBuffer(8, buffer);
  return ValueOrHttp2Status<Http2Frame>(Http2GoawayFrame{
      /*Last-Stream-ID (31)*/ Read31bits(buffer),
      /*Error Code (32)*/ Read4b(buffer + 4),
      /*Additional Debug Data(variable)*/ payload.JoinIntoSlice()});
}

ValueOrHttp2Status<Http2Frame> ParseWindowUpdateFrame(
    const Http2FrameHeader& hdr, SliceBuffer& payload) {
  if (GPR_UNLIKELY(payload.Length() != 4)) {
    return Http2Status::Http2ConnectionError(
        Http2ErrorCode::kFrameSizeError,
        absl::StrCat(RFC9113::kWindowUpdateLength4, hdr.ToString()));
  }
  if (GPR_UNLIKELY(hdr.stream_id > 0u && (hdr.stream_id % 2) == 0)) {
    return Http2Status::Http2ConnectionError(
        Http2ErrorCode::kProtocolError,
        absl::StrCat(RFC9113::kStreamIdMustBeOdd, hdr.ToString()));
  }
  uint8_t buffer[4];
  payload.CopyToBuffer(buffer);
  const uint32_t window_size_increment = Read31bits(buffer);
  if (GPR_UNLIKELY(window_size_increment == 0)) {
    if (hdr.stream_id == 0) {
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          absl::StrCat(RFC9113::kWindowSizeIncrement, hdr.ToString()));
    } else {
      return Http2Status::Http2StreamError(
          Http2ErrorCode::kProtocolError,
          absl::StrCat(RFC9113::kWindowSizeIncrement, hdr.ToString()));
    }
  }
  return ValueOrHttp2Status<Http2Frame>(
      Http2WindowUpdateFrame{hdr.stream_id, window_size_increment});
}

ValueOrHttp2Status<Http2Frame> ParseSecurityFrame(
    const Http2FrameHeader& /*hdr*/, SliceBuffer& payload) {
  // TODO(tjagtap) : [PH2][P3] : Add validations
  return ValueOrHttp2Status<Http2Frame>(Http2SecurityFrame{std::move(payload)});
}

}  // namespace

void Http2FrameHeader::Serialize(uint8_t* output) const {
  Write3b(length, output);
  output[3] = type;
  output[4] = flags;
  Write4b(stream_id, output + 5);
}

Http2FrameHeader Http2FrameHeader::Parse(const uint8_t* input) {
  return Http2FrameHeader{
      /* Length(24) */ Read3b(input),
      /* Type(8) */ input[3],
      /* Flags(8) */ input[4],
      /* Reserved(1), Stream Identifier(31) */ Read31bits(input + 5)};
}

namespace {

std::string Http2FrameTypeString(FrameType frame_type) {
  switch (frame_type) {
    case FrameType::kData:
      return "DATA";
    case FrameType::kHeader:
      return "HEADER";
    case FrameType::kRstStream:
      return "RST_STREAM";
    case FrameType::kSettings:
      return "SETTINGS";
    case FrameType::kPushPromise:
      return "PUSH_PROMISE";
    case FrameType::kPing:
      return "PING";
    case FrameType::kGoaway:
      return "GOAWAY";
    case FrameType::kWindowUpdate:
      return "WINDOW_UPDATE";
    case FrameType::kContinuation:
      return "CONTINUATION";
    case FrameType::kCustomSecurity:
      return "SECURITY";
  }
  return absl::StrCat("UNKNOWN(", static_cast<uint8_t>(frame_type), ")");
}
}  // namespace

std::string Http2FrameHeader::ToString() const {
  return absl::StrCat("{", Http2FrameTypeString(static_cast<FrameType>(type)),
                      ": flags=", flags, ", stream_id=", stream_id,
                      ", length=", length, "}");
}

void Serialize(absl::Span<Http2Frame> frames, SliceBuffer& out) {
  size_t buffer_needed = 0;
  for (auto& frame : frames) {
    // Bytes needed for framing
    buffer_needed += kFrameHeaderSize;
    // Bytes needed for frame payload
    buffer_needed += std::visit(SerializeExtraBytesRequired(), frame);
  }
  SerializeHeaderAndPayload serialize(buffer_needed, out);
  for (auto& frame : frames) {
    std::visit(serialize, frame);
  }
}

http2::ValueOrHttp2Status<Http2Frame> ParseFramePayload(
    const Http2FrameHeader& hdr, SliceBuffer payload) {
  CHECK(payload.Length() == hdr.length);

  switch (static_cast<FrameType>(hdr.type)) {
    case FrameType::kData:
      return ParseDataFrame(hdr, payload);
    case FrameType::kHeader:
      return ParseHeaderFrame(hdr, payload);
    case FrameType::kContinuation:
      return ParseContinuationFrame(hdr, payload);
    case FrameType::kRstStream:
      return ParseRstStreamFrame(hdr, payload);
    case FrameType::kSettings:
      return ParseSettingsFrame(hdr, payload);
    case FrameType::kPing:
      return ParsePingFrame(hdr, payload);
    case FrameType::kGoaway:
      return ParseGoawayFrame(hdr, payload);
    case FrameType::kWindowUpdate:
      return ParseWindowUpdateFrame(hdr, payload);
    case FrameType::kPushPromise:
      return Http2Status::Http2ConnectionError(
          Http2ErrorCode::kProtocolError,
          absl::StrCat(RFC9113::kNoPushPromise, hdr.ToString()));
    case FrameType::kCustomSecurity:
      return ParseSecurityFrame(hdr, payload);
    default:
      return ValueOrHttp2Status<Http2Frame>(Http2UnknownFrame{});
  }
}

GrpcMessageHeader ExtractGrpcHeader(SliceBuffer& payload) {
  CHECK_GE(payload.Length(), kGrpcHeaderSizeInBytes);
  uint8_t buffer[kGrpcHeaderSizeInBytes];
  payload.CopyFirstNBytesIntoBuffer(kGrpcHeaderSizeInBytes, buffer);
  GrpcMessageHeader header;
  header.flags = buffer[0];
  header.length = Read4b(buffer + 1);
  return header;
}

void AppendGrpcHeaderToSliceBuffer(SliceBuffer& payload, const uint8_t flags,
                                   const uint32_t length) {
  uint8_t* frame_hdr = payload.AddTiny(kGrpcHeaderSizeInBytes);
  frame_hdr[0] = flags;
  Write4b(length, frame_hdr + 1);
}

}  // namespace grpc_core
