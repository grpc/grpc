// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CHAOTIC_GOOD_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CHAOTIC_GOOD_TRANSPORT_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include "absl/strings/escaping.h"
#include "src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.h"
#include "src/core/ext/transport/chaotic_good/control_endpoint.h"
#include "src/core/ext/transport/chaotic_good/data_endpoints.h"
#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/event_engine/extensions/tcp_trace.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/match_promise.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

inline std::vector<PromiseEndpoint> OneDataEndpoint(PromiseEndpoint endpoint) {
  std::vector<PromiseEndpoint> ep;
  ep.emplace_back(std::move(endpoint));
  return ep;
}

class IncomingFrame {
 public:
  template <typename T>
  IncomingFrame(FrameHeader header, T payload, size_t remove_padding)
      : header_(header),
        payload_(std::move(payload)),
        remove_padding_(remove_padding) {}

  const FrameHeader& header() { return header_; }

  auto Payload() {
    return Map(
        MatchPromise(
            std::move(payload_),
            [](absl::StatusOr<SliceBuffer> status) { return status; },
            [](DataEndpoints::ReadTicket ticket) { return ticket.Await(); }),
        [remove_padding =
             remove_padding_](absl::StatusOr<SliceBuffer> payload) {
          if (payload.ok()) payload->RemoveLastNBytesNoInline(remove_padding);
          return payload;
        });
  }

 private:
  FrameHeader header_;
  absl::variant<absl::StatusOr<SliceBuffer>, DataEndpoints::ReadTicket>
      payload_;
  size_t remove_padding_;
};

class ChaoticGoodTransport : public RefCounted<ChaoticGoodTransport> {
 public:
  struct Options {
    uint32_t encode_alignment = 64;
    uint32_t decode_alignment = 64;
    uint32_t inlined_payload_size_threshold = 8 * 1024;
  };

  ChaoticGoodTransport(
      PromiseEndpoint control_endpoint,
      std::vector<PromiseEndpoint> data_endpoints,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine,
      Options options)
      : event_engine_(std::move(event_engine)),
        control_endpoint_(std::move(control_endpoint), event_engine_.get()),
        data_endpoints_(std::move(data_endpoints), event_engine_.get()),
        options_(options) {}

  auto WriteFrame(const FrameInterface& frame) {
    FrameHeader header = frame.MakeHeader();
    GRPC_TRACE_LOG(chaotic_good, INFO)
        << "CHAOTIC_GOOD: WriteFrame to:"
        << ResolvedAddressToString(control_endpoint_.GetPeerAddress())
               .value_or("<<unknown peer address>>")
        << " " << frame.ToString();
    return If(
        data_endpoints_.empty() ||
            header.payload_length <= options_.inlined_payload_size_threshold,
        [this, &header, &frame]() {
          SliceBuffer output;
          header.Serialize(output.AddTiny(FrameHeader::kFrameHeaderSize));
          frame.SerializePayload(output);
          return control_endpoint_.Write(std::move(output));
        },
        [this, header, &frame]() mutable {
          SliceBuffer payload;
          // Temporarily give a bogus connection id to get padding right
          header.payload_connection_id = 1;
          const size_t padding = header.Padding(options_.encode_alignment);
          frame.SerializePayload(payload);
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: Send " << payload.Length()
              << "b payload on data channel; add " << padding << " bytes for "
              << options_.encode_alignment << " alignment";
          if (padding != 0) {
            auto slice = MutableSlice::CreateUninitialized(padding);
            memset(slice.data(), 0, padding);
            payload.AppendIndexed(Slice(std::move(slice)));
          }
          return Seq(data_endpoints_.Write(std::move(payload)),
                     [this, header](uint32_t connection_id) mutable {
                       header.payload_connection_id = connection_id + 1;
                       SliceBuffer header_frame;
                       header.Serialize(
                           header_frame.AddTiny(FrameHeader::kFrameHeaderSize));
                       return control_endpoint_.Write(std::move(header_frame));
                     });
        });
  }

