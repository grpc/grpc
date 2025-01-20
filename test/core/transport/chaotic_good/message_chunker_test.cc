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

#include "src/core/ext/transport/chaotic_good/message_chunker.h"

#include <vector>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/status_flag.h"
#include "test/core/promise/poll_matcher.h"

namespace grpc_core {
namespace {

// One frame for this test is one of the message carrying frame types.
using Frame =
    std::variant<chaotic_good::BeginMessageFrame,
                 chaotic_good::MessageChunkFrame, chaotic_good::MessageFrame>;

// This type looks like an mpsc for sending frames, but simply accumulates
// frames so we can look at them at the end of the test and ensure they're
// correct.
struct Sender {
  std::vector<Frame> frames;
  Sender() = default;
  Sender(const Sender&) = delete;
  Sender(Sender&&) = delete;
  Sender& operator=(const Sender&) = delete;
  Sender& operator=(Sender&&) = delete;
  auto Send(Frame frame) {
    frames.emplace_back(std::move(frame));
    return []() -> Poll<StatusFlag> { return Success{}; };
  }
};

void MessageChunkerTest(uint32_t max_chunk_size, uint32_t alignment,
                        uint32_t stream_id, uint32_t message_flags,
                        std::string payload) {
  chaotic_good::MessageChunker chunker(max_chunk_size, alignment);
  Sender sender;
  EXPECT_THAT(chunker.Send(Arena::MakePooled<Message>(
                               SliceBuffer(Slice::FromCopiedString(payload)),
                               message_flags),
                           stream_id, sender)(),
              IsReady(Success{}));
  if (max_chunk_size == 0) {
    // No chunking ==> one frame with just a message.
    EXPECT_EQ(sender.frames.size(), 1);
    auto& f = std::get<chaotic_good::MessageFrame>(sender.frames[0]);
    EXPECT_EQ(f.message->payload()->JoinIntoString(), payload);
    EXPECT_EQ(f.stream_id, stream_id);
  } else {
    // Chunking ==> we'd better get at least one frame.
    ASSERT_GE(sender.frames.size(), 1);
    if (sender.frames.size() == 1) {
      // If just one frame, it'd better be one of the old-style message frames.
      EXPECT_LE(payload.length(), max_chunk_size);
      auto& f = std::get<chaotic_good::MessageFrame>(sender.frames[0]);
      EXPECT_EQ(f.message->payload()->JoinIntoString(), payload);
      EXPECT_EQ(f.stream_id, stream_id);
    } else {
      // Otherwise we should get a BeginMessage frame followed by a sequence of
      // MessageChunk frames, in payload order.
      auto& f0 = std::get<chaotic_good::BeginMessageFrame>(sender.frames[0]);
      EXPECT_EQ(f0.stream_id, stream_id);
      EXPECT_EQ(f0.body.length(), payload.length());
      std::string received_payload;
      for (size_t i = 1; i < sender.frames.size(); i++) {
        auto& f = std::get<chaotic_good::MessageChunkFrame>(sender.frames[i]);
        EXPECT_LE(f.payload.Length(), max_chunk_size);
        EXPECT_EQ(f.stream_id, stream_id);
        received_payload.append(f.payload.JoinIntoString());
      }
      EXPECT_EQ(received_payload, payload);
    }
  }
}
FUZZ_TEST(MyTestSuite, MessageChunkerTest);

}  // namespace
}  // namespace grpc_core
