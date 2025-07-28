//
//
// Copyright 2025 gRPC authors.
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
//
//

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chttp2/transport/stream_data_queue.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/sleep.h"
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

using http2::SimpleQueue;
using http2::StreamDataQueue;
using ::testing::MockFunction;
using ::testing::StrictMock;

// Fuzzer tests
class SimpleQueueFuzzTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  Party* GetParty() { return party_.get(); }

  void InitParty() {
    auto party_arena = SimpleArenaAllocator(0)->MakeArena();
    party_arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine().get());
    party_ = Party::Make(std::move(party_arena));
  }

  auto EnqueueAndCheckSuccess(SimpleQueue<int>& queue, int data, int tokens) {
    return Map([&queue, data,
                tokens]() mutable { return queue.Enqueue(data, tokens); },
               [](auto result) { EXPECT_EQ(result.status, absl::OkStatus()); });
  }

  bool DequeueAndCheck(SimpleQueue<int>& queue, int data,
                       bool allow_oversized_dequeue,
                       int allowed_dequeue_tokens) {
    auto result =
        queue.Dequeue(allowed_dequeue_tokens, allow_oversized_dequeue);
    if (!result.has_value()) {
      return false;
    }

    EXPECT_EQ(result.value(), data);
    return true;
  }

 private:
  void InitCoreConfiguration() override {}
  void InitTest() override { InitParty(); }
  void Shutdown() override { party_.reset(); }

  RefCountedPtr<Party> party_;
};

YODEL_TEST(SimpleQueueFuzzTest, NoOp) {}

YODEL_TEST(SimpleQueueFuzzTest, EnqueueAndDequeueMultiPartyTest) {
  // Test to enqueue and dequeue in a loop. This test enqueues 100 entries and
  // dequeues 100 entries. This test asserts the following:
  // 1. All enqueues and dequeues are successful.
  // 2. The dequeue data is the same as the enqueue data.
  SimpleQueue<int> queue(/*max_tokens=*/100);
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));
  constexpr int count = 100;
  constexpr int dequeue_count = 100;
  int current_enqueue_count = 0;
  int current_dequeue_count = 0;

  GetParty()->Spawn(
      "EnqueueTest",
      Loop([this, &queue, &on_done, &current_enqueue_count]() mutable {
        return If(
            current_enqueue_count < count,
            [this, &queue, &current_enqueue_count] {
              return Map(
                  EnqueueAndCheckSuccess(queue,
                                         /*data=*/current_enqueue_count++,
                                         /*tokens=*/10),
                  [](auto) -> LoopCtl<StatusFlag> { return Continue(); });
            },
            [&on_done]() -> LoopCtl<StatusFlag> {
              on_done.Call(absl::OkStatus());
              return Success{};
            });
      }),
      [](auto) { LOG(INFO) << "Reached end of EnqueueTest"; });

  GetParty()->Spawn(
      "DequeueTest",
      Loop([&on_dequeue_done, this, &queue, &current_dequeue_count] {
        return If(
            DequeueAndCheck(queue, /*data=*/current_dequeue_count,
                            /*allow_oversized_dequeue=*/false,
                            /*allowed_dequeue_tokens=*/10),
            [&current_dequeue_count, &on_dequeue_done,
             &queue]() -> LoopCtl<absl::Status> {
              if (++current_dequeue_count == dequeue_count) {
                on_dequeue_done.Call(absl::OkStatus());
                EXPECT_TRUE(queue.TestOnlyIsEmpty());
                return absl::OkStatus();
              } else {
                return Continue();
              }
            },
            [] {
              return Map(
                  Sleep(Duration::Seconds(1)),
                  [](auto) -> LoopCtl<absl::Status> { return Continue(); });
            });
      }),
      [](auto) { LOG(INFO) << "Reached end of DequeueTest"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

class StreamDataQueueFuzzTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  Party* GetParty() { return party_.get(); }
  Party* GetParty2() { return party2_.get(); }

  void InitParty() {
    auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
    general_party_arena
        ->SetContext<grpc_event_engine::experimental::EventEngine>(
            event_engine().get());
    party_ = Party::Make(std::move(general_party_arena));
  }

  void InitParty2() {
    auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
    general_party_arena
        ->SetContext<grpc_event_engine::experimental::EventEngine>(
            event_engine().get());
    party2_ = Party::Make(std::move(general_party_arena));
  }

  // Helper functions to create test data.
  ClientMetadataHandle TestClientInitialMetadata() {
    auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
    md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step"));
    return md;
  }

  MessageHandle TestMessage(SliceBuffer payload, const uint32_t flags) {
    return Arena::MakePooled<Message>(std::move(payload), flags);
  }
  std::vector<MessageHandle> TestMessages(int num_messages) {
    std::vector<MessageHandle> messages;
    for (int i = 0; i < num_messages; ++i) {
      messages.push_back(
          TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(i * 10)), 0));
    }
    return messages;
  }

  class AssembleFrames {
   public:
    explicit AssembleFrames(const uint32_t stream_id)
        : header_assembler_(stream_id) {}
    void operator()(Http2HeaderFrame frame) {
      auto status = header_assembler_.AppendHeaderFrame(std::move(frame));
      EXPECT_TRUE(status.IsOk());
    }
    void operator()(Http2ContinuationFrame frame) {
      auto status = header_assembler_.AppendContinuationFrame(std::move(frame));
      EXPECT_TRUE(status.IsOk());
    }
    void operator()(Http2DataFrame frame) {
      auto status = message_assembler_.AppendNewDataFrame(frame.payload,
                                                          frame.end_stream);
      EXPECT_TRUE(status.IsOk());
    }
    void operator()(Http2RstStreamFrame frame) {
      Crash("RstStreamFrame not expected");
    }
    void operator()(Http2GoawayFrame frame) {
      Crash("GoAwayFrame not expected");
    }
    void operator()(Http2WindowUpdateFrame frame) {
      Crash("WindowUpdateFrame not expected");
    }
    void operator()(Http2SettingsFrame frame) {
      Crash("SettingsFrame not expected");
    }
    void operator()(Http2SecurityFrame frame) {
      Crash("SecurityFrame not expected");
    }
    void operator()(Http2EmptyFrame frame) { Crash("EmptyFrame not expected"); }
    void operator()(Http2PingFrame frame) { Crash("PingFrame not expected"); }
    void operator()(Http2UnknownFrame frame) {
      Crash("UnknownFrame not expected");
    }
    ClientMetadataHandle GetMetadata() {
      DCHECK(header_assembler_.IsReady());
      auto status_or_metadata = header_assembler_.ReadMetadata(
          parser_, /*is_initial_metadata=*/true, /*is_client=*/true);
      EXPECT_TRUE(status_or_metadata.IsOk());
      return TakeValue(std::move(status_or_metadata));
    }
    std::vector<MessageHandle> GetMessages() {
      std::vector<MessageHandle> messages;
      while (true) {
        auto message = message_assembler_.ExtractMessage();
        if (message.IsOk() && message.value() != nullptr) {
          messages.push_back(TakeValue(std::move(message)));
        } else {
          break;
        }
      }
      return messages;
    }

   private:
    http2::HeaderAssembler header_assembler_;
    http2::GrpcMessageAssembler message_assembler_;
    HPackParser parser_;
  };

 private:
  void InitCoreConfiguration() override {}
  void InitTest() override { InitParty(); }
  void Shutdown() override {
    party_.reset();
    party2_.reset();
  }

  RefCountedPtr<Party> party_;
  RefCountedPtr<Party> party2_;
};

