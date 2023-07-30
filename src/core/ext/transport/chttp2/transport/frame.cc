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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/frame.h"

#include <cstdint>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/match.h"

namespace grpc_core {

namespace {

constexpr uint8_t kFrameTypeData = 0;
constexpr uint8_t kFrameTypeHeader = 1;
constexpr uint8_t kFrameTypeContinuation = 9;
constexpr uint8_t kFrameTypeRstStream = 3;
constexpr uint8_t kFrameTypeSettings = 4;
constexpr uint8_t kFrameTypePing = 6;
constexpr uint8_t kFrameTypeGoaway = 7;
constexpr uint8_t kFrameTypeWindowUpdate = 8;

constexpr uint8_t kFlagEndStream = 1;
constexpr uint8_t kFlagAck = 1;
constexpr uint8_t kFlagEndHeaders = 4;
constexpr uint8_t kFlagPadded = 8;
constexpr uint8_t kFlagPriority = 0x20;

constexpr size_t kFrameHeaderSize = 9;

void Write2b(uint16_t x, uint8_t* output) {
  output[0] = static_cast<uint8_t>(x >> 8);
  output[1] = static_cast<uint8_t>(x);
}

uint16_t Read2b(const uint8_t* input) {
  return static_cast<uint16_t>(input[0]) << 8 | static_cast<uint16_t>(input[1]);
}

void Write3b(uint32_t x, uint8_t* output) {
  GPR_ASSERT(x < 16777216);
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
  size_t operator()(const Http2UnknownFrame&) { Crash("unreachable"); }
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
        static_cast<uint32_t>(frame.payload.Length()), kFrameTypeHeader,
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

  void operator()(Http2UnknownFrame&) { Crash("unreachable"); }

 private:
  SliceBuffer& out_;
  MutableSlice extra_bytes_;
};

absl::Status StripPadding(SliceBuffer& payload) {
  if (payload.Length() < 1) {
    return absl::InternalError("padding flag set but no padding byte");
  }
  uint8_t padding_bytes;
  payload.MoveFirstNBytesIntoBuffer(1, &padding_bytes);
  if (payload.Length() < padding_bytes) {
    return absl::InternalError("padding flag set but not enough padding bytes");
  }
  payload.RemoveLastNBytes(padding_bytes);
  return absl::OkStatus();
}

absl::StatusOr<Http2DataFrame> ParseDataFrame(const Http2FrameHeader& hdr,
                                              SliceBuffer& payload) {
  if (hdr.flags & ~(kFlagEndStream | kFlagPadded)) {
    return absl::InternalError(
        absl::StrCat("unsupported data flags: ", hdr.ToString()));
  }

  if (hdr.stream_id == 0) {
    return absl::InternalError(
        absl::StrCat("invalid stream id: ", hdr.ToString()));
  }

  if (hdr.flags & kFlagPadded) {
    auto s = StripPadding(payload);
    if (!s.ok()) return s;
  }

  return Http2DataFrame{hdr.stream_id, ExtractFlag(hdr.flags, kFlagEndStream),
                        std::move(payload)};
}

absl::StatusOr<Http2HeaderFrame> ParseHeaderFrame(const Http2FrameHeader& hdr,
                                                  SliceBuffer& payload) {
  if (hdr.flags &
      ~(kFlagEndHeaders | kFlagEndStream | kFlagPriority | kFlagPadded)) {
    return absl::InternalError(
        absl::StrCat("unsupported header flags: ", hdr.ToString()));
  }

  if (hdr.stream_id == 0) {
    return absl::InternalError(
        absl::StrCat("invalid stream id: ", hdr.ToString()));
  }

  if (hdr.flags & kFlagPriority) {
    if (payload.Length() < 5) {
      return absl::InternalError(
          absl::StrCat("invalid priority payload: ", hdr.ToString()));
    }
    payload.RemoveFirstNBytes(5);
  }

  if (hdr.flags & kFlagPadded) {
    auto s = StripPadding(payload);
    if (!s.ok()) return s;
  }

  return Http2HeaderFrame{
      hdr.stream_id, ExtractFlag(hdr.flags, kFlagEndHeaders),
      ExtractFlag(hdr.flags, kFlagEndStream), std::move(payload)};
}

absl::StatusOr<Http2ContinuationFrame> ParseContinuationFrame(
    const Http2FrameHeader& hdr, SliceBuffer& payload) {
  if (hdr.flags & ~kFlagEndHeaders) {
    return absl::InternalError(
        absl::StrCat("unsupported header flags: ", hdr.ToString()));
  }

  if (hdr.stream_id == 0) {
    return absl::InternalError(
        absl::StrCat("invalid stream id: ", hdr.ToString()));
  }

  return Http2ContinuationFrame{hdr.stream_id,
                                ExtractFlag(hdr.flags, kFlagEndHeaders),
                                std::move(payload)};
}

absl::StatusOr<Http2RstStreamFrame> ParseRstStreamFrame(
    const Http2FrameHeader& hdr, SliceBuffer& payload) {
  if (hdr.flags != 0) {
    return absl::InternalError(
        absl::StrCat("unsupported rst stream flags: ", hdr.ToString()));
  }

  if (payload.Length() != 4) {
    return absl::InternalError(
        absl::StrCat("invalid rst stream payload: ", hdr.ToString()));
  }

  uint8_t buffer[4];
  payload.CopyToBuffer(buffer);

  return Http2RstStreamFrame{hdr.stream_id, Read4b(buffer)};
}

absl::StatusOr<Http2SettingsFrame> ParseSettingsFrame(
    const Http2FrameHeader& hdr, SliceBuffer& payload) {
  if (hdr.flags == kFlagAck) return Http2SettingsFrame{true, {}};
  if (hdr.flags != 0) return absl::InternalError("invalid settings flags");

  if (payload.Length() % 6 != 0) {
    return absl::InternalError(
        absl::StrCat("invalid settings payload: ", hdr.ToString(),
                     " -- settings must be multiples of 6 bytes, got ",
                     payload.Length(), " bytes"));
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

absl::StatusOr<Http2PingFrame> ParsePingFrame(const Http2FrameHeader& hdr,
                                              SliceBuffer& payload) {
  if (payload.Length() != 8) {
    return absl::InternalError(
        absl::StrCat("invalid ping payload: ", hdr.ToString()));
  }

  if (hdr.stream_id != 0) {
    return absl::InternalError(
        absl::StrCat("invalid ping stream id: ", hdr.ToString()));
  }

  bool ack;
  switch (hdr.flags) {
    case 0:
      ack = false;
      break;
    case kFlagAck:
      ack = true;
      break;
    default:
      return absl::InternalError(
          absl::StrCat("invalid ping flags: ", hdr.ToString()));
  }

  uint8_t buffer[8];
  payload.CopyToBuffer(buffer);

  return Http2PingFrame{ack, Read8b(buffer)};
}

absl::StatusOr<Http2GoawayFrame> ParseGoawayFrame(const Http2FrameHeader& hdr,
                                                  SliceBuffer& payload) {
  if (payload.Length() < 8) {
    return absl::InternalError(
        absl::StrCat("invalid goaway payload: ", hdr.ToString(),
                     " -- must be at least 8 bytes"));
  }

  if (hdr.stream_id != 0) {
    return absl::InternalError(
        absl::StrCat("invalid goaway stream id: ", hdr.ToString()));
  }

  if (hdr.flags != 0) {
    return absl::InternalError(
        absl::StrCat("invalid goaway flags: ", hdr.ToString()));
  }

  uint8_t buffer[8];
  payload.MoveFirstNBytesIntoBuffer(8, buffer);
  return Http2GoawayFrame{Read4b(buffer), Read4b(buffer + 4),
                          payload.JoinIntoSlice()};
}

absl::StatusOr<Http2WindowUpdateFrame> ParseWindowUpdateFrame(
    const Http2FrameHeader& hdr, SliceBuffer& payload) {
  if (payload.Length() != 4) {
    return absl::InternalError(
        absl::StrCat("invalid window update payload: ", hdr.ToString()));
  }

  if (hdr.flags != 0) {
    return absl::InternalError(
        absl::StrCat("invalid window update flags: ", hdr.ToString()));
  }

  uint8_t buffer[4];
  payload.CopyToBuffer(buffer);
  return Http2WindowUpdateFrame{hdr.stream_id, Read4b(buffer)};
}

}  // namespace

void Http2FrameHeader::Serialize(uint8_t* output) {
  Write3b(length, output);
  output[3] = type;
  output[4] = flags;
  Write4b(stream_id, output + 5);
}

Http2FrameHeader Http2FrameHeader::Parse(const uint8_t* input) {
  return Http2FrameHeader{Read3b(input), input[3], input[4], Read4b(input + 5)};
}

void Serialize(absl::Span<Http2Frame> frames, SliceBuffer& out) {
  size_t buffer_needed = 0;
  for (auto& frame : frames) {
    // Bytes needed for framing
    buffer_needed += kFrameHeaderSize;
    // Bytes needed for unserialized payload
    buffer_needed += absl::visit(SerializeExtraBytesRequired(), frame);
  }
  SerializeHeaderAndPayload serialize(buffer_needed, out);
  for (auto& frame : frames) {
    absl::visit(serialize, frame);
  }
}

absl::StatusOr<Http2Frame> ParseFramePayload(const Http2FrameHeader& hdr,
                                             SliceBuffer& payload) {
  GPR_ASSERT(payload.Length() == hdr.length);
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
    default:
      return Http2UnknownFrame{};
  }
}

}  // namespace grpc_core
