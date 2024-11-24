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

#define GRPC_ARG_CHAOTIC_GOOD_ALIGNMENT "grpc.chaotic_good.alignment"
#define GRPC_ARG_CHAOTIC_GOOD_MAX_RECV_CHUNK_SIZE \
  "grpc.chaotic_good.max_recv_chunk_size"
#define GRPC_ARG_CHAOTIC_GOOD_MAX_SEND_CHUNK_SIZE \
  "grpc.chaotic_good.max_send_chunk_size"
#define GRPC_ARG_CHAOTIC_GOOD_INLINED_PAYLOAD_SIZE_THRESHOLD \
  "grpc.chaotic_good.inlined_payload_size_threshold"

// Transport configuration.
// Most of our configuration is derived from channel args, and then exchanged
// via settings frames to define a final shared configuration between client and
// server.
class Config {
 public:
  explicit Config(const ChannelArgs& channel_args) {
    decode_alignment_ =
        std::max(1, channel_args.GetInt(GRPC_ARG_CHAOTIC_GOOD_ALIGNMENT)
                        .value_or(decode_alignment_));
    max_recv_chunk_size_ = std::max(
        0, channel_args.GetInt(GRPC_ARG_CHAOTIC_GOOD_MAX_RECV_CHUNK_SIZE)
               .value_or(max_recv_chunk_size_));
    max_send_chunk_size_ = std::max(
        0, channel_args.GetInt(GRPC_ARG_CHAOTIC_GOOD_MAX_SEND_CHUNK_SIZE)
               .value_or(max_send_chunk_size_));
    if (max_recv_chunk_size_ == 0 || max_send_chunk_size_ == 0) {
      max_recv_chunk_size_ = 0;
      max_send_chunk_size_ = 0;
    }
    inline_payload_size_threshold_ = std::max(
        0, channel_args
               .GetInt(GRPC_ARG_CHAOTIC_GOOD_INLINED_PAYLOAD_SIZE_THRESHOLD)
               .value_or(inline_payload_size_threshold_));
    tracing_enabled_ =
        channel_args.GetBool(GRPC_ARG_TCP_TRACING_ENABLED).value_or(false);
  }

  // Fill-in a settings frame to be sent with the results of the negotiation so
  // far. For the client this will be whatever we got from channel args; for the
  // server this is called *AFTER* ReceiveIncomingSettings and so contains the
  // result of mixing the server channel args with the client settings frame.
  void PrepareOutgoingSettings(chaotic_good_frame::Settings& settings) const {
    settings.set_alignment(decode_alignment_);
    settings.set_max_chunk_size(max_recv_chunk_size_);
  }

  // Receive a settings frame from our peer and integrate its settings with our
  // own.
  absl::Status ReceiveIncomingSettings(
      const chaotic_good_frame::Settings& settings) {
    if (settings.alignment() != 0) encode_alignment_ = settings.alignment();
    max_send_chunk_size_ =
        std::min(max_send_chunk_size_, settings.max_chunk_size());
    if (settings.max_chunk_size() == 0) {
      max_recv_chunk_size_ = 0;
      max_send_chunk_size_ = 0;
    }
    return absl::OkStatus();
  }

  // Factory: make transport options from the settings derived here-in.
  ChaoticGoodTransport::Options MakeTransportOptions() const {
    ChaoticGoodTransport::Options options;
    options.encode_alignment = encode_alignment_;
    options.decode_alignment = decode_alignment_;
    options.inlined_payload_size_threshold = inline_payload_size_threshold_;
    return options;
  }

  // Factory: create a message chunker based on negotiated settings.
  MessageChunker MakeMessageChunker() const {
    return MessageChunker(max_send_chunk_size_, encode_alignment_);
  }

  bool tracing_enabled() const { return tracing_enabled_; }

  void TestOnlySetChunkSizes(uint32_t size) {
    max_send_chunk_size_ = size;
    max_recv_chunk_size_ = size;
  }

  uint32_t encode_alignment() const { return encode_alignment_; }
  uint32_t decode_alignment() const { return decode_alignment_; }
  uint32_t max_send_chunk_size() const { return max_send_chunk_size_; }
  uint32_t max_recv_chunk_size() const { return max_recv_chunk_size_; }
  uint32_t inline_payload_size_threshold() const {
    return inline_payload_size_threshold_;
  }

  std::string ToString() const {
    return absl::StrCat(GRPC_DUMP_ARGS(tracing_enabled_, encode_alignment_,
                                       decode_alignment_, max_send_chunk_size_,
                                       max_recv_chunk_size_,
                                       inline_payload_size_threshold_));
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Config& config) {
    sink.Append(config.ToString());
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