  template <typename Frame>
  auto TransportWriteLoop(MpscReceiver<Frame>& outgoing_frames) {
    return Loop([self = Ref(), &outgoing_frames] {
      return TrySeq(
          // Get next outgoing frame.
          outgoing_frames.Next(),
          // Serialize and write it out.
          [self = self.get()](Frame client_frame) {
            return self->WriteFrame(GetFrameInterface(client_frame));
          },
          []() -> LoopCtl<absl::Status> {
            // The write failures will be caught in TrySeq and exit loop.
            // Therefore, only need to return Continue() in the last lambda
            // function.
            return Continue();
          });
    });
  }

  // Read frame header and payloads for control and data portions of one frame.
  // Resolves to StatusOr<IncomingFrame>.
  auto ReadFrameBytes() {
    return TrySeq(
        control_endpoint_.ReadSlice(FrameHeader::kFrameHeaderSize),
        [this](Slice read_buffer) {
          auto frame_header =
              FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
                  GRPC_SLICE_START_PTR(read_buffer.c_slice())));
          GRPC_TRACE_LOG(chaotic_good, INFO)
              << "CHAOTIC_GOOD: ReadHeader from:"
              << ResolvedAddressToString(control_endpoint_.GetPeerAddress())
                     .value_or("<<unknown peer address>>")
              << " "
              << (frame_header.ok() ? frame_header->ToString()
                                    : frame_header.status().ToString());
          return frame_header;
        },
        [this](FrameHeader frame_header) {
          return If(
              frame_header.payload_connection_id == 0,
              [this, frame_header]() {
                return Map(control_endpoint_.Read(frame_header.payload_length),
                           [frame_header](absl::StatusOr<SliceBuffer> payload)
                               -> absl::StatusOr<IncomingFrame> {
                             if (!payload.ok()) return payload.status();
                             return IncomingFrame(frame_header,
                                                  std::move(payload), 0);
                           });
              },
              [this, frame_header]() -> absl::StatusOr<IncomingFrame> {
                const auto padding =
                    frame_header.Padding(options_.decode_alignment);
                return IncomingFrame(
                    frame_header,
                    data_endpoints_.Read(frame_header.payload_connection_id - 1,
                                         frame_header.payload_length + padding),
                    padding);
              });
        });
  }

  template <typename T>
  absl::StatusOr<T> DeserializeFrame(const FrameHeader& header,
                                     SliceBuffer payload) {
    T frame;
    GRPC_TRACE_LOG(chaotic_good, INFO)
        << "CHAOTIC_GOOD: Deserialize " << header << " with payload "
        << absl::CEscape(payload.JoinIntoString());
    CHECK_EQ(header.payload_length, payload.Length());
    auto s = frame.Deserialize(header, std::move(payload));
    GRPC_TRACE_LOG(chaotic_good, INFO)
        << "CHAOTIC_GOOD: DeserializeFrame "
        << (s.ok() ? frame.ToString() : s.ToString());
    if (s.ok()) return std::move(frame);
    return std::move(s);
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  ControlEndpoint control_endpoint_;
  DataEndpoints data_endpoints_;
  const Options options_;
};

class MessageChunker {
 public:
  MessageChunker(uint32_t max_chunk_size, uint32_t alignment)
      : max_chunk_size_(max_chunk_size), alignment_(alignment) {}

  template <typename Output>
  auto Send(MessageHandle message, uint32_t stream_id, Output& output) {
    return If(
        ShouldChunk(*message),
        [&]() {
          BeginMessageFrame begin;
          begin.payload.set_length(message->payload()->Length());
          begin.stream_id = stream_id;
          return Seq(
              output.Send(std::move(begin)),
              Loop([max_chunk_size = max_chunk_size_, alignment = alignment_,
                    stream_id, payload = std::move(*message->payload()),
                    output]() mutable {
                auto remaining = payload.Length();
                return If(
                    remaining > max_chunk_size,
                    [&]() {
                      auto take = max_chunk_size;
                      if (remaining / 2 < max_chunk_size) {
                        take = remaining / 2;
                        take += (take % alignment == 0
                                     ? 0
                                     : alignment - (take % alignment));
                      }
                      MessageChunkFrame chunk;
                      payload.MoveFirstNBytesIntoSliceBuffer(take,
                                                             chunk.payload);
                      chunk.stream_id = stream_id;
                      return Map(
                          output.Send(std::move(chunk)),
                          [](bool) -> LoopCtl<bool> { return Continue{}; });
                    },
                    [&]() {
                      MessageChunkFrame chunk;
                      chunk.payload = std::move(payload);
                      chunk.stream_id = stream_id;
                      return Map(output.Send(std::move(chunk)),
                                 [](bool x) -> LoopCtl<bool> { return x; });
                    });
              }));
        },
        [&]() {
          MessageFrame frame;
          frame.message = std::move(message);
          frame.stream_id = stream_id;
          return output.Send(std::move(frame));
        });
  }

