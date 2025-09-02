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

#include "src/core/ext/transport/chttp2/transport/stream_data_queue.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/sleep.h"
#include "test/core/transport/chttp2/http2_common_test_inputs.h"
#include "test/core/transport/util/transport_test.h"

namespace grpc_core {

namespace http2 {
namespace testing {

using ::testing::MockFunction;
using ::testing::StrictMock;
using util::testing::TransportTest;

namespace {
auto EnqueueAndCheckSuccess(http2::SimpleQueue<int>& queue, int data,
                            int tokens) {
  LOG(INFO) << "EnqueueAndCheckSuccess for data: " << data
            << " tokens: " << tokens;
  return Map(
      [&queue, data, tokens]() mutable { return queue.Enqueue(data, tokens); },
      [data, tokens](absl::StatusOr<bool> result) {
        LOG(INFO) << "Enqueue done for data: " << data << " tokens: " << tokens;
        EXPECT_TRUE(result.ok());
      });
}

void DequeueAndCheckPending(http2::SimpleQueue<int>& queue,
                            bool allow_oversized_dequeue,
                            int allowed_dequeue_tokens) {
  LOG(INFO) << "DequeueAndCheckPending for allow_oversized_dequeue: "
            << allow_oversized_dequeue
            << " allowed_dequeue_tokens: " << allowed_dequeue_tokens;
  std::optional<int> result =
      queue.Dequeue(allowed_dequeue_tokens, allow_oversized_dequeue);
  EXPECT_FALSE(result.has_value());
}

void DequeueAndCheckSuccess(http2::SimpleQueue<int>& queue, int data,
                            bool allow_oversized_dequeue,
                            int allowed_dequeue_tokens) {
  LOG(INFO) << "DequeueAndCheckSuccess for data: " << data
            << " allow_oversized_dequeue: " << allow_oversized_dequeue
            << " allowed_dequeue_tokens: " << allowed_dequeue_tokens;
  std::optional<int> result =
      queue.Dequeue(allowed_dequeue_tokens, allow_oversized_dequeue);
  EXPECT_TRUE(result.has_value());

  LOG(INFO) << "Dequeue successful for data: " << data;
  EXPECT_EQ(result.value(), data);
}

bool DequeueAndCheck(http2::SimpleQueue<int>& queue, int data,
                     bool allow_oversized_dequeue, int allowed_dequeue_tokens) {
  LOG(INFO) << "DequeueAndCheck for data: " << data
            << " allow_oversized_dequeue: " << allow_oversized_dequeue
            << " allowed_dequeue_tokens: " << allowed_dequeue_tokens;
  std::optional<int> result =
      queue.Dequeue(allowed_dequeue_tokens, allow_oversized_dequeue);
  if (!result.has_value()) {
    LOG(INFO) << "Dequeue result is empty";
    return false;
  }

  LOG(INFO) << "Dequeue successful for data: " << data;
  EXPECT_EQ(result.value(), data);
  return true;
}
}  // namespace

class SimpleQueueTest : public TransportTest {
 protected:
  SimpleQueueTest() { InitParty(); }

  Party* GetParty() { return party_.get(); }

  void InitParty() {
    auto party_arena = SimpleArenaAllocator(0)->MakeArena();
    party_arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine().get());
    party_ = Party::Make(std::move(party_arena));
  }

 private:
  RefCountedPtr<Party> party_;
};

////////////////////////////////////////////////////////////////////////////////
// Enqueue tests

TEST_F(SimpleQueueTest, EnqueueTest) {
  // Simple test that does a single enqueue.
  http2::SimpleQueue<int> queue(/*max_tokens=*/100);
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));

  GetParty()->Spawn("EnqueueTest",
                    EnqueueAndCheckSuccess(queue, /*data=*/1, /*tokens=*/10),
                    [&on_done](auto) {
                      LOG(INFO) << "Reached end of EnqueueTest";
                      on_done.Call(absl::OkStatus());
                    });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(SimpleQueueTest, EnqueueZeroTokensTest) {
  // Simple test that does a single enqueue with zero tokens.
  http2::SimpleQueue<int> queue(/*max_tokens=*/100);
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));

  GetParty()->Spawn("EnqueueTest",
                    EnqueueAndCheckSuccess(queue, /*data=*/1, /*tokens=*/0),
                    [&on_done](auto) {
                      LOG(INFO) << "Reached end of EnqueueTest";
                      on_done.Call(absl::OkStatus());
                    });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(SimpleQueueTest, MultipleEnqueueTest) {
  // Test multiple enqueues. All the enqueues for this test are immediate.
  http2::SimpleQueue<int> queue(/*max_tokens=*/100);
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  int count = 10;

  GetParty()->Spawn("EnqueueTest", Loop([&count, &queue, &on_done]() {
                      return If(
                          count > 0,
                          [&queue, &count] {
                            return Map(EnqueueAndCheckSuccess(queue, /*data=*/1,
                                                              /*tokens=*/10),
                                       [&count](auto) -> LoopCtl<StatusFlag> {
                                         count--;
                                         return Continue();
                                       });
                          },
                          [&on_done]() -> LoopCtl<StatusFlag> {
                            on_done.Call(absl::OkStatus());
                            return Success{};
                          });
                    }),
                    [](auto) { LOG(INFO) << "Reached end of EnqueueTest"; });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

////////////////////////////////////////////////////////////////////////////////
// Dequeue tests
TEST_F(SimpleQueueTest, DequeueEmptyQueueTest) {
  // Test to dequeue from an empty queue.
  http2::SimpleQueue<int> queue(/*max_tokens=*/100);

  std::optional<int> result = queue.Dequeue(/*allowed_dequeue_tokens=*/10,
                                            /*allow_oversized_dequeue=*/false);
  EXPECT_FALSE(result.has_value());
}

TEST_F(SimpleQueueTest, DequeueTest) {
  // Simple test to dequeue a single entry. This test waits for the enqueue to
  // complete before dequeuing. This test asserts the following:
  // 1. Both enqueue and dequeue are successful.
  // 2. The dequeue data is the same as the enqueue data.
  http2::SimpleQueue<int> queue(/*max_tokens=*/100);
  Latch<void> enqueue_done;
  StrictMock<MockFunction<void(absl::Status)>> on_enqueue_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_enqueue_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));

  GetParty()->Spawn("EnqueueTest",
                    EnqueueAndCheckSuccess(queue, /*data=*/1, /*tokens=*/10),
                    [&on_enqueue_done, &enqueue_done](auto) {
                      LOG(INFO) << "Reached end of EnqueueTest";
                      on_enqueue_done.Call(absl::OkStatus());
                      enqueue_done.Set();
                    });

  GetParty()->Spawn("DequeueTest",
                    Map(enqueue_done.Wait(),
                        [&queue, &on_dequeue_done](auto) {
                          DequeueAndCheckSuccess(
                              queue, /*data=*/1,
                              /*allow_oversized_dequeue=*/false,
                              /*allowed_dequeue_tokens=*/10);
                          on_dequeue_done.Call(absl::OkStatus());
                          EXPECT_TRUE(queue.TestOnlyIsEmpty());
                          return absl::OkStatus();
                        }),
                    [](auto) { LOG(INFO) << "Reached end of DequeueTest"; });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(SimpleQueueTest, DequeuePartialDequeueTest) {
  // Test to assert on different combinations of allow_oversized_dequeue.
  http2::SimpleQueue<int> queue(/*max_tokens=*/200);
  Latch<void> enqueue_done;
  StrictMock<MockFunction<void(absl::Status)>> on_enqueue_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_enqueue_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));

  GetParty()->Spawn(
      "EnqueueTest",
      TrySeq(EnqueueAndCheckSuccess(queue, /*data=*/1, /*tokens=*/99),
             EnqueueAndCheckSuccess(queue, /*data=*/2, /*tokens=*/100)),
      [&on_enqueue_done, &enqueue_done](auto) {
        LOG(INFO) << "Reached end of EnqueueTest";
        on_enqueue_done.Call(absl::OkStatus());
        enqueue_done.Set();
      });

  GetParty()->Spawn(
      "DequeueTest",
      TrySeq(enqueue_done.Wait(),
             [&queue] {
               DequeueAndCheckPending(queue,
                                      /*allow_oversized_dequeue=*/false,
                                      /*allowed_dequeue_tokens=*/10);
               DequeueAndCheckSuccess(queue, /*data=*/1,
                                      /*allow_oversized_dequeue=*/true,
                                      /*allowed_dequeue_tokens=*/10);
               DequeueAndCheckSuccess(queue, /*data=*/2,
                                      /*allow_oversized_dequeue=*/false,
                                      /*allowed_dequeue_tokens=*/100);
               // Empty Queue
               DequeueAndCheckPending(queue,
                                      /*allow_oversized_dequeue=*/false,
                                      /*allowed_dequeue_tokens=*/10);
               DequeueAndCheckPending(queue,
                                      /*allow_oversized_dequeue=*/false,
                                      /*allowed_dequeue_tokens=*/100);
               DequeueAndCheckPending(queue,
                                      /*allow_oversized_dequeue=*/true,
                                      /*allowed_dequeue_tokens=*/10);
               return absl::OkStatus();
             }),
      [&on_dequeue_done, &queue](auto) {
        LOG(INFO) << "Reached end of DequeueTest";
        on_dequeue_done.Call(absl::OkStatus());
        EXPECT_TRUE(queue.TestOnlyIsEmpty());
      });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(SimpleQueueTest, DequeueTokensTest) {
  // Test to assert different combinations of allowed_dequeue_tokens.
  http2::SimpleQueue<int> queue(/*max_tokens=*/200);
  Latch<void> enqueue_done;
  StrictMock<MockFunction<void(absl::Status)>> on_enqueue_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_enqueue_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));

  GetParty()->Spawn("EnqueueTest",
                    TrySeq(EnqueueAndCheckSuccess(queue, /*data=*/1,
                                                  /*tokens=*/100),
                           EnqueueAndCheckSuccess(queue, /*data=*/2,
                                                  /*tokens=*/99)),
                    [&on_enqueue_done, &enqueue_done](auto) {
                      LOG(INFO) << "Reached end of EnqueueTest";
                      on_enqueue_done.Call(absl::OkStatus());
                      enqueue_done.Set();
                    });

  GetParty()->Spawn(
      "DequeueTest",
      TrySeq(enqueue_done.Wait(),
             [&queue] {
               // 2 entries
               DequeueAndCheckPending(queue,
                                      /*allow_oversized_dequeue=*/false,
                                      /*allowed_dequeue_tokens=*/10);
               DequeueAndCheckPending(queue,
                                      /*allow_oversized_dequeue=*/false,
                                      /*allowed_dequeue_tokens=*/99);
               DequeueAndCheckSuccess(queue, /*data=*/1,
                                      /*allow_oversized_dequeue=*/false,
                                      /*allowed_dequeue_tokens=*/100);

               // 1 entry
               DequeueAndCheckPending(queue,
                                      /*allow_oversized_dequeue=*/false,
                                      /*allowed_dequeue_tokens=*/5);
               DequeueAndCheckSuccess(queue, /*data=*/2,
                                      /*allow_oversized_dequeue=*/false,
                                      /*allowed_dequeue_tokens=*/500);
               return absl::OkStatus();
             }),
      [&on_dequeue_done, &queue](auto) {
        LOG(INFO) << "Reached end of DequeueTest";
        on_dequeue_done.Call(absl::OkStatus());
        EXPECT_TRUE(queue.TestOnlyIsEmpty());
      });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

