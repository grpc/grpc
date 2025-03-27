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
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/crash.h"

namespace grpc_core {

namespace {

// HTTP2 Frame Types
constexpr uint8_t kFrameTypeData = 0;
constexpr uint8_t kFrameTypeHeader = 1;
// type 2 was Priority which has been deprecated.
constexpr uint8_t kFrameTypeRstStream = 3;
constexpr uint8_t kFrameTypeSettings = 4;
constexpr uint8_t kFrameTypePushPromise = 5;
constexpr uint8_t kFrameTypePing = 6;
constexpr uint8_t kFrameTypeGoaway = 7;
constexpr uint8_t kFrameTypeWindowUpdate = 8;
constexpr uint8_t kFrameTypeContinuation = 9;

// Custom Frame Type
constexpr uint8_t kFrameTypeSecurity = 200;

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
  size_t operator()(const Http2EmptyFrame&) { Crash("unreachable"); }
};

class SerializeHeaderAndPayload {
 public:
  SerializeHeaderAndPayload(size_t extra_bytes, SliceBuffer& out)
      : out_(out),
        extra_bytes_(MutableSlice::CreateUninitialized(extra_bytes)) {}

  void operator()(Http2DataFrame& frame) {
    auto hdr = extra_bytes_.TakeFirst(kFrameHeaderSize);
    Http2FrameHeader{
        static_cast<uint32_t>(frame.payload.Length()), kFrameTypeData,
        MaybeFlag(frame.end_stream, kFlagEndStream), frame.stream_id}
        .Serialize(hdr.begin());
    out_.AppendIndexed(Slice(std::move(hdr)));
    out_.TakeAndAppend(frame.payload);
  }

  void operator()(Http2HeaderFrame& frame) {
    auto hdr = extra_bytes_.TakeFirst(kFrameHeaderSize);
    Http2FrameHeader{
        static_cast<uint32_t>(frame.payload.Length()), kFrameTypeHeader,
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
        static_cast<uint32_t>(frame.payload.Length()), kFrameTypeContinuation,
        static_cast<uint8_t>(MaybeFlag(frame.end_headers, kFlagEndHeaders)),
        frame.stream_id}
        .Serialize(hdr.begin());
    out_.AppendIndexed(Slice(std::move(hdr)));
    out_.TakeAndAppend(frame.payload);
  }

  void operator()(Http2RstStreamFrame& frame) {
    auto hdr_and_payload = extra_bytes_.TakeFirst(kFrameHeaderSize + 4);
    Http2FrameHeader{4, kFrameTypeRstStream, 0, frame.stream_id}.Serialize(
        hdr_and_payload.begin());
    Write4b(frame.error_code, hdr_and_payload.begin() + kFrameHeaderSize);
    out_.AppendIndexed(Slice(std::move(hdr_and_payload)));
  }

