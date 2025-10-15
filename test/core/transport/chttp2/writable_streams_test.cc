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

#include "src/core/ext/transport/chttp2/transport/writable_streams.h"

#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/loop.h"
#include "test/core/transport/util/transport_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace http2 {
namespace testing {

namespace {
using ::testing::MockFunction;
using ::testing::StrictMock;
using util::testing::TransportTest;

struct TestStream : public RefCounted<TestStream> {
  explicit TestStream(uint32_t stream_id) : stream_id(stream_id) {}
  uint32_t stream_id;
  uint32_t GetStreamId() const { return stream_id; }
};

using Stream = TestStream;
using WritableStreams = http2::WritableStreams<RefCountedPtr<Stream>>;

class WritableStreamsTest : public TransportTest {
 protected:
  WritableStreamsTest() { InitParty(); }

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

void EnqueueAndCheckSuccess(WritableStreams& writable_streams,
                            RefCountedPtr<Stream> stream,
                            const WritableStreamPriority priority) {
  absl::Status status;
  if (priority != WritableStreamPriority::kWaitForTransportFlowControl) {
    status = writable_streams.Enqueue(stream, priority);
  } else {
    status = writable_streams.BlockedOnTransportFlowControl(stream);
  }
  LOG(INFO) << "EnqueueAndCheckSuccess result " << status;
  EXPECT_TRUE(status.ok());
}

auto DequeuePromise(WritableStreams& writable_streams,
                    const bool transport_tokens_available = true,
                    const bool expect_result = true) {
  return AssertResultType<std::optional<RefCountedPtr<Stream>>>(
      Map(writable_streams.WaitForReady(transport_tokens_available),
          [&writable_streams, transport_tokens_available,
           expect_result](absl::StatusOr<Empty> result)
              -> std::optional<RefCountedPtr<Stream>> {
            EXPECT_TRUE(result.ok()) << result.status();

            std::optional<RefCountedPtr<Stream>> stream =
                writable_streams.ImmediateNext(transport_tokens_available);
            LOG(INFO) << "DequeuePromise result returned with"
                      << " stream id "
                      << (stream.has_value()
                              ? (*stream)->GetStreamId()
                              : std::numeric_limits<uint32_t>::max());
            if (expect_result) {
              EXPECT_TRUE(stream.has_value());
              return stream;
            }
            return std::nullopt;
          }));
}

void DequeueAndCheckSuccess(WritableStreams& writable_streams,
                            const uint32_t expected_stream_id) {
  auto promise =
      DequeuePromise(writable_streams, /*transport_tokens_available=*/true);
  Poll<std::optional<RefCountedPtr<Stream>>> result = promise();
  EXPECT_TRUE(result.ready());
  EXPECT_TRUE(result.value().has_value());
  LOG(INFO) << "DequeueAndCheckSuccess result returned with"
            << " stream id " << (result.value().value()->GetStreamId());
  EXPECT_EQ(result.value().value()->GetStreamId(), expected_stream_id);
}

absl::Status ForceReadyForWriteAndCheckSuccess(
    WritableStreams& writable_streams) {
  absl::Status status = writable_streams.ForceReadyForWrite();
  EXPECT_TRUE(status.ok()) << status;
  return status;
}

}  // namespace

/////////////////////////////////////////////////////////////////////////////////
// Enqueue tests
TEST_F(WritableStreamsTest, EnqueueTest) {
  // Simple test to ensure that enqueue promise works.
  WritableStreams writable_streams(/*max_queue_size=*/1);
  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(1),
                         WritableStreamPriority::kDefault);

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(WritableStreamsTest, MultipleEnqueueTest) {
  // Test to ensure that multiple enqueues up to the max queue size resolve
  // immediately.
  WritableStreams writable_streams(/*max_queue_size=*/3);
  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(1),
                         WritableStreamPriority::kDefault);
  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(3),
                         WritableStreamPriority::kStreamClosed);
  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(5),
                         WritableStreamPriority::kWaitForTransportFlowControl);

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

