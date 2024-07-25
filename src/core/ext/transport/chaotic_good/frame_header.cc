// Copyright 2022 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/frame_header.h"

#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

namespace grpc_core {
namespace chaotic_good {

namespace {
void WriteLittleEndianUint32(uint32_t value, uint8_t* data) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
  data[2] = static_cast<uint8_t>(value >> 16);
  data[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t ReadLittleEndianUint32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}
}  // namespace

// Serializes a frame header into a buffer of 24 bytes.
void FrameHeader::Serialize(uint8_t* data) const {
  WriteLittleEndianUint32(
      static_cast<uint32_t>(type) | (flags.ToInt<uint32_t>() << 8), data);
  WriteLittleEndianUint32(stream_id, data + 4);
  WriteLittleEndianUint32(header_length, data + 8);
  WriteLittleEndianUint32(message_length, data + 12);
  WriteLittleEndianUint32(message_padding, data + 16);
  WriteLittleEndianUint32(trailer_length, data + 20);
}

// Parses a frame header from a buffer of 24 bytes. All 24 bytes are consumed.
absl::StatusOr<FrameHeader> FrameHeader::Parse(const uint8_t* data) {
  FrameHeader header;
  const uint32_t type_and_flags = ReadLittleEndianUint32(data);
  header.type = static_cast<FrameType>(type_and_flags & 0xff);
  const uint32_t flags = type_and_flags >> 8;
  if (flags > 7) return absl::InvalidArgumentError("Invalid flags");
  header.flags = BitSet<3>::FromInt(flags);
  header.stream_id = ReadLittleEndianUint32(data + 4);
  header.header_length = ReadLittleEndianUint32(data + 8);
  header.message_length = ReadLittleEndianUint32(data + 12);
  header.message_padding = ReadLittleEndianUint32(data + 16);
  header.trailer_length = ReadLittleEndianUint32(data + 20);
  return header;
}

uint32_t FrameHeader::GetFrameLength() const {
  // In chaotic-good transport design, message and message padding are sent
  // through different channel. So not included in the frame length calculation.
  uint32_t frame_length = header_length + trailer_length;
  return frame_length;
}

std::string FrameHeader::ToString() const {
  return absl::StrFormat(
      "[type=0x%02x, flags=0x%02x, stream_id=%d, header_length=%d, "
      "message_length=%d, message_padding=%d, trailer_length=%d]",
      static_cast<uint8_t>(type), flags.ToInt<uint8_t>(), stream_id,
      header_length, message_length, message_padding, trailer_length);
}

}  // namespace chaotic_good
}  // namespace grpc_core