  void operator()(Http2SettingsFrame& frame) {
    // Six bytes per setting (u16 id, u32 value)
    const size_t payload_size = 6 * frame.settings.size();
    auto hdr_and_payload =
        extra_bytes_.TakeFirst(kFrameHeaderSize + payload_size);
    Http2FrameHeader{static_cast<uint32_t>(payload_size), kFrameTypeSettings,
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
    Http2FrameHeader{8, kFrameTypePing, MaybeFlag(frame.ack, kFlagAck), 0}
        .Serialize(hdr_and_payload.begin());
    Write8b(frame.opaque, hdr_and_payload.begin() + kFrameHeaderSize);
    out_.AppendIndexed(Slice(std::move(hdr_and_payload)));
  }

  void operator()(Http2GoawayFrame& frame) {
    auto hdr_and_fixed_payload = extra_bytes_.TakeFirst(kFrameHeaderSize + 8);
    Http2FrameHeader{static_cast<uint32_t>(8 + frame.debug_data.length()),
                     kFrameTypeGoaway, 0, 0}
        .Serialize(hdr_and_fixed_payload.begin());
    Write4b(frame.last_stream_id,
            hdr_and_fixed_payload.begin() + kFrameHeaderSize);
    Write4b(frame.error_code,
            hdr_and_fixed_payload.begin() + kFrameHeaderSize + 4);
    out_.AppendIndexed(Slice(std::move(hdr_and_fixed_payload)));
    out_.AppendIndexed(std::move(frame.debug_data));
  }

  void operator()(Http2WindowUpdateFrame& frame) {
    auto hdr_and_payload = extra_bytes_.TakeFirst(kFrameHeaderSize + 4);
    Http2FrameHeader{4, kFrameTypeWindowUpdate, 0, frame.stream_id}.Serialize(
        hdr_and_payload.begin());
    Write4b(frame.increment, hdr_and_payload.begin() + kFrameHeaderSize);
    out_.AppendIndexed(Slice(std::move(hdr_and_payload)));
  }

  void operator()(Http2SecurityFrame& frame) {
    auto hdr = extra_bytes_.TakeFirst(kFrameHeaderSize);
    Http2FrameHeader{static_cast<uint32_t>(frame.payload.Length()),
                     kFrameTypeSecurity, 0, 0}
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

// 5.1.1.
inline constexpr absl::string_view kStreamIdMustBeOdd =
    "RFC9113 : Streams initiated by a client MUST use odd-numbered stream "
    "identifiers";

// 6.
inline constexpr absl::string_view kDataStreamIdMustBeNonZero =
    "RFC9113 : DATA frames MUST be "
    "associated with a stream";
inline constexpr absl::string_view kHeaderStreamIdMustBeNonZero =
    "RFC9113 : HEADER frames MUST be "
    "associated with a stream";
inline constexpr absl::string_view kContinuationStreamIdMustBeNonZero =
    "RFC9113 : CONTINUATION frames MUST be "
    "associated with a stream";
inline constexpr absl::string_view kRstStreamStreamIdMustBeNonZero =
    "RFC9113 : RST_STREAM frames frames MUST be "
    "associated with a stream";

inline constexpr absl::string_view kPingStreamIdMustBeZero =
    "RFC9113 : The stream identifier for a PING frame MUST be "
    "zero";
inline constexpr absl::string_view kGoAwayStreamIdMustBeZero =
    "RFC9113 : The stream identifier for a GOAWAY frame MUST be "
    "zero";
inline constexpr absl::string_view kSettingsStreamIdMustBeZero =
    "RFC9113 : The stream identifier for a SETTINGS frame MUST be "
    "zero";

inline constexpr absl::string_view kRstStreamLength4 =
    "RFC9113 : A RST_STREAM frame with a length other than 4 octets MUST be "
    "treated as a connection error";
inline constexpr absl::string_view kSettingsLength0 =
    "RFC9113 : Receipt of a SETTINGS frame with the ACK flag set and a length "
    "field value other than 0 MUST be treated as a connection error";
inline constexpr absl::string_view kSettingsLength6x =
    "RFC9113 : SETTINGS frame with a length other than a multiple of 6 octets "
    "MUST be treated as a connection error";
inline constexpr absl::string_view kPingLength8 =
    "Receipt of a PING frame with a length field value other than 8 MUST be "
    "treated as a connection error";
inline constexpr absl::string_view kNoPushPromise =
    "PUSH_PROMISE MUST NOT be sent if the SETTINGS_ENABLE_PUSH setting of the "
    "peer endpoint is set to 0";
inline constexpr absl::string_view kWindowUpdateLength4 =
    "A WINDOW_UPDATE frame with a length other than 4 octets MUST be "
    "treated as a connection error";

// Misc Errors
inline constexpr absl::string_view kFrameParserIncorrectPadding =
    "Incorrect length of padding in frame";
inline constexpr absl::string_view kIncorrectFrame = "Incorrect Frame";
inline constexpr absl::string_view kGoAwayLength8 =
    "GOAWAY frame should have a Last-Stream-ID and Error Code making the "
    "minimum length 8 octets";

Http2Error StripPadding(SliceBuffer& payload) {
  if (payload.Length() < 1) {
    return Http2Error::ProtocolConnectionError(kFrameParserIncorrectPadding);
  }
  uint8_t padding_bytes;
  payload.MoveFirstNBytesIntoBuffer(1, &padding_bytes);
  if (payload.Length() < padding_bytes) {
    return Http2Error::ProtocolConnectionError(kFrameParserIncorrectPadding);
  }
  payload.RemoveLastNBytes(padding_bytes);
  return Http2Error::Ok();
}

Http2StatusOr ParseDataFrame(const Http2FrameHeader& hdr,
                             SliceBuffer& payload) {
  if (hdr.stream_id == 0) {
    return Http2Error::ProtocolConnectionError(kDataStreamIdMustBeNonZero);
  } else if ((hdr.stream_id % 2) == 0) {
    return Http2Error::ProtocolConnectionError(kStreamIdMustBeOdd);
  }

  if (hdr.flags & kFlagPadded) {
    auto s = StripPadding(payload);
    if (!s.ok()) return s;
  }

  return Http2DataFrame{hdr.stream_id, ExtractFlag(hdr.flags, kFlagEndStream),
                        std::move(payload)};
}

Http2StatusOr ParseHeaderFrame(const Http2FrameHeader& hdr,
                               SliceBuffer& payload) {
  if (hdr.stream_id == 0) {
    return Http2Error::ProtocolConnectionError(kHeaderStreamIdMustBeNonZero);
  } else if ((hdr.stream_id % 2) == 0) {
    return Http2Error::ProtocolConnectionError(kStreamIdMustBeOdd);
  }

  if (hdr.flags & kFlagPadded) {
    auto s = StripPadding(payload);
    if (!s.ok()) return s;
  }

  if (hdr.flags & kFlagPriority) {
    if (payload.Length() < 5) {
      Http2Error::ProtocolConnectionError(kIncorrectFrame);
    }
    uint8_t trash[5];
    payload.MoveFirstNBytesIntoBuffer(5, trash);
  }

  return Http2HeaderFrame{
      hdr.stream_id, ExtractFlag(hdr.flags, kFlagEndHeaders),
      ExtractFlag(hdr.flags, kFlagEndStream), std::move(payload)};
}

Http2StatusOr ParseContinuationFrame(const Http2FrameHeader& hdr,
                                     SliceBuffer& payload) {
  if (hdr.stream_id == 0) {
    return Http2Error::ProtocolConnectionError(
        kContinuationStreamIdMustBeNonZero);
  } else if ((hdr.stream_id % 2) == 0) {
    return Http2Error::ProtocolConnectionError(kStreamIdMustBeOdd);
  }

  return Http2ContinuationFrame{hdr.stream_id,
                                ExtractFlag(hdr.flags, kFlagEndHeaders),
                                std::move(payload)};
}

Http2StatusOr ParseRstStreamFrame(const Http2FrameHeader& hdr,
                                  SliceBuffer& payload) {
  if (payload.Length() != 4) {
    return Http2Error::ProtocolConnectionError(kRstStreamLength4);
  }

  if (hdr.stream_id == 0) {
    return Http2Error::ProtocolConnectionError(kRstStreamStreamIdMustBeNonZero);
  } else if ((hdr.stream_id % 2) == 0) {
    return Http2Error::ProtocolConnectionError(kStreamIdMustBeOdd);
  }

  uint8_t buffer[4];
  payload.CopyToBuffer(buffer);

  return Http2RstStreamFrame{hdr.stream_id, Read4b(buffer)};
}

Http2StatusOr ParseSettingsFrame(const Http2FrameHeader& hdr,
                                 SliceBuffer& payload) {
  if (hdr.stream_id != 0) {
    return Http2Error::ProtocolConnectionError(kSettingsStreamIdMustBeZero);
  }

  if (hdr.flags == kFlagAck) {
    if (payload.Length() != 0) {
      return Http2Error::FrameSizeConnectionError(kSettingsLength0);
    }
    return Http2SettingsFrame{true, {}};
  }

  if (payload.Length() % 6 != 0) {
    return Http2Error::FrameSizeConnectionError(kSettingsLength6x);
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
  return std::move(frame);
}

Http2StatusOr ParsePingFrame(const Http2FrameHeader& hdr,
                             SliceBuffer& payload) {
  if (payload.Length() != 8) {
    return Http2Error::FrameSizeConnectionError(kPingLength8);
  }

  if (hdr.stream_id != 0) {
    return Http2Error::ProtocolConnectionError(kPingStreamIdMustBeZero);
  }

  // RFC9113 : Unused flags MUST be ignored on receipt and MUST be left unset
  // (0x00) when sending.
  bool ack = ((hdr.flags & kFlagAck) == kFlagAck);

  uint8_t buffer[8];
  payload.CopyToBuffer(buffer);

  return Http2PingFrame{ack, Read8b(buffer)};
}

Http2StatusOr ParseGoawayFrame(const Http2FrameHeader& hdr,
                               SliceBuffer& payload) {
  if (payload.Length() < 8) {
    return Http2Error::FrameSizeConnectionError(kGoAwayLength8);
  }

  if (hdr.stream_id != 0) {
    return Http2Error::ProtocolConnectionError(kGoAwayStreamIdMustBeZero);
  }

  uint8_t buffer[8];
  payload.MoveFirstNBytesIntoBuffer(8, buffer);
  return Http2GoawayFrame{Read4b(buffer), Read4b(buffer + 4),
                          payload.JoinIntoSlice()};
}

Http2StatusOr ParseWindowUpdateFrame(const Http2FrameHeader& hdr,
                                     SliceBuffer& payload) {
  if (payload.Length() != 4) {
    return Http2Error::FrameSizeConnectionError(kWindowUpdateLength4);
  }

  uint8_t buffer[4];
  payload.CopyToBuffer(buffer);
  return Http2WindowUpdateFrame{hdr.stream_id, Read4b(buffer)};
}

Http2StatusOr ParseSecurityFrame(const Http2FrameHeader& /*hdr*/,
                                 SliceBuffer& payload) {
  return Http2SecurityFrame{std::move(payload)};
}

}  // namespace

void Http2FrameHeader::Serialize(uint8_t* output) const {
  Write3b(length, output);
  output[3] = type;
  output[4] = flags;
  Write4b(stream_id, output + 5);
}

Http2FrameHeader Http2FrameHeader::Parse(const uint8_t* input) {
  return Http2FrameHeader{Read3b(input), input[3], input[4], Read4b(input + 5)};
}

namespace {
std::string Http2FrameTypeString(uint8_t frame_type) {
  switch (frame_type) {
    case kFrameTypeData:
      return "DATA";
    case kFrameTypeHeader:
      return "HEADER";
    case kFrameTypeContinuation:
      return "CONTINUATION";
    case kFrameTypeRstStream:
      return "RST_STREAM";
    case kFrameTypeSettings:
      return "SETTINGS";
    case kFrameTypeGoaway:
      return "GOAWAY";
    case kFrameTypeWindowUpdate:
      return "WINDOW_UPDATE";
    case kFrameTypePing:
      return "PING";
    case kFrameTypeSecurity:
      return "SECURITY";
  }
  return absl::StrCat("UNKNOWN(", frame_type, ")");
}
}  // namespace

std::string Http2FrameHeader::ToString() const {
  return absl::StrCat("{", Http2FrameTypeString(type), ": flags=", flags,
                      ", stream_id=", stream_id, ", length=", length, "}");
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

Http2StatusOr ParseFramePayload(const Http2FrameHeader& hdr,
                                SliceBuffer payload) {
  CHECK(payload.Length() == hdr.length);
  switch (hdr.type) {
    case kFrameTypeData:
      return ParseDataFrame(hdr, payload);
    case kFrameTypeHeader:
      return ParseHeaderFrame(hdr, payload);
    case kFrameTypeContinuation:
      return ParseContinuationFrame(hdr, payload);
    case kFrameTypeRstStream:
      return ParseRstStreamFrame(hdr, payload);
    case kFrameTypeSettings:
      return ParseSettingsFrame(hdr, payload);
    case kFrameTypePing:
      return ParsePingFrame(hdr, payload);
    case kFrameTypeGoaway:
      return ParseGoawayFrame(hdr, payload);
    case kFrameTypeWindowUpdate:
      return ParseWindowUpdateFrame(hdr, payload);
    case kFrameTypePushPromise:
      return Http2Error::ProtocolConnectionError(kNoPushPromise);
    case kFrameTypeSecurity:
      return ParseSecurityFrame(hdr, payload);
    default:
      return Http2UnknownFrame{};
  }
}

GrpcMessageHeader ExtractGrpcHeader(SliceBuffer& payload) {
  CHECK_GE(payload.Length(), kGrpcHeaderSizeInBytes);
  uint8_t buffer[kGrpcHeaderSizeInBytes];
  payload.MoveFirstNBytesIntoBuffer(kGrpcHeaderSizeInBytes, buffer);
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
