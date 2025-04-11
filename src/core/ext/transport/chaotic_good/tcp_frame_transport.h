// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_FRAME_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_FRAME_TRANSPORT_H

#include <vector>

#include "src/core/ext/transport/chaotic_good/control_endpoint.h"
#include "src/core/ext/transport/chaotic_good/data_endpoints.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chaotic_good/frame_transport.h"
#include "src/core/ext/transport/chaotic_good/pending_connection.h"
#include "src/core/lib/promise/inter_activity_latch.h"

namespace grpc_core {
namespace chaotic_good {

struct TcpFrameHeader {
  // Frame header size is fixed.
  enum { kFrameHeaderSize = 16 };

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

inline std::vector<PromiseEndpoint> OneDataEndpoint(PromiseEndpoint endpoint) {
  std::vector<PromiseEndpoint> ep;
  ep.emplace_back(std::move(endpoint));
  return ep;
}

class TcpFrameTransport final : public FrameTransport {
 public:
  struct Options {
    uint32_t encode_alignment = 64;
    uint32_t decode_alignment = 64;
    uint32_t inlined_payload_size_threshold = 8 * 1024;
    bool enable_tracing = false;
  };

  TcpFrameTransport(
      Options options, PromiseEndpoint control_endpoint,
      std::vector<PendingConnection> pending_data_endpoints,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine,
      std::shared_ptr<GlobalStatsPluginRegistry::StatsPluginGroup>
          stats_plugin_group);

  void Start(Party* party, MpscReceiver<Frame> outgoing_frames,
             RefCountedPtr<FrameTransportSink> sink) override;
  void Orphan() override;

 private:
  auto WriteFrame(const FrameInterface& frame);
  auto WriteLoop(MpscReceiver<Frame> frames);
  // Read frame header and payloads for control and data portions of one frame.
  // Resolves to StatusOr<IncomingFrame>.
  auto ReadFrameBytes();
  template <typename Promise>
  auto UntilClosed(Promise promise);

  ControlEndpoint control_endpoint_;
  DataEndpoints data_endpoints_;
  const Options options_;
  InterActivityLatch<void> closed_;
  uint64_t next_payload_tag_ = 1;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_FRAME_TRANSPORT_H
