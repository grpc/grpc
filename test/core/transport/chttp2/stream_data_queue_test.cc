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

namespace {
auto EnqueueAndCheckSuccess(http2::SimpleQueue<int>& queue, int data,
                            int tokens) {
  return Map(queue.Enqueue(data, tokens),
             [](auto result) { EXPECT_EQ(result, absl::OkStatus()); });
}

void DequeueAndCheckPending(http2::SimpleQueue<int>& queue,
                            bool allow_oversized_dequeue, int max_tokens) {
  auto result = queue.Dequeue(max_tokens, allow_oversized_dequeue);
  EXPECT_FALSE(result.has_value());
}

void DequeueAndCheckSuccess(http2::SimpleQueue<int>& queue, int data,
                            bool allow_oversized_dequeue, int max_tokens) {
  auto result = queue.Dequeue(max_tokens, allow_oversized_dequeue);
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), data);
}

bool DequeueAndCheck(http2::SimpleQueue<int>& queue, int data,
                     bool allow_oversized_dequeue, int max_tokens) {
  auto result = queue.Dequeue(max_tokens, allow_oversized_dequeue);
  if (!result.has_value()) {
    return false;
  }

  EXPECT_EQ(result.value(), data);
  return true;
}
}  // namespace

namespace http2 {
namespace testing {

using ::testing::MockFunction;
using ::testing::StrictMock;
using util::testing::TransportTest;

class SimpleQueueTest : public TransportTest {
 protected:
  SimpleQueueTest() {
    InitParty();
  }

  Party* GetParty() { return party_.get(); }

  void InitParty() {
    auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
    general_party_arena
        ->SetContext<grpc_event_engine::experimental::EventEngine>(
            event_engine().get());
    party_ = Party::Make(std::move(general_party_arena));
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

  auto result =
      queue.Dequeue(/*max_tokens=*/10, /*allow_oversized_dequeue=*/false);
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
                                            /*max_tokens=*/10);
                     on_dequeue_done.Call(absl::OkStatus());
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
                                      /*max_tokens=*/10);
               DequeueAndCheckSuccess(queue, /*data=*/1,
                                      /*allow_oversized_dequeue=*/true,
                                      /*max_tokens=*/10);
               DequeueAndCheckSuccess(queue, /*data=*/2,
                                      /*allow_oversized_dequeue=*/false,
                                      /*max_tokens=*/100);
               // Empty Queue
               DequeueAndCheckPending(queue,
                                      /*allow_oversized_dequeue=*/false,
                                      /*max_tokens=*/10);
               DequeueAndCheckPending(queue,
                                      /*allow_oversized_dequeue=*/false,
                                      /*max_tokens=*/100);
               DequeueAndCheckPending(queue,
                                      /*allow_oversized_dequeue=*/true,
                                      /*max_tokens=*/10);
               return absl::OkStatus();
             }),
      [&on_dequeue_done](auto) {
        LOG(INFO) << "Reached end of DequeueTest";
        on_dequeue_done.Call(absl::OkStatus());
      });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(SimpleQueueTest, DequeueMaxTokensTest) {
  // Test to assert different combinations of max_tokens.
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

  party->Spawn("DequeueTest",
               TrySeq(enqueue_done.Wait(),
                      [&queue] {
                        // 2 entries
                        DequeueAndCheckPending(
                            queue,
                            /*allow_oversized_dequeue=*/false,
                            /*max_tokens=*/10);
                        DequeueAndCheckPending(
                            queue,
                            /*allow_oversized_dequeue=*/false,
                            /*max_tokens=*/99);
                        DequeueAndCheckSuccess(
                            queue, /*data=*/1,
                            /*allow_oversized_dequeue=*/false,
                            /*max_tokens=*/100);

                        // 1 entry
                        DequeueAndCheckPending(
                            queue,
                            /*allow_oversized_dequeue=*/false,
                            /*max_tokens=*/5);
                        DequeueAndCheckSuccess(
                            queue, /*data=*/2,
                            /*allow_oversized_dequeue=*/false,
                            /*max_tokens=*/500);

                        // Empty Queue
                        DequeueAndCheckPending(
                            queue,
                            /*allow_oversized_dequeue=*/false,
                            /*max_tokens=*/10);
                        DequeueAndCheckPending(
                            queue,
                            /*allow_oversized_dequeue=*/false,
                            /*max_tokens=*/100);
                        DequeueAndCheckPending(queue,
                                               /*allow_oversized_dequeue=*/true,
                                               /*max_tokens=*/10);
                        return absl::OkStatus();
                      }),
               [&on_dequeue_done](auto) {
                 LOG(INFO) << "Reached end of DequeueTest";
                 on_dequeue_done.Call(absl::OkStatus());
               });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

////////////////////////////////////////////////////////////////////////////////
// Enqueue and dequeue tests
TEST_F(SimpleQueueTest, BigMessageEnqueueDequeueTest) {
  // Tests that the first message is enqueued even if the tokens are more than
  // the max tokens.
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
                            /*max_tokens=*/10),
            [&dequeue_count, &on_dequeue_done,
             &execution_order]() -> LoopCtl<absl::Status> {
              if (--dequeue_count == 0) {
                execution_order.push_back('3');
                on_dequeue_done.Call(absl::OkStatus());
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
