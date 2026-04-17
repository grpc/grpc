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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_LEGACY_MESSAGE_REASSEMBLY_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_LEGACY_MESSAGE_REASSEMBLY_H

#include "src/core/call/call_spine.h"
#include "src/core/ext/transport/chaotic_good_legacy/frame.h"
#include "absl/log/log.h"

namespace grpc_core {
namespace chaotic_good_legacy {

// Reassemble chunks of messages into messages, and enforce invariants about
// never having two messages in flight on the same stream.
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

  // Sets the maximum allowed incoming message size in bytes.
  // Should reflect GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH for the channel.
  void set_max_receive_message_length(size_t max) {
    max_receive_message_length_ = max;
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
    } else if (frame.body.length() == 0) {
      FailCall(sink,
               "Received begin message for an empty message (not allowed)");
    } else if (frame.body.length() > max_receive_message_length_) {
      FailCall(sink, "Received too large begin message");
    } else {
      GRPC_TRACE_LOG(chaotic_good, INFO)
          << this << " begin message " << frame.body.ShortDebugString();
      chunk_receiver_ = std::make_unique<ChunkReceiver>();
      chunk_receiver_->bytes_remaining = frame.body.length();
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
  // Maximum allowed incoming message size. Defaults to 4 GiB as a hard
  // safety ceiling to prevent unbounded heap allocation; callers must lower
  // this to GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH for the channel.
  size_t max_receive_message_length_ = std::numeric_limits<uint32_t>::max();

  struct ChunkReceiver {
    size_t bytes_remaining;
    SliceBuffer incoming;
  };
  std::unique_ptr<ChunkReceiver> chunk_receiver_;
};

}  // namespace chaotic_good_legacy
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_LEGACY_MESSAGE_REASSEMBLY_H
