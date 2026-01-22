// Copyright 2025 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/tcp_frame_header.h"

#include "src/core/ext/transport/chaotic_good/serialize_little_endian.h"

namespace grpc_core::chaotic_good {

///////////////////////////////////////////////////////////////////////////////
// TcpFrameHeader

// Serializes a frame header into a buffer of 24 bytes.
void TcpFrameHeader::Serialize(uint8_t* data) const {
  DCHECK_EQ(payload_tag >> 56, 0u) << payload_tag;
  WriteLittleEndianUint64(
      static_cast<uint64_t>(header.type) | (payload_tag << 8), data);
  WriteLittleEndianUint32(header.stream_id, data + 8);
  WriteLittleEndianUint32(header.payload_length, data + 12);
}

// Parses a frame header from a buffer.
absl::StatusOr<TcpFrameHeader> TcpFrameHeader::Parse(const uint8_t* data) {
  TcpFrameHeader tcp_header;
  const uint64_t type_and_tag = ReadLittleEndianUint64(data);
  tcp_header.header.type = static_cast<FrameType>(type_and_tag & 0xff);
  tcp_header.payload_tag = type_and_tag >> 8;
  tcp_header.header.stream_id = ReadLittleEndianUint32(data + 8);
  tcp_header.header.payload_length = ReadLittleEndianUint32(data + 12);
  return tcp_header;
}

uint32_t TcpFrameHeader::Padding(uint32_t alignment) const {
  if (payload_tag == 0) return 0;
  return DataConnectionPadding(kFrameHeaderSize + header.payload_length,
                               alignment);
}

std::string TcpFrameHeader::ToString() const {
  return absl::StrCat(header.ToString(), "@", payload_tag);
}

///////////////////////////////////////////////////////////////////////////////
// TcpDataFrameHeader

void TcpDataFrameHeader::Serialize(uint8_t* data) const {
  WriteLittleEndianUint64(payload_tag, data);
  WriteLittleEndianUint64(send_timestamp, data + 8);
  WriteLittleEndianUint32(payload_length, data + 16);
}

absl::StatusOr<TcpDataFrameHeader> TcpDataFrameHeader::Parse(
    const uint8_t* data) {
  TcpDataFrameHeader header;
  header.payload_tag = ReadLittleEndianUint64(data);
  header.send_timestamp = ReadLittleEndianUint64(data + 8);
  header.payload_length = ReadLittleEndianUint32(data + 16);
  return header;
}

}  // namespace grpc_core::chaotic_good