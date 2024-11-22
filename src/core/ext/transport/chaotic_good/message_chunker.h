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

#include "frame.h"
namespace grpc_core {
namespace chaotic_good {

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
    return max_chunk_size_ == 0 ||
           message.payload()->Length() > max_chunk_size_;
  }

  struct ChunkResult {
    MessageChunkFrame frame;
    bool done;
  };

  const uint32_t max_chunk_size_;
  const uint32_t alignment_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif
