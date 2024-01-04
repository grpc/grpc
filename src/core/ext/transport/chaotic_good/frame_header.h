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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_HEADER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_HEADER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <cstdint>

#include "absl/status/statusor.h"

#include "src/core/lib/gprpp/bitset.h"

namespace grpc_core {
namespace chaotic_good {

enum class FrameType : uint8_t {
  kSettings = 0x00,
  kFragment = 0x80,
  kCancel = 0x81,
};

struct FrameHeader {
  FrameType type = FrameType::kCancel;
  BitSet<2> flags;
  uint32_t stream_id = 0;
  uint32_t header_length = 0;
  uint32_t message_length = 0;
  uint32_t message_padding = 0;
  uint32_t trailer_length = 0;

  // Parses a frame header from a buffer of 24 bytes. All 24 bytes are consumed.
  static absl::StatusOr<FrameHeader> Parse(const uint8_t* data);
  // Serializes a frame header into a buffer of 24 bytes.
  void Serialize(uint8_t* data) const;
  // Compute frame sizes from the header.
  uint32_t GetFrameLength() const;
  // Report contents as a string
  std::string ToString() const;

  bool operator==(const FrameHeader& h) const {
    return type == h.type && flags == h.flags && stream_id == h.stream_id &&
           header_length == h.header_length &&
           message_length == h.message_length &&
           message_padding == h.message_padding &&
           trailer_length == h.trailer_length;
  }
  // Frame header size is fixed to 24 bytes.
  static constexpr size_t frame_header_size_ = 24;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_HEADER_H
