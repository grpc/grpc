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

#ifndef CG_CHUNKER_H
#define CG_CHUNKER_H

#include <cstdint>

#include "frame.h"
namespace grpc_core {
namespace chaotic_good {

namespace message_chunker_detail {
struct ChunkResult {
  MessageChunkFrame frame;
  bool done;
};
class PayloadChunker {
 public:
  PayloadChunker(uint32_t max_chunk_size, uint32_t alignment,
                 uint32_t stream_id, SliceBuffer payload)
      : max_chunk_size_(max_chunk_size),
        alignment_(alignment),
        stream_id_(stream_id),
        payload_(std::move(payload)) {}

  ChunkResult NextChunk() {
    auto remaining = payload_.Length();
    ChunkResult result;
    if (remaining > max_chunk_size_) {
      auto take = max_chunk_size_;
      if (remaining / 2 < max_chunk_size_) {
        take = remaining / 2;
        if (take % alignment_ != 0) take += alignment_ - (take % alignment_);
      }
      payload_.MoveFirstNBytesIntoSliceBuffer(take, result.frame.payload);
      result.frame.stream_id = stream_id_;
      result.done = false;
    } else {
      result.frame.payload = std::move(payload_);
      result.frame.stream_id = stream_id_;
      result.done = true;
    }
    return result;
  }

 private:
  uint32_t max_chunk_size_;
  uint32_t alignment_;
  uint32_t stream_id_;
  SliceBuffer payload_;
};
}  // namespace message_chunker_detail

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
          return Seq(output.Send(std::move(begin)),
                     Loop([chunker = message_chunker_detail::PayloadChunker(
                               max_chunk_size_, alignment_, stream_id,
                               std::move(*message->payload())),
                           output]() mutable {
                       auto next = chunker.NextChunk();
                       return Map(output.Send(std::move(next.frame)),
                                  [done = next.done](bool x) -> LoopCtl<bool> {
                                    if (!done) return Continue{};
                                    return x;
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
    return max_chunk_size_ == 0 ||
           message.payload()->Length() > max_chunk_size_;
  }

  const uint32_t max_chunk_size_;
  const uint32_t alignment_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif
