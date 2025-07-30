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
      [data, tokens](auto result) {
        LOG(INFO) << "Enqueue done for data: " << data << " tokens: " << tokens;
        EXPECT_EQ(result.status, absl::OkStatus());
      });
}

void DequeueAndCheckPending(http2::SimpleQueue<int>& queue,
                            bool allow_oversized_dequeue,
                            int allowed_dequeue_tokens) {
  LOG(INFO) << "DequeueAndCheckPending for allow_oversized_dequeue: "
            << allow_oversized_dequeue
            << " allowed_dequeue_tokens: " << allowed_dequeue_tokens;
  auto result = queue.Dequeue(allowed_dequeue_tokens, allow_oversized_dequeue);
  EXPECT_FALSE(result.has_value());
}

void DequeueAndCheckSuccess(http2::SimpleQueue<int>& queue, int data,
                            bool allow_oversized_dequeue,
                            int allowed_dequeue_tokens) {
  LOG(INFO) << "DequeueAndCheckSuccess for data: " << data
            << " allow_oversized_dequeue: " << allow_oversized_dequeue
            << " allowed_dequeue_tokens: " << allowed_dequeue_tokens;
  auto result = queue.Dequeue(allowed_dequeue_tokens, allow_oversized_dequeue);
  EXPECT_TRUE(result.has_value());

  LOG(INFO) << "Dequeue successful for data: " << data;
  EXPECT_EQ(result.value(), data);
}

