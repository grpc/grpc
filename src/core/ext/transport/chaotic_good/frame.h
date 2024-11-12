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
#include <string>

#include "absl/random/bit_gen_ref.h"
#include "absl/status/status.h"
#include "absl/types/variant.h"
#include "src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/message.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/util/match.h"

namespace grpc_core {
namespace chaotic_good {

struct BufferPair {
  SliceBuffer control;
  SliceBuffer data;
};

class FrameInterface {
 public:
  virtual absl::Status Deserialize(const FrameHeader& header,
                                   SliceBuffer payload) = 0;
  virtual FrameHeader MakeHeader() const = 0;
  virtual void SerializePayload(SliceBuffer& payload) const = 0;
  virtual std::string ToString() const = 0;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const FrameInterface& frame) {
    sink.Append(frame.ToString());
  }

 protected:
  ~FrameInterface() = default;
};

inline std::ostream& operator<<(std::ostream& os, const FrameInterface& frame) {
  return os << frame.ToString();
}

chaotic_good_frame::ClientMetadata ClientMetadataProtoFromGrpc(
    const ClientMetadata& md);
absl::StatusOr<ClientMetadataHandle> ClientMetadataGrpcFromProto(
    chaotic_good_frame::ClientMetadata& metadata);
chaotic_good_frame::ServerMetadata ServerMetadataProtoFromGrpc(
    const ServerMetadata& md);
absl::StatusOr<ServerMetadataHandle> ServerMetadataGrpcFromProto(
    chaotic_good_frame::ServerMetadata& metadata);

struct SettingsFrame final : public FrameInterface {
  absl::Status Deserialize(const FrameHeader& header,
                           SliceBuffer payload) override;
  FrameHeader MakeHeader() const override;
  void SerializePayload(SliceBuffer& payload) const override;
  std::string ToString() const override;

  chaotic_good_frame::Settings settings;
};

struct ClientInitialMetadataFrame final : public FrameInterface {
  absl::Status Deserialize(const FrameHeader& header,
                           SliceBuffer payload) override;
  FrameHeader MakeHeader() const override;
  void SerializePayload(SliceBuffer& payload) const override;
  std::string ToString() const override;

  uint32_t stream_id;
  chaotic_good_frame::ClientMetadata headers;
};

struct MessageFrame final : public FrameInterface {
  absl::Status Deserialize(const FrameHeader& header,
                           SliceBuffer payload) override;
  FrameHeader MakeHeader() const override;
  void SerializePayload(SliceBuffer& payload) const override;
  std::string ToString() const override;

  uint32_t stream_id;
  MessageHandle message;
};

struct ClientEndOfStream final : public FrameInterface {
  absl::Status Deserialize(const FrameHeader& header,
                           SliceBuffer payload) override;
  FrameHeader MakeHeader() const override;
  void SerializePayload(SliceBuffer& payload) const override;
  std::string ToString() const override;

  uint32_t stream_id;
};

struct ServerInitialMetadataFrame final : public FrameInterface {
  absl::Status Deserialize(const FrameHeader& header,
                           SliceBuffer payload) override;
  FrameHeader MakeHeader() const override;
  void SerializePayload(SliceBuffer& payload) const override;
  std::string ToString() const override;

  uint32_t stream_id;
  chaotic_good_frame::ServerMetadata headers;
};

struct ServerTrailingMetadataFrame final : public FrameInterface {
  absl::Status Deserialize(const FrameHeader& header,
                           SliceBuffer payload) override;
  FrameHeader MakeHeader() const override;
  void SerializePayload(SliceBuffer& payload) const override;
  std::string ToString() const override;

  uint32_t stream_id;
  chaotic_good_frame::ServerMetadata trailers;
};

struct CancelFrame final : public FrameInterface {
  CancelFrame() = default;
  explicit CancelFrame(uint32_t stream_id) : stream_id(stream_id) {}

  absl::Status Deserialize(const FrameHeader& header,
                           SliceBuffer payload) override;
  FrameHeader MakeHeader() const override;
  void SerializePayload(SliceBuffer& payload) const override;
  std::string ToString() const override;

  uint32_t stream_id;
};

using ClientFrame = absl::variant<ClientInitialMetadataFrame, MessageFrame,
                                  ClientEndOfStream, CancelFrame>;
using ServerFrame = absl::variant<ServerInitialMetadataFrame, MessageFrame,
                                  ServerTrailingMetadataFrame>;

inline FrameInterface& GetFrameInterface(ClientFrame& frame) {
  return MatchMutable(
      &frame,
      [](ClientInitialMetadataFrame* frame) -> FrameInterface& {
        return *frame;
      },
      [](MessageFrame* frame) -> FrameInterface& { return *frame; },
      [](ClientEndOfStream* frame) -> FrameInterface& { return *frame; },
      [](CancelFrame* frame) -> FrameInterface& { return *frame; });
}

inline FrameInterface& GetFrameInterface(ServerFrame& frame) {
  return MatchMutable(
      &frame,
      [](ServerInitialMetadataFrame* frame) -> FrameInterface& {
        return *frame;
      },
      [](MessageFrame* frame) -> FrameInterface& { return *frame; },
      [](ServerTrailingMetadataFrame* frame) -> FrameInterface& {
        return *frame;
      });
}

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_H
