
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_FRAME_HEADER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_FRAME_HEADER_H

#include "absl/strings/str_cat.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"

namespace grpc_core::chaotic_good {

inline uint32_t DataConnectionPadding(uint32_t payload_length,
                                      uint32_t alignment) {
  if (payload_length % alignment == 0) return 0;
  uint32_t padding = alignment - (payload_length % alignment);
  DCHECK_GT(padding, 0u);
  return padding;
}

struct TcpFrameHeader {
  // Frame header size is fixed.
  enum { kFrameHeaderSize = 24 };

  FrameHeader header;
  // if 0 ==> this frames payload will be on the control channel
  // otherwise ==> a data frame will be sent on a data channel with a matching
  // tag.
  uint64_t payload_tag = 0;

  // Parses a frame header from a buffer of kFrameHeaderSize bytes. All
  // kFrameHeaderSize bytes are consumed.
  static absl::StatusOr<TcpFrameHeader> Parse(const uint8_t* data);
  // Serializes a frame header into a buffer of kFrameHeaderSize bytes.
  void Serialize(uint8_t* data) const;

  // Report contents as a string
  std::string ToString() const;

  bool operator==(const TcpFrameHeader& h) const {
    return header == h.header && payload_tag == h.payload_tag;
  }

  // Required padding to maintain alignment.
  uint32_t Padding(uint32_t alignment) const;
};

struct TcpDataFrameHeader {
  enum { kFrameHeaderSize = 12 };
  uint64_t payload_tag;
  uint32_t payload_length;

  // Parses a frame header from a buffer of kFrameHeaderSize bytes. All
  // kFrameHeaderSize bytes are consumed.
  static absl::StatusOr<TcpDataFrameHeader> Parse(const uint8_t* data);
  // Serializes a frame header into a buffer of kFrameHeaderSize bytes.
  void Serialize(uint8_t* data) const;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const TcpDataFrameHeader& frame) {
    sink.Append(absl::StrCat("DataFrameHeader{payload_tag:", frame.payload_tag,
                             ",payload_length:", frame.payload_length, "}"));
  }
};

}  // namespace grpc_core::chaotic_good

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_FRAME_HEADER_H