bool DequeueAndCheck(http2::SimpleQueue<int>& queue, int data,
                     bool allow_oversized_dequeue, int allowed_dequeue_tokens) {
  LOG(INFO) << "DequeueAndCheck for data: " << data
            << " allow_oversized_dequeue: " << allow_oversized_dequeue
            << " allowed_dequeue_tokens: " << allowed_dequeue_tokens;
  auto result = queue.Dequeue(allowed_dequeue_tokens, allow_oversized_dequeue);
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
  auto* party = GetParty();
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));

  party->Spawn("EnqueueTest",
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
  auto* party = GetParty();
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));

  party->Spawn("EnqueueTest",
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
  auto* party = GetParty();
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  int count = 10;

  party->Spawn("EnqueueTest", Loop([&count, &queue, &on_done]() {
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

  auto result = queue.Dequeue(/*allowed_dequeue_tokens=*/10,
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
  auto* party = GetParty();
  StrictMock<MockFunction<void(absl::Status)>> on_enqueue_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_enqueue_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));

  party->Spawn("EnqueueTest",
               EnqueueAndCheckSuccess(queue, /*data=*/1, /*tokens=*/10),
               [&on_enqueue_done, &enqueue_done](auto) {
                 LOG(INFO) << "Reached end of EnqueueTest";
                 on_enqueue_done.Call(absl::OkStatus());
                 enqueue_done.Set();
               });

  party->Spawn("DequeueTest",
               Map(enqueue_done.Wait(),
                   [&queue, &on_dequeue_done](auto) {
                     DequeueAndCheckSuccess(queue, /*data=*/1,
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
  auto* party = GetParty();
  StrictMock<MockFunction<void(absl::Status)>> on_enqueue_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_enqueue_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));

  party->Spawn(
      "EnqueueTest",
      TrySeq(EnqueueAndCheckSuccess(queue, /*data=*/1, /*tokens=*/99),
             EnqueueAndCheckSuccess(queue, /*data=*/2, /*tokens=*/100)),
      [&on_enqueue_done, &enqueue_done](auto) {
        LOG(INFO) << "Reached end of EnqueueTest";
        on_enqueue_done.Call(absl::OkStatus());
        enqueue_done.Set();
      });

  party->Spawn(
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
  auto* party = GetParty();
  StrictMock<MockFunction<void(absl::Status)>> on_enqueue_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_enqueue_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));

  party->Spawn("EnqueueTest",
               TrySeq(EnqueueAndCheckSuccess(queue, /*data=*/1,
                                             /*tokens=*/100),
                      EnqueueAndCheckSuccess(queue, /*data=*/2,
                                             /*tokens=*/99)),
               [&on_enqueue_done, &enqueue_done](auto) {
                 LOG(INFO) << "Reached end of EnqueueTest";
                 on_enqueue_done.Call(absl::OkStatus());
                 enqueue_done.Set();
               });

  party->Spawn(
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
  auto* party = GetParty();
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));
  int dequeue_count = 2;
  std::string execution_order;
  std::vector<int> expected_data = {1, 2};
  int expected_data_index = 0;

  party->Spawn("EnqueueTest",
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

  party->Spawn(
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
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step"));
  return md;
}

ServerMetadataHandle TestServerInitialMetadata() {
  auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step2"));
  return md;
}

ServerMetadataHandle TestServerTrailingMetadata() {
  auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step3"));
  return md;
}

MessageHandle TestMessage(SliceBuffer payload, const uint32_t flags) {
  return Arena::MakePooled<Message>(std::move(payload), flags);
}

template <typename MetadataHandle>
void EnqueueInitialMetadataAndCheckSuccess(
    RefCountedPtr<StreamDataQueue<MetadataHandle>> queue,
    MetadataHandle metadata) {
  LOG(INFO) << "Enqueueing initial metadata";
  auto promise = queue->EnqueueInitialMetadata(std::move(metadata));
  auto result = promise();
  ASSERT_TRUE(result.ready());
  EXPECT_EQ(result.value(), absl::OkStatus());
  LOG(INFO) << "Enqueueing initial metadata success";
}

template <typename MetadataHandle>
void EnqueueTrailingMetadataAndCheckSuccess(
    RefCountedPtr<StreamDataQueue<MetadataHandle>> queue,
    MetadataHandle metadata) {
  LOG(INFO) << "Enqueueing trailing metadata";
  auto promise = queue->EnqueueTrailingMetadata(std::move(metadata));
  auto result = promise();
  ASSERT_TRUE(result.ready());
  EXPECT_EQ(result.value(), absl::OkStatus());
  LOG(INFO) << "Enqueueing trailing metadata success";
}

template <typename MetadataHandle>
void EnqueueMessageAndCheckSuccess(
    RefCountedPtr<StreamDataQueue<MetadataHandle>> queue,
    MessageHandle message) {
  LOG(INFO) << "Enqueueing message with tokens: "
            << message->payload()->Length()
            << " and flags: " << message->flags();
  auto promise = queue->EnqueueMessage(std::move(message));
  auto result = promise();
  ASSERT_TRUE(result.ready());
  EXPECT_EQ(result.value(), absl::OkStatus());
  LOG(INFO) << "Enqueueing message success";
}

template <typename MetadataHandle>
void EnqueueResetStreamAndCheckSuccess(
    RefCountedPtr<StreamDataQueue<MetadataHandle>> queue) {
  LOG(INFO) << "Enqueueing reset stream";
  auto promise = queue->EnqueueResetStream(/*error_code=*/0);
  auto result = promise();
  ASSERT_TRUE(result.ready());
  EXPECT_EQ(result.value(), absl::OkStatus());
  LOG(INFO) << "Enqueueing reset stream success";
}

void EnqueueHalfClosedAndCheckSuccess(
    RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> queue) {
  LOG(INFO) << "Enqueueing half closed";
  auto promise = queue->EnqueueHalfClosed();
  auto result = promise();
  ASSERT_TRUE(result.ready());
  EXPECT_EQ(result.value(), absl::OkStatus());
  LOG(INFO) << "Enqueueing half closed success";
}
}  // namespace

////////////////////////////////////////////////////////////////////////////////
// Client Tests
TEST(StreamDataQueueTest, ClientEnqueueInitialMetadataTest) {
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
          /*is_client=*/true,
          /*stream_id=*/1,
          /*queue_size=*/10);
  EnqueueInitialMetadataAndCheckSuccess(stream_data_queue,
                                        TestClientInitialMetadata());
}

TEST(StreamDataQueueTest, ClientEnqueueMultipleMessagesTest) {
  HPackCompressor encoder;
  constexpr int num_messages = 10;
  constexpr int message_size = 1;
  constexpr int queued_size =
      num_messages * (message_size + kGrpcHeaderSizeInBytes);
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
          /*is_client=*/true,
          /*stream_id=*/1,
          /*queue_size=*/queued_size);
  EnqueueInitialMetadataAndCheckSuccess(stream_data_queue,
                                        TestClientInitialMetadata());

  for (int count = 0; count < 10; ++count) {
    EnqueueMessageAndCheckSuccess(
        stream_data_queue,
        TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(message_size)),
                    0));
  }
}