////////////////////////////////////////////////////////////////////////////////
// Enqueue and dequeue tests
TEST_F(SimpleQueueTest, BigMessageEnqueueDequeueTest) {
  // Tests that for a queue with current tokens consumed equal to 0, allows a
  // message to be enqueued even if the tokens are more than the max tokens.
  SimpleQueue<int> queue(/*max_tokens=*/100);
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));
  int dequeue_count = 2;
  std::string execution_order;
  std::vector<int> expected_data = {1, 2};
  int expected_data_index = 0;

  GetParty()->Spawn("EnqueueTest",
                    TrySeq(
                        EnqueueAndCheckSuccess(queue, /*data=*/1, /*tokens=*/0),
                        [&queue, &execution_order] {
                          execution_order.push_back('1');
                          return EnqueueAndCheckSuccess(queue, /*data=*/2,
                                                        /*tokens=*/1000);
                        },
                        [&queue, &execution_order] {
                          execution_order.push_back('2');
                          return EnqueueAndCheckSuccess(queue, /*data=*/3,
                                                        /*tokens=*/10);
                        }),
                    [&on_done, &execution_order](auto) {
                      LOG(INFO) << "Reached end of EnqueueTest";
                      on_done.Call(absl::OkStatus());
                      execution_order.push_back('4');
                    });

  GetParty()->Spawn(
      "DequeueTest",
      Loop([&dequeue_count, &on_dequeue_done, &queue, &execution_order,
            &expected_data, &expected_data_index] {
        return If(
            DequeueAndCheck(queue, expected_data[expected_data_index++],
                            /*allow_oversized_dequeue=*/true,
                            /*allowed_dequeue_tokens=*/10),
            [&dequeue_count, &on_dequeue_done, &execution_order,
             &queue]() -> LoopCtl<absl::Status> {
              if (--dequeue_count == 0) {
                execution_order.push_back('3');
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

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
  EXPECT_STREQ(execution_order.c_str(), "1234");
}

////////////////////////////////////////////////////////////////////////////////
// Stream Data Queue Tests

namespace {
// Helper functions to create test data.
ClientMetadataHandle TestClientInitialMetadata() {
  ClientMetadataHandle md = Arena::MakePooledForOverwrite<ClientMetadata>();
  md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step"));
  return md;
}

ServerMetadataHandle TestServerInitialMetadata() {
  ServerMetadataHandle md = Arena::MakePooledForOverwrite<ServerMetadata>();
  md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step2"));
  return md;
}

ServerMetadataHandle TestServerTrailingMetadata() {
  ServerMetadataHandle md = Arena::MakePooledForOverwrite<ServerMetadata>();
  md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step3"));
  return md;
}

MessageHandle TestMessage(SliceBuffer payload, const uint32_t flags) {
  return Arena::MakePooled<Message>(std::move(payload), flags);
}

template <typename MetadataHandle>
void EnqueueInitialMetadataAndCheckSuccess(
    RefCountedPtr<StreamDataQueue<MetadataHandle>> queue,
    MetadataHandle&& metadata, const bool expected_writeable_state,
    const WritableStreams::StreamPriority expected_priority) {
  LOG(INFO) << "Enqueueing initial metadata";
  auto promise =
      queue->EnqueueInitialMetadata(std::forward<MetadataHandle>(metadata));
  auto result = promise();
  EXPECT_TRUE(result.ready());
  EXPECT_TRUE(result.value().ok());
  EXPECT_EQ(result.value().value().became_writable, expected_writeable_state);
  EXPECT_EQ(result.value().value().priority, expected_priority);
  LOG(INFO) << "Enqueueing initial metadata success";
}

template <typename MetadataHandle>
void EnqueueTrailingMetadataAndCheckSuccess(
    RefCountedPtr<StreamDataQueue<MetadataHandle>> queue,
    MetadataHandle&& metadata, const bool expected_writeable_state,
    const WritableStreams::StreamPriority expected_priority) {
  LOG(INFO) << "Enqueueing trailing metadata";
  auto promise =
      queue->EnqueueTrailingMetadata(std::forward<MetadataHandle>(metadata));
  auto result = promise();
  EXPECT_TRUE(result.ready());
  EXPECT_TRUE(result.value().ok());
  EXPECT_EQ(result.value().value().became_writable, expected_writeable_state);
  EXPECT_EQ(result.value().value().priority, expected_priority);
  LOG(INFO) << "Enqueueing trailing metadata success";
}

template <typename MetadataHandle>
void EnqueueMessageAndCheckSuccess(
    RefCountedPtr<StreamDataQueue<MetadataHandle>> queue,
    MessageHandle&& message, const bool expected_writeable_state,
    const WritableStreams::StreamPriority expected_priority) {
  LOG(INFO) << "Enqueueing message with tokens: "
            << message->payload()->Length()
            << " and flags: " << message->flags();
  auto promise = queue->EnqueueMessage(std::forward<MessageHandle>(message));
  auto result = promise();
  EXPECT_TRUE(result.ready());
  EXPECT_TRUE(result.value().ok());
  EXPECT_EQ(result.value().value().became_writable, expected_writeable_state);
  EXPECT_EQ(result.value().value().priority, expected_priority);
  LOG(INFO) << "Enqueueing message success";
}

template <typename MetadataHandle>
void EnqueueResetStreamAndCheckSuccess(
    RefCountedPtr<StreamDataQueue<MetadataHandle>> queue,
    const bool expected_writeable_state,
    const WritableStreams::StreamPriority expected_priority) {
  LOG(INFO) << "Enqueueing reset stream";
  auto promise = queue->EnqueueResetStream(/*error_code=*/0);
  auto result = promise();
  EXPECT_TRUE(result.ready());
  EXPECT_TRUE(result.value().ok());
  EXPECT_EQ(result.value().value().became_writable, expected_writeable_state);
  EXPECT_EQ(result.value().value().priority, expected_priority);
  LOG(INFO) << "Enqueueing reset stream success";
}

void EnqueueHalfClosedAndCheckSuccess(
    RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> queue,
    const bool expected_writeable_state,
    const WritableStreams::StreamPriority expected_priority) {
  LOG(INFO) << "Enqueueing half closed";
  auto promise = queue->EnqueueHalfClosed();
  auto result = promise();
  EXPECT_TRUE(result.ready());
  EXPECT_TRUE(result.value().ok());
  EXPECT_EQ(result.value().value().became_writable, expected_writeable_state);
  EXPECT_EQ(result.value().value().priority, expected_priority);
  LOG(INFO) << "Enqueueing half closed success";
}

template <typename MetadataHandle>
void DequeueAndCheckSuccess(
    RefCountedPtr<StreamDataQueue<MetadataHandle>> queue,
    std::vector<Http2Frame> expected_frames, HPackCompressor& encoder,
    const uint32_t max_tokens = 10, const uint32_t max_frame_length = 10) {
  absl::StatusOr<typename StreamDataQueue<MetadataHandle>::DequeueResult>
      frames = queue->DequeueFrames(max_tokens, max_frame_length, encoder);
  EXPECT_TRUE(frames.ok());
  EXPECT_EQ(frames.value().frames.size(), expected_frames.size());

  std::vector<Http2Frame>& frames_vector = frames.value().frames;
  for (int count = 0; count < frames_vector.size(); ++count) {
    EXPECT_EQ((frames_vector[count]), (expected_frames[count]));
  }
}

template <typename MetadataHandle>
void DequeueMessageAndCheckSuccess(
    RefCountedPtr<StreamDataQueue<MetadataHandle>> queue,
    std::vector<int> expected_frames_length, HPackCompressor& encoder,
    const uint32_t max_tokens = 10, const uint32_t max_frame_length = 10) {
  absl::StatusOr<typename StreamDataQueue<MetadataHandle>::DequeueResult>
      frames = queue->DequeueFrames(max_tokens, max_frame_length, encoder);
  EXPECT_TRUE(frames.ok());
  EXPECT_EQ(frames.value().frames.size(), expected_frames_length.size());
  std::vector<Http2Frame>& frames_vector = frames.value().frames;
  for (int count = 0; count < frames.value().frames.size(); ++count) {
    EXPECT_EQ(std::get<Http2DataFrame>(frames_vector[count]).payload.Length(),
              expected_frames_length[count]);
  }
}
}  // namespace

constexpr bool kAllowTrueBinaryMetadataSetting = true;

////////////////////////////////////////////////////////////////////////////////
// Client Tests
TEST(StreamDataQueueTest, ClientEnqueueInitialMetadataTest) {
  // Simple test to enqueue initial metadata.
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
          /*is_client=*/true,
          /*stream_id=*/1,
          /*queue_size=*/10, kAllowTrueBinaryMetadataSetting);
  EnqueueInitialMetadataAndCheckSuccess(
      stream_data_queue, TestClientInitialMetadata(),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
}

TEST(StreamDataQueueTest, ClientEnqueueMultipleMessagesTest) {
  // Test to enqueue multiple messages upto the queue size. This tests expects
  // that all the enqueue promises are resolved immediately.
  HPackCompressor encoder;
  constexpr int num_messages = 10;
  constexpr int message_size = 1;
  constexpr int queued_size =
      num_messages * (message_size + kGrpcHeaderSizeInBytes);
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
          /*is_client=*/true,
          /*stream_id=*/1,
          /*queue_size=*/queued_size, kAllowTrueBinaryMetadataSetting);
  EnqueueInitialMetadataAndCheckSuccess(
      stream_data_queue, TestClientInitialMetadata(),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);

  for (int count = 0; count < 10; ++count) {
    EnqueueMessageAndCheckSuccess(
        stream_data_queue,
        TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(message_size)),
                    0),
        /*expected_writeable_state=*/false,
        /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  }
}

TEST(StreamDataQueueTest, ClientEnqueueEndStreamTest) {
  // Test to enqueue initial Metadata, Message and Half Close. This asserts the
  // order of enqueue operations (initial metadata -> message -> half close).
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
          /*is_client=*/true,
          /*stream_id=*/1,
          /*queue_size=*/10, kAllowTrueBinaryMetadataSetting);
  EnqueueInitialMetadataAndCheckSuccess(
      stream_data_queue, TestClientInitialMetadata(),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  EnqueueMessageAndCheckSuccess(
      stream_data_queue,
      TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(1)), 0),
      /*expected_writeable_state=*/false,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  EnqueueHalfClosedAndCheckSuccess(
      stream_data_queue,
      /*expected_writeable_state=*/false,
      /*expected_priority=*/WritableStreams::StreamPriority::kStreamClosed);
}

TEST(StreamDataQueueTest, ClientEnqueueResetStreamTest) {
  // Test to assert that messages are optional and reset stream can be enqueued
  // after initial metadata.
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
          /*is_client=*/true,
          /*stream_id=*/1,
          /*queue_size=*/10, kAllowTrueBinaryMetadataSetting);
  EnqueueInitialMetadataAndCheckSuccess(
      stream_data_queue, TestClientInitialMetadata(),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  EnqueueResetStreamAndCheckSuccess(
      stream_data_queue,
      /*expected_writeable_state=*/false,
      /*expected_priority=*/WritableStreams::StreamPriority::kStreamClosed);
}

TEST(StreamDataQueueTest, ClientEmptyDequeueTest) {
  // Test to assert that dequeue returns empty frames when there is nothing to
  // dequeue.
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
          /*is_client=*/true,
          /*stream_id=*/1,
          /*queue_size=*/10, kAllowTrueBinaryMetadataSetting);
  EXPECT_TRUE(stream_data_queue->TestOnlyIsEmpty());
  DequeueAndCheckSuccess(stream_data_queue, std::vector<Http2Frame>(), encoder);
  EXPECT_TRUE(stream_data_queue->TestOnlyIsEmpty());
}

TEST(StreamDataQueueTest, ClientDequeueMetadataSingleFrameTest) {
  // Test to enqueue and dequeue initial Metadata.
  HPackCompressor encoder;
  std::vector<Http2Frame> expected_frames;
  const uint32_t max_frame_length = kPathDemoServiceStep.size();
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
          /*is_client=*/true,
          /*stream_id=*/1,
          /*queue_size=*/10, kAllowTrueBinaryMetadataSetting);
  EnqueueInitialMetadataAndCheckSuccess(
      stream_data_queue, TestClientInitialMetadata(),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  GetExpectedHeaderAndContinuationFrames(max_frame_length, expected_frames,
                                         kPathDemoServiceStep,
                                         /*end_stream=*/false);
  DequeueAndCheckSuccess(stream_data_queue, std::move(expected_frames), encoder,
                         /*max_tokens=*/10, max_frame_length);
  EXPECT_TRUE(stream_data_queue->TestOnlyIsEmpty());
}

TEST(StreamDataQueueTest, ClientDequeueFramesTest) {
  // Test to enqueue multiple messages and dequeue frames. This test also
  // asserts the following:
  // 1. Dequeue returns as much data as possible with max_tokens as the upper
  //    limit.
  // 2. max_frame_length is respected.
  HPackCompressor encoder;
  const uint32_t max_frame_length = 17;
  std::vector<Http2Frame> expected_frames;
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
          /*is_client=*/true,
          /*stream_id=*/1,
          /*queue_size=*/10, kAllowTrueBinaryMetadataSetting);
  EnqueueInitialMetadataAndCheckSuccess(
      stream_data_queue, TestClientInitialMetadata(),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);

  GetExpectedHeaderAndContinuationFrames(max_frame_length, expected_frames,
                                         kPathDemoServiceStep,
                                         /*end_stream=*/false);
  DequeueAndCheckSuccess(stream_data_queue, std::move(expected_frames), encoder,
                         /*max_tokens=*/10,
                         /*max_frame_length=*/max_frame_length);

  EnqueueMessageAndCheckSuccess(
      stream_data_queue,
      TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(50)), 0),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  DequeueMessageAndCheckSuccess(stream_data_queue,
                                /*expected_frames_length=*/{10, 10, 10, 10, 10},
                                encoder,
                                /*max_tokens=*/50,
                                /*max_frame_length=*/10);
  DequeueMessageAndCheckSuccess(stream_data_queue,
                                /*expected_frames_length=*/{5}, encoder,
                                /*max_tokens=*/50,
                                /*max_frame_length=*/10);
  EXPECT_TRUE(stream_data_queue->TestOnlyIsEmpty());

  EnqueueMessageAndCheckSuccess(
      stream_data_queue,
      TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(50)), 0),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  DequeueMessageAndCheckSuccess(stream_data_queue,
                                /*expected_frames_length=*/{15, 10}, encoder,
                                /*max_tokens=*/25,
                                /*max_frame_length=*/15);
  DequeueMessageAndCheckSuccess(stream_data_queue,
                                /*expected_frames_length=*/{15, 10}, encoder,
                                /*max_tokens=*/25,
                                /*max_frame_length=*/15);
  DequeueMessageAndCheckSuccess(stream_data_queue,
                                /*expected_frames_length=*/{5}, encoder,
                                /*max_tokens=*/25,
                                /*max_frame_length=*/15);
  EXPECT_TRUE(stream_data_queue->TestOnlyIsEmpty());
}

