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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_H
#define GRPC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_H

#include <grpc/support/port_platform.h>

#include <cstdint>

#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {

class FrameSerializer {
 public:
  explicit FrameSerializer(FrameType type, BitSet<3> flags);
  // If called, must be called before AddMessage, AddTrailers, Finish
  SliceBuffer& AddHeaders();
  // If called, must be called before AddTrailers, Finish
  SliceBuffer& AddMessage();
  // If called, must be called before Finish
  SliceBuffer& AddTrailers();

  SliceBuffer Finish();

 private:
  FrameHeader header_;
};

class FrameDeserializer {
 public:
  FrameDeserializer(const FrameHeader& header, const SliceBuffer& slice_buffer);
  const FrameHeader& header() const { return header_; }
  // If called, must be called before ReceiveMessage, ReceiveTrailers
  SliceBuffer ReceiveHeaders();
  // If called, must be called before ReceiveTrailers
  SliceBuffer ReceiveMessage();
  // If called, must be called before Finish
  SliceBuffer ReceiveTrailers();

  absl::Status Finish();

 private:
  FrameHeader header_;
};

class FrameInterface {
 public:
  virtual absl::Status Deserialize(HPackParser* parser,
                                   const FrameHeader& header,
                                   const SliceBuffer& slice_buffer) = 0;
  virtual SliceBuffer Serialize(HPackCompressor* encoder) const = 0;

 protected:
  ~FrameInterface() = default;
};

struct SettingsFrame final : public FrameInterface {
  absl::Status Deserialize(HPackParser* parser, const FrameHeader& header,
                           const SliceBuffer& slice_buffer) override;
  SliceBuffer Serialize(HPackCompressor* encoder) const override;
};

struct ClientFragmentFrame final : public FrameInterface {
  absl::Status Deserialize(HPackParser* parser, const FrameHeader& header,
                           const SliceBuffer& slice_buffer) override;
  SliceBuffer Serialize(HPackCompressor* encoder) const override;

  ClientMetadataHandle headers;
  MessageHandle message;
  bool end_of_stream;
};

struct ServerFragmentFrame final : public FrameInterface {
  absl::Status Deserialize(HPackParser* parser, const FrameHeader& header,
                           const SliceBuffer& slice_buffer) override;
  SliceBuffer Serialize(HPackCompressor* encoder) const override;

  ServerMetadataHandle headers;
  MessageHandle message;
  ServerMetadataHandle trailers;
};

struct CancelFrame final : public FrameInterface {
  absl::Status Deserialize(HPackParser* parser, const FrameHeader& header,
                           const SliceBuffer& slice_buffer) override;
  SliceBuffer Serialize(HPackCompressor* encoder) const override;
};

using ClientFrame = absl::variant<ClientFragmentFrame, CancelFrame>;
using ServerFrame = absl::variant<ServerFragmentFrame>;

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_H