YODEL_TEST(StreamDataQueueFuzzTest, EnqueueDequeueMultiParty) {
  InitParty();
  InitParty2();
  constexpr uint32_t max_frame_length = 44;
  constexpr uint32_t max_tokens = 75;
  constexpr uint32_t queue_size = 100;
  constexpr uint32_t stream_id = 1;
  constexpr uint32_t num_messages = 1;
  StrictMock<MockFunction<void()>> on_enqueue_done;
  StrictMock<MockFunction<void()>> on_dequeue_done;
  EXPECT_CALL(on_enqueue_done, Call());
  EXPECT_CALL(on_dequeue_done, Call());
  HPackCompressor encoder;
  StreamDataQueue<ClientMetadataHandle> stream_data_queue(
      /*is_client=*/true,
      /*stream_id=*/stream_id,
      /*queue_size=*/queue_size);
  std::vector<MessageHandle> messages = TestMessages(num_messages);
  std::vector<MessageHandle> messages_copy = TestMessages(num_messages);
  std::vector<Http2Frame> received_frames;
  uint message_index = 0;

  auto validate = [this,
                   &messages_copy](std::vector<Http2Frame>& received_frames) {
    AssembleFrames assembler(stream_id);
    for (auto& frame : received_frames) {
      std::visit(assembler, std::move(frame));
    }
    auto metadata = assembler.GetMetadata();
    auto expected_metadata = TestClientInitialMetadata();
    absl::string_view metadata_value =
        metadata->get_pointer(HttpPathMetadata())->as_string_view();
    absl::string_view expected_metadata_value =
        expected_metadata->get_pointer(HttpPathMetadata())->as_string_view();
    EXPECT_EQ(metadata_value, expected_metadata_value);
    auto messages = assembler.GetMessages();
    EXPECT_EQ(messages.size(), messages_copy.size());
    for (int i = 0; i < messages.size(); ++i) {
      EXPECT_EQ(messages[i]->flags(), messages_copy[i]->flags());
      EXPECT_EQ(messages[i]->payload()->JoinIntoString(),
                messages_copy[i]->payload()->JoinIntoString());
    }
  };
  GetParty()->Spawn(
      "EnqueuePromise",
      TrySeq(
          stream_data_queue.EnqueueInitialMetadata(TestClientInitialMetadata()),
          [&stream_data_queue, &messages, &message_index] {
            return Loop([&stream_data_queue, &messages, &message_index] {
              return If(
                  message_index < messages.size(),
                  [&stream_data_queue, &messages, &message_index] {
                    return Map(stream_data_queue.EnqueueMessage(
                                   std::move(messages[message_index++])),
                               [](auto) -> LoopCtl<absl::Status> {
                                 return Continue{};
                               });
                  },
                  []() -> LoopCtl<absl::Status> { return absl::OkStatus(); });
            });
          },
          [&stream_data_queue] {
            return stream_data_queue.EnqueueHalfClosed();
          }),
      [&on_enqueue_done](auto) { on_enqueue_done.Call(); });

  GetParty2()->Spawn(
      "DequeuePromise",
      Loop([&stream_data_queue, &received_frames, &validate]() {
        return If(
            stream_data_queue.IsClosed(),
            [&validate, &received_frames]() -> LoopCtl<absl::Status> {
              validate(received_frames);
              return absl::OkStatus();
            },
            [&stream_data_queue, &received_frames] {
              auto frames =
                  stream_data_queue.DequeueFrames(max_tokens, max_frame_length);
              EXPECT_TRUE(frames.ok());
              for (auto& frame : frames.value().frames) {
                received_frames.push_back(std::move(frame));
              }
              return Map(
                  Sleep(Duration::Seconds(1)),
                  [](auto) -> LoopCtl<absl::Status> { return Continue{}; });
            });
      }),
      [&on_dequeue_done](auto) { on_dequeue_done.Call(); });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

}  // namespace grpc_core