TEST(StreamDataQueueTest, ClientEnqueueDequeueFlowTest) {
  // Test to enqueue and dequeue all the valid frames for a client.
  HPackCompressor encoder;
  const uint32_t max_frame_length = 8;
  std::vector<Http2Frame> expected_initial_metadata_frames;
  std::vector<int> expected_frames_length = {6};
  std::vector<Http2Frame> expected_close_frames;
  SliceBuffer expected_payload;
  AppendGrpcHeaderToSliceBuffer(expected_payload, /*flags=*/0, /*length=*/1);
  expected_payload.Append(Slice::ZeroContentsWithLength(1));
  expected_close_frames.emplace_back(
      Http2DataFrame{/*stream_id=*/1,
                     /*end_stream=*/false,
                     /*payload=*/std::move(expected_payload)});
  expected_close_frames.emplace_back(Http2DataFrame{/*stream_id=*/1,
                                                    /*end_stream=*/true,
                                                    /*payload=*/SliceBuffer()});
  expected_close_frames.emplace_back(
      Http2RstStreamFrame{/*stream_id=*/1, /*error_code=*/0});

  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
          /*is_client=*/true,
          /*stream_id=*/1,
          /*queue_size=*/10, kAllowTrueBinaryMetadataSetting);
  EnqueueInitialMetadataAndCheckSuccess(
      stream_data_queue, TestClientInitialMetadata(),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  EnqueueMessageAndCheckSuccess(
      stream_data_queue,
      TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(1)), 0),
      /*expected_writeable_state=*/false,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  EnqueueHalfClosedAndCheckSuccess(
      stream_data_queue,
      /*expected_writeable_state=*/false,
      /*expected_priority=*/WritableStreams::StreamPriority::kStreamClosed);
  EnqueueResetStreamAndCheckSuccess(
      stream_data_queue,
      /*expected_writeable_state=*/false,
      /*expected_priority=*/WritableStreams::StreamPriority::kStreamClosed);

  // Dequeue Initial Metadata
  GetExpectedHeaderAndContinuationFrames(
      max_frame_length, expected_initial_metadata_frames, kPathDemoServiceStep,
      /*end_stream=*/false);
  DequeueAndCheckSuccess(stream_data_queue,
                         std::move(expected_initial_metadata_frames), encoder,
                         /*max_tokens=*/0, max_frame_length);

  // Dequeue Message, Half Close and Reset Stream
  DequeueAndCheckSuccess(stream_data_queue, std::move(expected_close_frames),
                         encoder, /*max_tokens=*/6, max_frame_length);
  EXPECT_TRUE(stream_data_queue->TestOnlyIsEmpty());
}