TEST(StreamDataQueueTest, ClientEnqueueEndStreamTest) {
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
          /*is_client=*/true,
          /*stream_id=*/1,
          /*queue_size=*/10);
  EnqueueInitialMetadataAndCheckSuccess(stream_data_queue,
                                        TestClientInitialMetadata());
  EnqueueMessageAndCheckSuccess(
      stream_data_queue,
      TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(1)), 0));
  EnqueueHalfClosedAndCheckSuccess(stream_data_queue);
}

TEST(StreamDataQueueTest, ClientEnqueueResetStreamTest) {
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
          /*is_client=*/true,
          /*stream_id=*/1,
          /*queue_size=*/10);
  EnqueueInitialMetadataAndCheckSuccess(stream_data_queue,
                                        TestClientInitialMetadata());
  EnqueueResetStreamAndCheckSuccess(stream_data_queue);
}

////////////////////////////////////////////////////////////////////////////////
// Server Tests
TEST(StreamDataQueueTest, ServerEnqueueInitialMetadataTest) {
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ServerMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ServerMetadataHandle>>(
          /*is_client=*/false,
          /*stream_id=*/1,
          /*queue_size=*/10);
  EnqueueInitialMetadataAndCheckSuccess(stream_data_queue,
                                        TestServerInitialMetadata());
}

TEST(StreamDataQueueTest, ServerEnqueueMultipleMessagesTest) {
  HPackCompressor encoder;
  constexpr int num_messages = 10;
  constexpr int message_size = 1;
  constexpr int queued_size =
      num_messages * (message_size + kGrpcHeaderSizeInBytes);
  RefCountedPtr<StreamDataQueue<ServerMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ServerMetadataHandle>>(
          /*is_client=*/false,
          /*stream_id=*/1,
          /*queue_size=*/queued_size);
  EnqueueInitialMetadataAndCheckSuccess(stream_data_queue,
                                        TestServerInitialMetadata());

  for (int count = 0; count < 10; ++count) {
    EnqueueMessageAndCheckSuccess(
        stream_data_queue,
        TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(message_size)),
                    0));
  }
}

TEST(StreamDataQueueTest, ServerEnqueueTrailingMetadataTest) {
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ServerMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ServerMetadataHandle>>(
          /*is_client=*/false,
          /*stream_id=*/1,
          /*queue_size=*/10);
  EnqueueInitialMetadataAndCheckSuccess(stream_data_queue,
                                        TestServerInitialMetadata());
  EnqueueMessageAndCheckSuccess(
      stream_data_queue,
      TestMessage(SliceBuffer(Slice::ZeroContentsWithLength(1)), 0));
  EnqueueTrailingMetadataAndCheckSuccess(stream_data_queue,
                                         TestServerTrailingMetadata());
}

TEST(StreamDataQueueTest, ServerResetStreamTest) {
  HPackCompressor encoder;
  RefCountedPtr<StreamDataQueue<ServerMetadataHandle>> stream_data_queue =
      MakeRefCounted<StreamDataQueue<ServerMetadataHandle>>(
          /*is_client=*/false,
          /*stream_id=*/1,
          /*queue_size=*/10);
  EnqueueInitialMetadataAndCheckSuccess(stream_data_queue,
                                        TestServerInitialMetadata());
  EnqueueResetStreamAndCheckSuccess(stream_data_queue);
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
