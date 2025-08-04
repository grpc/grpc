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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/loop.h"
#include "test/core/transport/util/transport_test.h"

namespace grpc_core {
namespace http2 {
namespace testing {

namespace {
using ::testing::MockFunction;
using ::testing::StrictMock;
using util::testing::TransportTest;

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

void EnqueueAndCheckSuccess(WritableStreams& lows, const uint32_t stream_id,
                            const WritableStreams::StreamPriority priority) {
  auto promise = lows.Enqueue(stream_id, priority);
  auto result = promise();
  EXPECT_TRUE(result.ready());
  LOG(INFO) << "EnqueueAndCheckSuccess result " << result.value();
  EXPECT_TRUE(result.value().ok());
}
void SpawnEnqueueAndCheckSuccess(
    Party* party, WritableStreams& lows, const uint32_t stream_id,
    const WritableStreams::StreamPriority priority,
    absl::AnyInvocable<void(absl::Status)> on_complete) {
  LOG(INFO) << "Spawn EnqueueAndCheckSuccess for stream id " << stream_id
            << " with priority "
            << WritableStreams::GetPriorityString(priority);
  party->Spawn(
      "EnqueueAndCheckSuccess",
      [stream_id, priority, &lows] {
        return lows.Enqueue(stream_id, priority);
      },
      [stream_id,
       on_complete = std::move(on_complete)](absl::Status status) mutable {
        LOG(INFO) << "EnqueueAndCheckSuccess done for stream id " << stream_id
                  << " with status " << status;
        on_complete(status);
      });
}
void DequeueAndCheckSuccess(WritableStreams& lows,
                            const uint32_t expected_stream_id) {
  auto promise = lows.Next(/*transport_tokens_available=*/true);
  auto result = promise();
  EXPECT_TRUE(result.ready());
  EXPECT_TRUE(result.value().ok());
  LOG(INFO) << "DequeueAndCheckSuccess result " << result.value()
            << " stream id " << *(result.value());
  EXPECT_EQ(*result.value(), expected_stream_id);
}

}  // namespace

/////////////////////////////////////////////////////////////////////////////////
// Enqueue tests
TEST_F(WritableStreamsTest, EnqueueTest) {
  // Simple test to ensure that enqueue promise works.
  WritableStreams lows(/*max_queue_size=*/1);
  SpawnEnqueueAndCheckSuccess(
      GetParty(), lows, /*stream_id=*/1,
      WritableStreams::StreamPriority::kDefault,
      [](absl::Status status) { EXPECT_TRUE(status.ok()); });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(WritableStreamsTest, MultipleEnqueueTest) {
  // Test to ensure that multiple enqueues up to the max queue size resolve
  // immediately.
  WritableStreams lows(/*max_queue_size=*/3);
  std::string execution_order;
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/1,
                              WritableStreams::StreamPriority::kDefault,
                              [&execution_order](absl::Status status) {
                                execution_order.append("1");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/2,
                              WritableStreams::StreamPriority::kStreamClosed,
                              [&execution_order](absl::Status status) {
                                execution_order.append("2");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(
      GetParty(), lows, /*stream_id=*/3,
      WritableStreams::StreamPriority::kWaitForTransportFlowControl,
      [&execution_order](absl::Status status) {
        execution_order.append("3");
        EXPECT_TRUE(status.ok());
      });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
  EXPECT_STREQ(execution_order.c_str(), "123");
}

/////////////////////////////////////////////////////////////////////////////////
// Dequeue tests
TEST_F(WritableStreamsTest, EnqueueDequeueTest) {
  // Simple test to ensure that enqueue and dequeue works.
  WritableStreams lows(/*max_queue_size=*/1);
  EnqueueAndCheckSuccess(lows, /*stream_id=*/1,
                         WritableStreams::StreamPriority::kDefault);
  DequeueAndCheckSuccess(lows, /*expected_stream_id=*/1);

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(WritableStreamsTest, MultipleEnqueueDequeueTest) {
  // Test to ensure that multiple enqueues and dequeues works. This test also
  // simulates the case where the queue is full and the producer is blocked on
  // the queue.
  WritableStreams lows(/*max_queue_size=*/1);
  uint dequeue_count = 0;
  std::vector<uint32_t> expected_stream_ids = {1, 2, 3};
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());

  SpawnEnqueueAndCheckSuccess(
      GetParty(), lows, /*stream_id=*/1,
      WritableStreams::StreamPriority::kDefault,
      [](absl::Status status) { EXPECT_TRUE(status.ok()); });
  SpawnEnqueueAndCheckSuccess(
      GetParty(), lows, /*stream_id=*/2,
      WritableStreams::StreamPriority::kDefault,
      [](absl::Status status) { EXPECT_TRUE(status.ok()); });
  SpawnEnqueueAndCheckSuccess(
      GetParty(), lows, /*stream_id=*/3,
      WritableStreams::StreamPriority::kDefault,
      [](absl::Status status) { EXPECT_TRUE(status.ok()); });

  GetParty()->Spawn(
      "Dequeue",
      [&lows, &dequeue_count, &expected_stream_ids, &on_done] {
        return Loop([&lows, &dequeue_count, &expected_stream_ids, &on_done]() {
          return If(
              dequeue_count < expected_stream_ids.size(),
              [&lows, &dequeue_count, &expected_stream_ids, &on_done]() {
                return Map(lows.Next(/*transport_tokens_available=*/true),
                           [&dequeue_count, &expected_stream_ids,
                            &on_done](absl::StatusOr<uint32_t> result)
                               -> LoopCtl<absl::Status> {
                             EXPECT_TRUE(result.ok());
                             EXPECT_EQ(result.value(),
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
  WritableStreams lows(/*max_queue_size=*/3);
  std::string execution_order;
  uint dequeue_count = 0;
  std::vector<uint32_t> expected_stream_ids = {2, 3, 1};
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());

  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/1,
                              WritableStreams::StreamPriority::kDefault,
                              [&execution_order](absl::Status status) {
                                execution_order.append("1");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/2,
                              WritableStreams::StreamPriority::kStreamClosed,
                              [&execution_order](absl::Status status) {
                                execution_order.append("2");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(
      GetParty(), lows, /*stream_id=*/3,
      WritableStreams::StreamPriority::kWaitForTransportFlowControl,
      [&execution_order](absl::Status status) {
        execution_order.append("3");
        EXPECT_TRUE(status.ok());
      });

  GetParty()->Spawn(
      "Dequeue",
      [&lows, &dequeue_count, &expected_stream_ids, &on_done] {
        return Loop([&lows, &dequeue_count, &expected_stream_ids, &on_done]() {
          return If(
              dequeue_count < expected_stream_ids.size(),
              [&lows, &dequeue_count, &expected_stream_ids, &on_done]() {
                return Map(lows.Next(/*transport_tokens_available=*/true),
                           [&dequeue_count, &expected_stream_ids,
                            &on_done](absl::StatusOr<uint32_t> result)
                               -> LoopCtl<absl::Status> {
                             EXPECT_TRUE(result.ok());
                             EXPECT_EQ(result.value(),
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
  EXPECT_STREQ(execution_order.c_str(), "123");
}

TEST_F(WritableStreamsTest, DequeueWithTransportTokensUnavailableTest) {
  // Test to ensure that stream ids waiting on transport flow control are
  // dequeued only when transport tokens are available. The enqueues are done
  // upto the max queue size and the dequeue is done for all the stream ids.
  WritableStreams lows(/*max_queue_size=*/3);
  std::string execution_order;
  uint dequeue_count = 0;
  std::vector<uint32_t> expected_stream_ids = {2, 1, 3};
  std::vector<bool> transport_tokens_available = {true, false, true};
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());

  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/1,
                              WritableStreams::StreamPriority::kDefault,
                              [&execution_order](absl::Status status) {
                                execution_order.append("1");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/2,
                              WritableStreams::StreamPriority::kStreamClosed,
                              [&execution_order](absl::Status status) {
                                execution_order.append("2");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(
      GetParty(), lows, /*stream_id=*/3,
      WritableStreams::StreamPriority::kWaitForTransportFlowControl,
      [&execution_order](absl::Status status) {
        execution_order.append("3");
        EXPECT_TRUE(status.ok());
      });

  GetParty()->Spawn(
      "Dequeue",
      [&lows, &dequeue_count, &expected_stream_ids, &transport_tokens_available,
       &on_done] {
        return Loop([&lows, &dequeue_count, &expected_stream_ids,
                     &transport_tokens_available, &on_done]() {
          return If(
              dequeue_count < expected_stream_ids.size(),
              [&lows, &dequeue_count, &expected_stream_ids,
               &transport_tokens_available, &on_done]() {
                return Map(lows.Next(
                               /*transport_tokens_available=*/
                               transport_tokens_available[dequeue_count]),
                           [&dequeue_count, &expected_stream_ids,
                            &on_done](absl::StatusOr<uint32_t> result)
                               -> LoopCtl<absl::Status> {
                             EXPECT_TRUE(result.ok());
                             EXPECT_EQ(result.value(),
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
  EXPECT_STREQ(execution_order.c_str(), "123");
}

TEST_F(WritableStreamsTest, EnqueueDequeueFlowTest) {
  // Test to enqueue 4 stream ids and dequeue them as the queue is full. The
  // test asserts the following:
  // 1. Stream ids are dequeued in the correct order based on their priorities.
  //    This includes the case where if a new stream id with a higher priority
  //    is enqueued, the stream id is dequeued before the already stream ids
  //    in the queue.
  WritableStreams lows(/*max_queue_size=*/2);
  std::string execution_order;
  std::vector<uint32_t> expected_stream_ids = {2, 3, 1, 4};
  uint dequeue_count = 0;
  std::vector<bool> transport_tokens_available = {true, true, true, false};
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());

  GetParty()->Spawn(
      "EnqueueAndDequeue",
      [&lows, &dequeue_count, &expected_stream_ids, &transport_tokens_available,
       &on_done] {
        return TrySeq(
            lows.Enqueue(/*stream_id=*/1,
                         WritableStreams::StreamPriority::kDefault),
            [&lows] {
              return lows.Enqueue(
                  /*stream_id=*/2,
                  WritableStreams::StreamPriority::kStreamClosed);
            },
            [&lows, &transport_tokens_available, &dequeue_count] {
              return lows.Next(transport_tokens_available[dequeue_count]);
            },
            [&lows, &dequeue_count, &expected_stream_ids,
             &on_done](uint32_t stream_id) {
              EXPECT_EQ(stream_id, expected_stream_ids[dequeue_count++]);
              on_done.Call();
              return lows.Enqueue(
                  /*stream_id=*/3, WritableStreams::StreamPriority::
                                       kWaitForTransportFlowControl);
            },
            [&lows, &transport_tokens_available, &dequeue_count] {
              return lows.Next(transport_tokens_available[dequeue_count]);
            },
            [&lows, &dequeue_count, &expected_stream_ids,
             &on_done](uint32_t stream_id) {
              EXPECT_EQ(stream_id, expected_stream_ids[dequeue_count++]);
              on_done.Call();
              return lows.Enqueue(/*stream_id=*/4,
                                  WritableStreams::StreamPriority::kDefault);
            },
            [&lows, &transport_tokens_available, &dequeue_count] {
              return lows.Next(transport_tokens_available[dequeue_count]);
            },
            [&lows, &dequeue_count, &expected_stream_ids,
             &transport_tokens_available, &on_done](uint32_t stream_id) {
              EXPECT_EQ(stream_id, expected_stream_ids[dequeue_count++]);
              on_done.Call();
              return Map(lows.Next(transport_tokens_available[dequeue_count]),
                         [&expected_stream_ids, &dequeue_count,
                          &on_done](absl::StatusOr<uint32_t> stream_id) {
                           EXPECT_TRUE(stream_id.ok());
                           EXPECT_EQ(*stream_id,
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