////////////////////////////////////////////////////////////////////////////////
// Server Tests
TEST(StreamDataQueueTest, ServerEnqueueInitialMetadataTest) {
  // Simple test to enqueue initial metadata.
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ServerMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ServerMetadataHandle>>(
          /*is_client=*/false,
          /*stream_id=*/1,
          /*queue_size=*/10, kAllowTrueBinaryMetadataSetting);
  EnqueueInitialMetadataAndCheckSuccess(
      stream_data_queue, TestServerInitialMetadata(),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
}

TEST(StreamDataQueueTest, ServerEnqueueMultipleMessagesTest) {
  // Test to enqueue multiple messages upto the queue size. This tests expects
  // that all the enqueue promises are resolved immediately.
  HPackCompressor encoder;
  constexpr int num_messages = 10;
  constexpr int message_size = 1;
  constexpr int queued_size =
      num_messages * (message_size + kGrpcHeaderSizeInBytes);
  RefCountedPtr<StreamDataQueue<ServerMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ServerMetadataHandle>>(
          /*is_client=*/false,
          /*stream_id=*/1,
          /*queue_size=*/queued_size, kAllowTrueBinaryMetadataSetting);
  EnqueueInitialMetadataAndCheckSuccess(
      stream_data_queue, TestServerInitialMetadata(),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);

  for (int count = 0; count < 10; ++count) {
    EnqueueMessageAndCheckSuccess(
        stream_data_queue,
        TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(message_size)),
                    0),
        /*expected_writeable_state=*/false,
        /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  }
}

