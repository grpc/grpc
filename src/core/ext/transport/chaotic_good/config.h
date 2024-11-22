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

#ifndef CG_CONFIG_H
#define CG_CONFIG_H

#include "src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.h"
#include "src/core/ext/transport/chaotic_good/chaotic_good_transport.h"
#include "src/core/ext/transport/chaotic_good/message_chunker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/extensions/tcp_trace.h"

namespace grpc_core {
namespace chaotic_good {

class Config {
 public:
  explicit Config(const ChannelArgs& channel_args) {
    decode_alignment_ = channel_args.GetInt("grpc.chaotic_good.alignment")
                            .value_or(decode_alignment_);
    max_recv_chunk_size_ =
        channel_args.GetInt("grpc.chaotic_good.recv_chunk_size")
            .value_or(max_recv_chunk_size_);
    inline_payload_size_threshold_ =
        channel_args.GetInt("grpc.chaotic_good.inlined_payload_size_threshold")
            .value_or(inline_payload_size_threshold_);
    tracing_enabled_ =
        channel_args.GetBool(GRPC_ARG_TCP_TRACING_ENABLED).value_or(false);
  }

  void PrepareOutgoingSettings(chaotic_good_frame::Settings& settings) const {
    settings.set_alignment(decode_alignment_);
    settings.set_max_chunk_size(max_recv_chunk_size_);
  }

  absl::Status ReceiveIncomingSettings(
      const chaotic_good_frame::Settings& settings) {
    if (settings.alignment() != 0) encode_alignment_ = settings.alignment();
    max_send_chunk_size_ =
        std::min(max_send_chunk_size_, settings.max_chunk_size());
    if (settings.max_chunk_size() == 0) {
      max_recv_chunk_size_ = 0;
    }
    return absl::OkStatus();
  }

  ChaoticGoodTransport::Options MakeTransportOptions() const {
    ChaoticGoodTransport::Options options;
    options.encode_alignment = encode_alignment_;
    options.decode_alignment = decode_alignment_;
    options.inlined_payload_size_threshold = inline_payload_size_threshold_;
    return options;
  }

  MessageChunker MakeMessageChunker() const {
    return MessageChunker(max_send_chunk_size_, encode_alignment_);
  }

  bool tracing_enabled() const { return tracing_enabled_; }

  void TestOnlySetChunkSizes(uint32_t size) {
    max_send_chunk_size_ = size;
    max_recv_chunk_size_ = size;
  }

 private:
  bool tracing_enabled_ = false;
  uint32_t encode_alignment_ = 64;
  uint32_t decode_alignment_ = 64;
  uint32_t max_send_chunk_size_ = 1024 * 1024;
  uint32_t max_recv_chunk_size_ = 1024 * 1024;
  uint32_t inline_payload_size_threshold_ = 8 * 1024;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif
