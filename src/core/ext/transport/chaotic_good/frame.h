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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/types/variant.h"

#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {

class FrameInterface {
 public:
  virtual absl::Status Deserialize(HPackParser* parser,
                                   const FrameHeader& header,
                                   SliceBuffer& slice_buffer) = 0;
  virtual SliceBuffer Serialize(HPackCompressor* encoder) const = 0;

 protected:
  static bool EqVal(const Message& a, const Message& b) {
    return a.payload()->JoinIntoString() == b.payload()->JoinIntoString() &&
           a.flags() == b.flags();
  }
  static bool EqVal(const grpc_metadata_batch& a,
                    const grpc_metadata_batch& b) {
    return a.DebugString() == b.DebugString();
  }
  template <typename T>
  static bool EqHdl(const Arena::PoolPtr<T>& a, const Arena::PoolPtr<T>& b) {
    if (a == nullptr && b == nullptr) return true;
    if (a == nullptr || b == nullptr) return false;
    return EqVal(*a, *b);
  }
  ~FrameInterface() = default;
};

struct SettingsFrame final : public FrameInterface {
  absl::Status Deserialize(HPackParser* parser, const FrameHeader& header,
                           SliceBuffer& slice_buffer) override;
  SliceBuffer Serialize(HPackCompressor* encoder) const override;

  bool operator==(const SettingsFrame&) const { return true; }
};

struct ClientFragmentFrame final : public FrameInterface {
  absl::Status Deserialize(HPackParser* parser, const FrameHeader& header,
                           SliceBuffer& slice_buffer) override;
  SliceBuffer Serialize(HPackCompressor* encoder) const override;

  uint32_t stream_id;
  ClientMetadataHandle headers;
  bool end_of_stream = false;

  bool operator==(const ClientFragmentFrame& other) const {
    return stream_id == other.stream_id && EqHdl(headers, other.headers) &&
           end_of_stream == other.end_of_stream;
  }
};

struct ServerFragmentFrame final : public FrameInterface {
  absl::Status Deserialize(HPackParser* parser, const FrameHeader& header,
                           SliceBuffer& slice_buffer) override;
  SliceBuffer Serialize(HPackCompressor* encoder) const override;

  uint32_t stream_id;
  ServerMetadataHandle headers;
  ServerMetadataHandle trailers;

  bool operator==(const ServerFragmentFrame& other) const {
    return stream_id == other.stream_id && EqHdl(headers, other.headers) &&
           EqHdl(trailers, other.trailers);
  }
};

struct CancelFrame final : public FrameInterface {
  absl::Status Deserialize(HPackParser* parser, const FrameHeader& header,
                           SliceBuffer& slice_buffer) override;
  SliceBuffer Serialize(HPackCompressor* encoder) const override;

  uint32_t stream_id;

  bool operator==(const CancelFrame& other) const {
    return stream_id == other.stream_id;
  }
};

using ClientFrame = absl::variant<ClientFragmentFrame, CancelFrame>;
using ServerFrame = absl::variant<ServerFragmentFrame>;

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_H