TEST(StreamDataQueueTest, ServerEnqueueTrailingMetadataTest) {
  // Test to enqueue initial Metadata, Message and Trailing Metadata. This
  // asserts the order of enqueue operations (initial metadata -> message ->
  // trailing metadata).
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ServerMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ServerMetadataHandle>>(
          /*is_client=*/false,
          /*stream_id=*/1,
          /*queue_size=*/10, kAllowTrueBinaryMetadataSetting);
  EnqueueInitialMetadataAndCheckSuccess(
      stream_data_queue, TestServerInitialMetadata(),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  EnqueueMessageAndCheckSuccess(
      stream_data_queue,
      TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(1)), 0),
      /*expected_writeable_state=*/false,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  EnqueueTrailingMetadataAndCheckSuccess(
      stream_data_queue, TestServerTrailingMetadata(),
      /*expected_writeable_state=*/false,
      /*expected_priority=*/WritableStreams::StreamPriority::kStreamClosed);
}

TEST(StreamDataQueueTest, ServerResetStreamTest) {
  // Test to assert that messages are optional and reset stream can be enqueued
  // after initial metadata.
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ServerMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ServerMetadataHandle>>(
          /*is_client=*/false,
          /*stream_id=*/1,
          /*queue_size=*/10, kAllowTrueBinaryMetadataSetting);
  EnqueueInitialMetadataAndCheckSuccess(
      stream_data_queue, TestServerInitialMetadata(),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  EnqueueResetStreamAndCheckSuccess(
      stream_data_queue,
      /*expected_writeable_state=*/false,
      /*expected_priority=*/WritableStreams::StreamPriority::kStreamClosed);
}

TEST(StreamDataQueueTest, ServerEnqueueDequeueFlowTest) {
  // Test to enqueue and dequeue all the valid frames for a server.
  HPackCompressor encoder;
  const uint32_t max_frame_length = 50;
  std::vector<Http2Frame> expected_initial_metadata_frames;
  std::vector<int> expected_frames_length = {6};
  std::vector<Http2Frame> expected_close_frames;
  SliceBuffer expected_payload;
  AppendGrpcHeaderToSliceBuffer(expected_payload, /*flags=*/0, /*length=*/1);
  expected_payload.Append(Slice::ZeroContentsWithLength(1));
  expected_close_frames.emplace_back(
      Http2DataFrame{/*stream_id=*/1,
                     /*end_stream=*/false,
                     /*payload=*/std::move(expected_payload)});
  GetExpectedHeaderAndContinuationFrames(
      max_frame_length, expected_close_frames, kPathDemoServiceStep3,
      /*end_stream=*/true);
  expected_close_frames.emplace_back(
      Http2RstStreamFrame{/*stream_id=*/1, /*error_code=*/0});

  RefCountedPtr<StreamDataQueue<ServerMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ServerMetadataHandle>>(
          /*is_client=*/false,
          /*stream_id=*/1,
          /*queue_size=*/10, kAllowTrueBinaryMetadataSetting);
  EnqueueInitialMetadataAndCheckSuccess(
      stream_data_queue, TestServerInitialMetadata(),
      /*expected_writeable_state=*/true,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  EnqueueMessageAndCheckSuccess(
      stream_data_queue,
      TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(1)), 0),
      /*expected_writeable_state=*/false,
      /*expected_priority=*/WritableStreams::StreamPriority::kDefault);
  EnqueueTrailingMetadataAndCheckSuccess(
      stream_data_queue, TestServerTrailingMetadata(),
      /*expected_writeable_state=*/false,
      /*expected_priority=*/WritableStreams::StreamPriority::kStreamClosed);
  EnqueueResetStreamAndCheckSuccess(
      stream_data_queue,
      /*expected_writeable_state=*/false,
      /*expected_priority=*/WritableStreams::StreamPriority::kStreamClosed);

  // Dequeue Initial Metadata
  GetExpectedHeaderAndContinuationFrames(
      max_frame_length, expected_initial_metadata_frames, kPathDemoServiceStep2,
      /*end_stream=*/false);
  DequeueAndCheckSuccess(stream_data_queue,
                         std::move(expected_initial_metadata_frames), encoder,
                         /*max_tokens=*/0, max_frame_length);

  // Dequeue Message, Trailing Metadata and Reset Stream
  DequeueAndCheckSuccess(stream_data_queue, std::move(expected_close_frames),
                         encoder, /*max_tokens=*/6, max_frame_length);
  EXPECT_TRUE(stream_data_queue->TestOnlyIsEmpty());
}

}  // namespace testing
}  // namespace http2

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
