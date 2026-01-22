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

#include "src/core/ext/transport/chaotic_good_legacy/frame_header.h"

#include <grpc/support/port_platform.h>

#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {
namespace chaotic_good_legacy {

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
  WriteLittleEndianUint32((static_cast<uint32_t>(type) << 16) |
                              static_cast<uint32_t>(payload_connection_id),
                          data);
  WriteLittleEndianUint32(stream_id, data + 4);
  WriteLittleEndianUint32(payload_length, data + 8);
}

// Parses a frame header from a buffer of 24 bytes. All 24 bytes are consumed.
absl::StatusOr<FrameHeader> FrameHeader::Parse(const uint8_t* data) {
  FrameHeader header;
  const uint32_t type_and_conn_id = ReadLittleEndianUint32(data);
  if (type_and_conn_id & 0xff000000u) {
    return absl::InternalError("Non-zero reserved byte received");
  }
  header.type = static_cast<FrameType>(type_and_conn_id >> 16);
  header.payload_connection_id = type_and_conn_id & 0xffff;
  header.stream_id = ReadLittleEndianUint32(data + 4);
  header.payload_length = ReadLittleEndianUint32(data + 8);
  return header;
}

std::string FrameHeader::ToString() const {
  return absl::StrCat("[type:", type, " conn:", payload_connection_id,
                      " stream_id:", stream_id,
                      " payload_length:", payload_length, "]");
}

std::string FrameTypeString(FrameType type) {
  switch (type) {
    case FrameType::kSettings:
      return "Settings";
    case FrameType::kClientInitialMetadata:
      return "ClientInitialMetadata";
    case FrameType::kClientEndOfStream:
      return "ClientEndOfStream";
    case FrameType::kMessage:
      return "Message";
    case FrameType::kServerInitialMetadata:
      return "ServerInitialMetadata";
    case FrameType::kServerTrailingMetadata:
      return "ServerTrailingMetadata";
    case FrameType::kCancel:
      return "Cancel";
    case FrameType::kBeginMessage:
      return "BeginMessage";
    case FrameType::kMessageChunk:
      return "MessageChunk";
  }
  return absl::StrCat("Unknown[", static_cast<int>(type), "]");
}

}  // namespace chaotic_good_legacy
}  // namespace grpc_core