 private:
  bool ShouldChunk(Message& message) {
    LOG(INFO) << GRPC_DUMP_ARGS(max_chunk_size_, message.payload()->Length());
    return max_chunk_size_ == 0 ||
           message.payload()->Length() > max_chunk_size_;
  }

  const uint32_t max_chunk_size_;
  const uint32_t alignment_;
};

class MessageReassembly {
 public:
  void FailCall(CallInitiator& call, absl::string_view msg) {
    LOG_EVERY_N_SEC(INFO, 10) << "Call failed during reassembly: " << msg;
    call.Cancel();
  }
  void FailCall(CallHandler& call, absl::string_view msg) {
    LOG_EVERY_N_SEC(INFO, 10) << "Call failed during reassembly: " << msg;
    call.PushServerTrailingMetadata(
        CancelledServerMetadataFromStatus(GRPC_STATUS_INTERNAL, msg));
  }

  template <typename Sink>
  auto PushFrameInto(MessageFrame frame, Sink& sink) {
    return If(
        in_message_boundary(),
        [&]() { return sink.PushMessage(std::move(frame.message)); },
        [&]() {
          FailCall(sink,
                   "Received full message without completing previous chunked "
                   "message");
          return Immediate(StatusFlag(Failure{}));
        });
  }

  template <typename Sink>
  auto PushFrameInto(BeginMessageFrame frame, Sink& sink) {
    bool ok = false;
    if (!in_message_boundary()) {
      FailCall(sink,
               "Received begin message without completing previous chunked "
               "message");
    } else if (frame.payload.length() == 0) {
      FailCall(sink,
               "Received begin message for an empty message (not allowed)");
    } else if (frame.payload.length() > std::numeric_limits<size_t>::max()) {
      FailCall(sink, "Received too large begin message");
    } else {
      GRPC_TRACE_LOG(chaotic_good, INFO)
          << this << " begin message " << frame.payload.ShortDebugString();
      chunk_receiver_ = std::make_unique<ChunkReceiver>();
      chunk_receiver_->bytes_remaining = frame.payload.length();
      ok = true;
    }
    return Immediate(StatusFlag(ok));
  }

  template <typename Sink>
  auto PushFrameInto(MessageChunkFrame frame, Sink& sink) {
    bool ok = false;
    bool done = false;
    if (in_message_boundary()) {
      FailCall(sink, "Received message chunk without BeginMessage");
    } else if (chunk_receiver_->bytes_remaining < frame.payload.Length()) {
      FailCall(sink, "Message chunks are longer than BeginMessage declared");
    } else {
      GRPC_TRACE_LOG(chaotic_good, INFO)
          << "CHAOTIC_GOOD: " << this << " got chunk " << frame.payload.Length()
          << "b in message with " << chunk_receiver_->bytes_remaining
          << "b left";
      chunk_receiver_->bytes_remaining -= frame.payload.Length();
      chunk_receiver_->incoming.Append(frame.payload);
      ok = true;
      done = chunk_receiver_->bytes_remaining == 0;
      GRPC_TRACE_LOG(chaotic_good, INFO)
          << "CHAOTIC_GOOD: " << this << " " << GRPC_DUMP_ARGS(ok, done);
    }
    return If(
        done,
        [&]() {
          auto message = Arena::MakePooled<Message>(
              std::move(chunk_receiver_->incoming), 0);
          chunk_receiver_.reset();
          return sink.PushMessage(std::move(message));
        },
        [ok]() { return StatusFlag(ok); });
  }

  bool in_message_boundary() { return chunk_receiver_ == nullptr; }

 private:
  struct ChunkReceiver {
    size_t bytes_remaining;
    SliceBuffer incoming;
  };
  std::unique_ptr<ChunkReceiver> chunk_receiver_;
};

class Config {
 public:
  Config(const ChannelArgs& channel_args) {
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

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CHAOTIC_GOOD_TRANSPORT_H
