// Copyright 2015 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_H

#include <grpc/support/port_platform.h>

#include <cstdint>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"

#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {

struct Http2DataFrame {
  uint32_t stream_id = 0;
  bool end_stream = false;
  SliceBuffer payload;
};

struct Http2HeaderFrame {
  uint32_t stream_id = 0;
  bool end_headers = false;
  bool end_stream = false;
  SliceBuffer payload;
};

struct Http2ContinuationFrame {
  uint32_t stream_id = 0;
  bool end_headers = false;
  SliceBuffer payload;
};

struct Http2RstStreamFrame {
  uint32_t stream_id = 0;
  uint32_t error_code = 0;
};

struct Http2SettingsFrame {
  struct Setting {
    uint16_t id;
    uint32_t value;
  };
  bool ack = false;
  std::vector<Setting> settings;
};

struct Http2PingFrame {
  uint32_t stream_id = 0;
  bool ack = false;
  uint64_t opaque = 0;
};

struct Http2GoawayFrame {
  uint32_t last_stream_id = 0;
  uint32_t error_code = 0;
  Slice debug_data;
};

struct Http2WindowUpdateFrame {
  uint32_t stream_id;
  uint32_t increment;
};

struct Http2FrameHeader {
  uint32_t length;
  uint8_t type;
  uint8_t flags;
  uint32_t stream_id;
  // Serialize header to 9 byte long buffer output
  void Serialize(uint8_t* output);
  // Parse header from 9 byte long buffer input
  static Http2FrameHeader Parse(const uint8_t* input);
  std::string ToString() const;
};

struct Http2UnknownFrame {};

using Http2Frame =
    absl::variant<Http2DataFrame, Http2HeaderFrame, Http2ContinuationFrame,
                  Http2RstStreamFrame, Http2SettingsFrame, Http2PingFrame,
                  Http2GoawayFrame, Http2WindowUpdateFrame, Http2UnknownFrame>;

absl::StatusOr<Http2Frame> ParseFramePayload(const Http2FrameHeader& hdr,
                                             SliceBuffer& payload);

// Serialize frame to out, leaves frames in an unknown state (may move things
// out of frames)
void Serialize(absl::Span<Http2Frame> frames, SliceBuffer& out);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FRAME_H
