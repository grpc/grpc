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

#include <cstdint>

#include "frame.h"

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

void Write3b(uint32_t x, uint8_t* output) {
  GPR_ASSERT(x < 16777216);
  output[0] = static_cast<uint8_t>(x >> 16);
  output[1] = static_cast<uint8_t>(x >> 8);
  output[2] = static_cast<uint8_t>(x);
}

void Write4b(uint32_t x, uint8_t* output) {
  output[0] = static_cast<uint8_t>(x >> 24);
  output[1] = static_cast<uint8_t>(x >> 16);
  output[2] = static_cast<uint8_t>(x >> 8);
  output[3] = static_cast<uint8_t>(x);
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

void SerializeFrameHeader(uint32_t length, uint8_t type, uint8_t flags,
                          uint32_t stream_id, uint8_t* output) {
  Write3b(length, output);
  output[3] = type;
  output[4] = flags;
  Write4b(stream_id, output + 5);
}

class SerializeExtraBytesRequired {
 public:
  size_t operator()(const Http2DataFrame&) { return 0; }
  size_t operator()(const Http2HeaderFrame&) { return 0; }
  size_t operator()(const Http2RstStreamFrame&) { return 4; }
  size_t operator()(const Http2SettingsFrame& f) {
    return 6 * f.settings.size();
  }
  size_t operator()(const Http2PingFrame&) { return 8; }
  size_t operator()(const Http2GoawayFrame&) { return 8; }
  size_t operator()(const Http2WindowUpdateFrame&) { return 4; }
};

class SerializeHeaderAndPayload {
 public:
  SerializeHeaderAndPayload(size_t extra_bytes, SliceBuffer& out)
      : out_(out),
        extra_bytes_(MutableSlice::CreateUninitialized(extra_bytes)) {}

  void operator()(Http2DataFrame& frame) {
    auto hdr = extra_bytes_.TakeFirst(kFrameHeaderSize);
    SerializeFrameHeader(frame.payload.Length(), kFrameTypeData,
                         frame.end_stream ? kFlagEndStream : 0, frame.stream_id,
                         hdr.begin());
    out_.AppendIndexed(Slice(std::move(hdr)));
    out_.TakeAndAppend(frame.payload);
  }

  void operator()(Http2HeaderFrame& frame) {
    auto hdr = extra_bytes_.TakeFirst(kFrameHeaderSize);
    uint8_t flags;
    switch (frame.boundary) {
      case Http2HeaderFrame::Boundary::None:
        flags = 0;
        break;
      case Http2HeaderFrame::Boundary::EndOfHeaders:
        flags = kFlagEndHeaders;
        break;
      case Http2HeaderFrame::Boundary::EndOfStream:
        flags = kFlagEndStream;
        break;
    }
    SerializeFrameHeader(
        frame.payload.Length(),
        frame.is_continuation ? kFrameTypeContinuation : kFrameTypeHeader,
        flags, frame.stream_id, hdr.begin());
    out_.AppendIndexed(Slice(std::move(hdr)));
    out_.TakeAndAppend(frame.payload);
  }

  void operator()(Http2RstStreamFrame& frame) {
    auto hdr_and_payload = extra_bytes_.TakeFirst(kFrameHeaderSize + 4);
    SerializeFrameHeader(4, kFrameTypeRstStream, 0, frame.stream_id,
                         hdr_and_payload.begin());
    Write4b(frame.error_code, hdr_and_payload.begin() + kFrameHeaderSize);
    out_.AppendIndexed(Slice(std::move(hdr_and_payload)));
  }

  void operator()(Http2SettingsFrame& frame) {
    const size_t payload_size = 6 * frame.settings.size();
    auto hdr_and_payload =
        extra_bytes_.TakeFirst(kFrameHeaderSize + payload_size);
    SerializeFrameHeader(payload_size, kFrameTypeSettings,
                         frame.ack ? kFlagAck : 0, 0, hdr_and_payload.begin());
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
    SerializeFrameHeader(8, kFrameTypePing, frame.ack ? kFlagAck : 0, 0,
                         hdr_and_payload.begin());
    Write8b(frame.opaque, hdr_and_payload.begin() + kFrameHeaderSize);
    out_.AppendIndexed(Slice(std::move(hdr_and_payload)));
  }

  void operator()(Http2GoawayFrame& frame) {
    auto hdr_and_fixed_payload = extra_bytes_.TakeFirst(kFrameHeaderSize + 8);
    SerializeFrameHeader(8 + frame.debug_data.length(), kFrameTypeGoaway, 0, 0,
                         hdr_and_fixed_payload.begin());
    Write4b(frame.last_stream_id,
            hdr_and_fixed_payload.begin() + kFrameHeaderSize);
    Write4b(frame.error_code,
            hdr_and_fixed_payload.begin() + kFrameHeaderSize + 4);
    out_.AppendIndexed(Slice(std::move(hdr_and_fixed_payload)));
    out_.AppendIndexed(std::move(frame.debug_data));
  }

  void operator()(Http2WindowUpdateFrame& frame) {
    auto hdr_and_payload = extra_bytes_.TakeFirst(kFrameHeaderSize + 4);
    SerializeFrameHeader(4, kFrameTypeWindowUpdate, 0, frame.stream_id,
                         hdr_and_payload.begin());
    Write4b(frame.increment, hdr_and_payload.begin() + kFrameHeaderSize);
    out_.AppendIndexed(Slice(std::move(hdr_and_payload)));
  }

 private:
  SliceBuffer& out_;
  MutableSlice extra_bytes_;
};

}  // namespace

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

}  // namespace grpc_core