/////////////////////////////////////////////////////////////////////////////////
// Dequeue tests
TEST_F(WritableStreamsTest, EnqueueDequeueTest) {
  // Simple test to ensure that enqueue and dequeue works.
  // TODO(akshitpatel) : [PH2][P2] - Make this parameterized.
  WritableStreams writable_streams(/*max_queue_size=*/1);
  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(1),
                         WritableStreamPriority::kDefault);
  DequeueAndCheckSuccess(writable_streams, /*expected_stream_id=*/1);

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(WritableStreamsTest, MultipleEnqueueDequeueTest) {
  // Test to ensure that multiple enqueues and dequeues works. This test also
  // simulates the case where the queue is full and the producer is blocked on
  // the queue.
  // TODO(akshitpatel) : [PH2][P2] - Make this parameterized.
  WritableStreams writable_streams(/*max_queue_size=*/1);
  uint32_t dequeue_count = 0;
  std::vector<uint32_t> expected_stream_ids = {1, 3, 5};
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());

  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(1),
                         WritableStreamPriority::kDefault);
  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(3),
                         WritableStreamPriority::kDefault);
  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(5),
                         WritableStreamPriority::kDefault);

  GetParty()->Spawn(
      "Dequeue",
      [&writable_streams, &dequeue_count, &expected_stream_ids, &on_done] {
        return Loop([&writable_streams, &dequeue_count, &expected_stream_ids,
                     &on_done]() {
          return If(
              dequeue_count < expected_stream_ids.size(),
              [&writable_streams, &dequeue_count, &expected_stream_ids,
               &on_done]() {
                return Map(
                    DequeuePromise(writable_streams,
                                   /*transport_tokens_available=*/true),
                    [&dequeue_count, &expected_stream_ids, &on_done](
                        absl::StatusOr<std::optional<RefCountedPtr<Stream>>>
                            result) -> LoopCtl<absl::Status> {
                      EXPECT_TRUE(result.ok());
                      EXPECT_TRUE(result.value().has_value());
                      EXPECT_EQ(result.value().value()->GetStreamId(),
                                expected_stream_ids[dequeue_count++]);
                      on_done.Call();
                      return Continue();
                    });
              },
              []() -> LoopCtl<absl::Status> { return absl::OkStatus(); });
        });
      },
      [](auto) {});

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(WritableStreamsTest, EnqueueDequeueDifferentPriorityTest) {
  // Test to ensure that stream ids are dequeued in the correct order based on
  // their priorities. The enqueues are done upto the max queue size and the
  // dequeue is done for all the stream ids.
  WritableStreams writable_streams(/*max_queue_size=*/3);
  uint32_t dequeue_count = 0;
  std::vector<uint32_t> expected_stream_ids = {3, 5, 1};
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());

  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(1),
                         WritableStreamPriority::kDefault);
  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(3),
                         WritableStreamPriority::kStreamClosed);
  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(5),
                         WritableStreamPriority::kWaitForTransportFlowControl);

  GetParty()->Spawn(
      "Dequeue",
      [&writable_streams, &dequeue_count, &expected_stream_ids, &on_done] {
        return Loop([&writable_streams, &dequeue_count, &expected_stream_ids,
                     &on_done]() {
          return If(
              dequeue_count < expected_stream_ids.size(),
              [&writable_streams, &dequeue_count, &expected_stream_ids,
               &on_done]() {
                return Map(
                    DequeuePromise(writable_streams,
                                   /*transport_tokens_available=*/true),
                    [&dequeue_count, &expected_stream_ids, &on_done](
                        absl::StatusOr<std::optional<RefCountedPtr<Stream>>>
                            result) -> LoopCtl<absl::Status> {
                      EXPECT_TRUE(result.ok());
                      EXPECT_TRUE(result.value().has_value());
                      EXPECT_EQ(result.value().value()->GetStreamId(),
                                expected_stream_ids[dequeue_count++]);
                      on_done.Call();
                      return Continue();
                    });
              },
              []() -> LoopCtl<absl::Status> { return absl::OkStatus(); });
        });
      },
      [](auto) {});
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(WritableStreamsTest, DequeueWithTransportTokensUnavailableTest) {
  // Test to ensure that stream ids waiting on transport flow control are
  // dequeued only when transport tokens are available. The enqueues are done
  // upto the max queue size and the dequeue is done for all the stream ids.
  WritableStreams writable_streams(/*max_queue_size=*/3);
  uint32_t dequeue_count = 0;
  std::vector<uint32_t> expected_stream_ids = {3, 1, 5};
  std::vector<bool> transport_tokens_available = {true, false, true};
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());

  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(1),
                         WritableStreamPriority::kDefault);
  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(3),
                         WritableStreamPriority::kStreamClosed);
  EnqueueAndCheckSuccess(writable_streams,
                         /*stream=*/MakeRefCounted<Stream>(5),
                         WritableStreamPriority::kWaitForTransportFlowControl);

  GetParty()->Spawn(
      "Dequeue",
      [&writable_streams, &dequeue_count, &expected_stream_ids,
       &transport_tokens_available, &on_done] {
        return Loop([&writable_streams, &dequeue_count, &expected_stream_ids,
                     &transport_tokens_available, &on_done]() {
          return If(
              dequeue_count < expected_stream_ids.size(),
              [&writable_streams, &dequeue_count, &expected_stream_ids,
               &transport_tokens_available, &on_done]() {
                return Map(
                    DequeuePromise(writable_streams,
                                   /*transport_tokens_available=*/
                                   transport_tokens_available[dequeue_count]),
                    [&dequeue_count, &expected_stream_ids, &on_done](
                        absl::StatusOr<std::optional<RefCountedPtr<Stream>>>
                            result) -> LoopCtl<absl::Status> {
                      EXPECT_TRUE(result.ok());
                      EXPECT_TRUE(result.value().has_value());
                      EXPECT_EQ(result.value().value()->GetStreamId(),
                                expected_stream_ids[dequeue_count++]);
                      on_done.Call();
                      return Continue();
                    });
              },
              []() -> LoopCtl<absl::Status> { return absl::OkStatus(); });
        });
      },
      [](auto) {});
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(WritableStreamsTest, EnqueueDequeueFlowTest) {
  // Test to enqueue 4 stream ids and dequeue them as the queue is full. The
  // test asserts the following:
  // 1. Stream ids are dequeued in the correct order based on their priorities.
  //    This includes the case where if a new stream id with a higher priority
  //    is enqueued, the stream id is dequeued before the already stream ids
  //    in the queue.
  WritableStreams writable_streams(/*max_queue_size=*/2);
  std::vector<uint32_t> expected_stream_ids = {3, 5, 1, 7};
  uint32_t dequeue_count = 0;
  std::vector<bool> transport_tokens_available = {true, true, true, false};
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());

  GetParty()->Spawn(
      "EnqueueAndDequeue",
      [&writable_streams, &dequeue_count, &expected_stream_ids,
       &transport_tokens_available, &on_done] {
        return TrySeq(
            [&writable_streams]() {
              return writable_streams.Enqueue(
                  /*stream=*/MakeRefCounted<Stream>(1),
                  WritableStreamPriority::kDefault);
            },
            [&writable_streams] {
              return writable_streams.Enqueue(
                  /*stream=*/MakeRefCounted<Stream>(3),
                  WritableStreamPriority::kStreamClosed);
            },
            [&writable_streams, &transport_tokens_available, &dequeue_count] {
              return DequeuePromise(writable_streams,
                                    transport_tokens_available[dequeue_count]);
            },
            [&writable_streams, &dequeue_count, &expected_stream_ids,
             &on_done](std::optional<RefCountedPtr<Stream>> stream) {
              EXPECT_TRUE(stream.has_value());
              EXPECT_EQ(stream.value()->GetStreamId(),
                        expected_stream_ids[dequeue_count++]);
              on_done.Call();
              return writable_streams.BlockedOnTransportFlowControl(
                  /*stream=*/MakeRefCounted<Stream>(5));
            },
            [&writable_streams, &transport_tokens_available, &dequeue_count] {
              return DequeuePromise(writable_streams,
                                    transport_tokens_available[dequeue_count]);
            },
            [&writable_streams, &dequeue_count, &expected_stream_ids,
             &on_done](std::optional<RefCountedPtr<Stream>> stream) {
              EXPECT_TRUE(stream.has_value());
              EXPECT_EQ(stream.value()->GetStreamId(),
                        expected_stream_ids[dequeue_count++]);
              on_done.Call();
              return writable_streams.Enqueue(
                  /*stream=*/MakeRefCounted<Stream>(7),
                  WritableStreamPriority::kDefault);
            },
            [&writable_streams, &transport_tokens_available, &dequeue_count] {
              return DequeuePromise(writable_streams,
                                    transport_tokens_available[dequeue_count]);
            },
            [&writable_streams, &dequeue_count, &expected_stream_ids,
             &transport_tokens_available,
             &on_done](std::optional<RefCountedPtr<Stream>> stream) {
              EXPECT_TRUE(stream.has_value());
              EXPECT_EQ(stream.value()->GetStreamId(),
                        expected_stream_ids[dequeue_count++]);
              on_done.Call();
              return Map(
                  DequeuePromise(writable_streams,
                                 transport_tokens_available[dequeue_count]),
                  [&expected_stream_ids, &dequeue_count, &on_done](
                      absl::StatusOr<std::optional<RefCountedPtr<Stream>>>
                          stream_id) {
                    EXPECT_TRUE(stream_id.ok());
                    EXPECT_TRUE(stream_id.value().has_value());
                    EXPECT_EQ(stream_id.value().value()->GetStreamId(),
                              expected_stream_ids[dequeue_count++]);
                    on_done.Call();
                    return absl::OkStatus();
                  });
            });
      },
      [](auto) {});

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(WritableStreamsTest, TestForceReadyForWrite) {
  // Test to ensure that ForceReadyForWrite unblocks the pending waiter on
  // WaitForReady. This test also asserts that ForceReadyForWrite can be called
  // with no waiters on WaitForReady.
  WritableStreams writable_streams(/*max_queue_size=*/2);
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(2);

  GetParty()->Spawn(
      "ForceReadyForWriteAndDequeue",
      TrySeq(
          [&writable_streams]() {
            return ForceReadyForWriteAndCheckSuccess(writable_streams);
          },
          [&writable_streams]() {
            return Join(
                DequeuePromise(writable_streams,
                               /*transport_tokens_available=*/true,
                               /*expect_result=*/false),
                [&writable_streams]() {
                  return ForceReadyForWriteAndCheckSuccess(writable_streams);
                });
          },
          [&writable_streams, &on_done]() {
            on_done.Call();
            return Join(
                DequeuePromise(writable_streams,
                               /*transport_tokens_available=*/true,
                               /*expect_result=*/false),
                [&writable_streams]() {
                  return ForceReadyForWriteAndCheckSuccess(writable_streams);
                });
          },
          [&on_done]() {
            on_done.Call();
            return absl::OkStatus();
          }),
      [](auto) {});

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
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
