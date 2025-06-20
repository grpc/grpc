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
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

using ::testing::MockFunction;
using ::testing::StrictMock;

class SimpleQueueTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  Party* GetParty() { return party_.get(); }

  void InitParty() {
    auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
    general_party_arena
        ->SetContext<grpc_event_engine::experimental::EventEngine>(
            event_engine().get());
    party_ = Party::Make(std::move(general_party_arena));
  }

  Party* GetParty2() { return party2_.get(); }

  void InitParty2() {
    auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
    general_party_arena
        ->SetContext<grpc_event_engine::experimental::EventEngine>(
            event_engine().get());
    party2_ = Party::Make(std::move(general_party_arena));
  }

  auto EnqueueAndCheckSuccess(http2::SimpleQueue<int>& queue, int data,
                              int tokens) {
    return Map(queue.Enqueue(data, tokens),
               [](auto result) { EXPECT_EQ(result, Success{}); });
  }

  void DequeueAndCheckPending(http2::SimpleQueue<int>& queue,
                              bool allow_partial_dequeue, int max_tokens) {
    auto result = queue.Dequeue(max_tokens, allow_partial_dequeue);
    EXPECT_FALSE(result.has_value());
  }

  void DequeueAndCheckSuccess(http2::SimpleQueue<int>& queue, int data,
                              bool allow_partial_dequeue, int max_tokens) {
    auto result = queue.Dequeue(max_tokens, allow_partial_dequeue);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), data);
  }

  bool DequeueAndCheck(http2::SimpleQueue<int>& queue, int data,
                       bool allow_partial_dequeue, int max_tokens) {
    auto result = queue.Dequeue(max_tokens, allow_partial_dequeue);
    if (!result.has_value()) {
      return false;
    }

    EXPECT_EQ(result.value(), data);
    return true;
  }

 private:
  void InitCoreConfiguration() override {}
  void InitTest() override {
    InitParty();
    InitParty2();
  }
  void Shutdown() override {
    party_.reset();
    party2_.reset();
  }

  RefCountedPtr<Party> party_;
  RefCountedPtr<Party> party2_;
};

YODEL_TEST(SimpleQueueTest, NoOp) {}

////////////////////////////////////////////////////////////////////////////////
// Enqueue tests

YODEL_TEST(SimpleQueueTest, EnqueueTest) {
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

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(SimpleQueueTest, MultipleEnqueueTest) {
  // Test multiple enqueues. All the enqueues for this test are immediate.
  http2::SimpleQueue<int> queue(/*max_tokens=*/100);
  auto* party = GetParty();
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  int count = 10;

  party->Spawn("EnqueueTest", Loop([&count, this, &queue, &on_done]() {
                 return If(
                     count > 0,
                     [this, &queue, &count] {
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

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

////////////////////////////////////////////////////////////////////////////////
// Dequeue tests
YODEL_TEST(SimpleQueueTest, DequeueEmptyQueueTest) {
  // Test to dequeue from an empty queue.
  http2::SimpleQueue<int> queue(/*max_tokens=*/100);

  auto result =
      queue.Dequeue(/*max_tokens=*/10, /*allow_partial_dequeue=*/false);
  EXPECT_FALSE(result.has_value());
}

YODEL_TEST(SimpleQueueTest, DequeueTest) {
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
                   [&queue, this, &on_dequeue_done](auto) {
                     DequeueAndCheckSuccess(queue, /*data=*/1,
                                            /*allow_partial_dequeue=*/false,
                                            /*max_tokens=*/10);
                     on_dequeue_done.Call(absl::OkStatus());
                     return absl::OkStatus();
                   }),
               [](auto) { LOG(INFO) << "Reached end of DequeueTest"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(SimpleQueueTest, DequeuePartialDequeueTest) {
  // Test to assert on different combinations of allow_partial_dequeue.
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

  party->Spawn("DequeueTest",
               TrySeq(enqueue_done.Wait(),
                      [&queue, this] {
                        DequeueAndCheckPending(queue,
                                               /*allow_partial_dequeue=*/false,
                                               /*max_tokens=*/10);
                        DequeueAndCheckSuccess(queue, /*data=*/1,
                                               /*allow_partial_dequeue=*/true,
                                               /*max_tokens=*/10);
                        DequeueAndCheckSuccess(queue, /*data=*/2,
                                               /*allow_partial_dequeue=*/false,
                                               /*max_tokens=*/100);
                        // Empty Queue
                        DequeueAndCheckPending(queue,
                                               /*allow_partial_dequeue=*/false,
                                               /*max_tokens=*/10);
                        DequeueAndCheckPending(queue,
                                               /*allow_partial_dequeue=*/false,
                                               /*max_tokens=*/100);
                        DequeueAndCheckPending(queue,
                                               /*allow_partial_dequeue=*/true,
                                               /*max_tokens=*/10);
                        return absl::OkStatus();
                      }),
               [&on_dequeue_done](auto) {
                 LOG(INFO) << "Reached end of DequeueTest";
                 on_dequeue_done.Call(absl::OkStatus());
               });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(SimpleQueueTest, DequeueMaxTokensTest) {
  // Test to assert different combinations of max_tokens.
  http2::SimpleQueue<int> queue(/*max_tokens=*/200);
  Latch<void> enqueue_done;
  auto* party = GetParty();
  StrictMock<MockFunction<void(absl::Status)>> on_enqueue_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_enqueue_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));

  party->Spawn("EnqueueTest",
               TrySeq(EnqueueAndCheckSuccess(queue, /*data=*/1, /*tokens=*/100),
                      EnqueueAndCheckSuccess(queue, /*data=*/2, /*tokens=*/99)),
               [&on_enqueue_done, &enqueue_done](auto) {
                 LOG(INFO) << "Reached end of EnqueueTest";
                 on_enqueue_done.Call(absl::OkStatus());
                 enqueue_done.Set();
               });

  party->Spawn("DequeueTest",
               TrySeq(enqueue_done.Wait(),
                      [&queue, this] {
                        // 2 entries
                        DequeueAndCheckPending(queue,
                                               /*allow_partial_dequeue=*/false,
                                               /*max_tokens=*/10);
                        DequeueAndCheckPending(queue,
                                               /*allow_partial_dequeue=*/false,
                                               /*max_tokens=*/99);
                        DequeueAndCheckSuccess(queue, /*data=*/1,
                                               /*allow_partial_dequeue=*/false,
                                               /*max_tokens=*/100);

                        // 1 entry
                        DequeueAndCheckPending(queue,
                                               /*allow_partial_dequeue=*/false,
                                               /*max_tokens=*/5);
                        DequeueAndCheckSuccess(queue, /*data=*/2,
                                               /*allow_partial_dequeue=*/false,
                                               /*max_tokens=*/500);

                        // Empty Queue
                        DequeueAndCheckPending(queue,
                                               /*allow_partial_dequeue=*/false,
                                               /*max_tokens=*/10);
                        DequeueAndCheckPending(queue,
                                               /*allow_partial_dequeue=*/false,
                                               /*max_tokens=*/100);
                        DequeueAndCheckPending(queue,
                                               /*allow_partial_dequeue=*/true,
                                               /*max_tokens=*/10);
                        return absl::OkStatus();
                      }),
               [&on_dequeue_done](auto) {
                 LOG(INFO) << "Reached end of DequeueTest";
                 on_dequeue_done.Call(absl::OkStatus());
               });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

////////////////////////////////////////////////////////////////////////////////
// Enqueue and Dequeue tests

YODEL_TEST(SimpleQueueTest, EnqueueAndDequeueTest) {
  // Test to enqueue and dequeue in a loop. This test enqueues 100 entries and
  // dequeues 100 entries. This test asserts the following:
  // 1. All enqueues and dequeues are successful.
  // 2. The dequeue data is the same as the enqueue data.
  http2::SimpleQueue<int> queue(/*max_tokens=*/100);
  auto* party = GetParty();
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));
  constexpr int count = 100;
  constexpr int dequeue_count = 100;
  int current_enqueue_count = 0;
  int current_dequeue_count = 0;

  party->Spawn("EnqueueTest",
               Loop([this, &queue, &on_done, &current_enqueue_count]() mutable {
                 return If(
                     current_enqueue_count < count,
                     [this, &queue, &current_enqueue_count] {
                       return Map(EnqueueAndCheckSuccess(
                                      queue, /*data=*/current_enqueue_count++,
                                      /*tokens=*/10),
                                  [](auto) -> LoopCtl<StatusFlag> {
                                    return Continue();
                                  });
                     },
                     [&on_done]() -> LoopCtl<StatusFlag> {
                       on_done.Call(absl::OkStatus());
                       return Success{};
                     });
               }),
               [](auto) { LOG(INFO) << "Reached end of EnqueueTest"; });

  party->Spawn("DequeueTest",
               Loop([&on_dequeue_done, this, &queue, &current_dequeue_count] {
                 return If(
                     DequeueAndCheck(queue, /*data=*/current_dequeue_count,
                                     /*allow_partial_dequeue=*/false,
                                     /*max_tokens=*/10),
                     [&current_dequeue_count,
                      &on_dequeue_done]() -> LoopCtl<absl::Status> {
                       if (++current_dequeue_count == dequeue_count) {
                         on_dequeue_done.Call(absl::OkStatus());
                         return absl::OkStatus();
                       } else {
                         return Continue();
                       }
                     },
                     [] {
                       return Map(Sleep(Duration::Seconds(1)),
                                  [](auto) -> LoopCtl<absl::Status> {
                                    return Continue();
                                  });
                     });
               }),
               [](auto) { LOG(INFO) << "Reached end of DequeueTest"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(SimpleQueueTest, EnqueueAndDequeueMultiPartyTest) {
  // Similar to EnqueueAndDequeueTest, but with two parties.
  http2::SimpleQueue<int> queue(/*max_tokens=*/100);
  auto* party = GetParty();
  auto* party2 = GetParty2();
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));
  constexpr int count = 100;
  constexpr int dequeue_count = 100;
  int current_enqueue_count = 0;
  int current_dequeue_count = 0;

  party->Spawn("EnqueueTest",
               Loop([this, &queue, &on_done, &current_enqueue_count]() mutable {
                 return If(
                     current_enqueue_count < count,
                     [this, &queue, &current_enqueue_count] {
                       return Map(EnqueueAndCheckSuccess(
                                      queue, /*data=*/current_enqueue_count++,
                                      /*tokens=*/10),
                                  [](auto) -> LoopCtl<StatusFlag> {
                                    return Continue();
                                  });
                     },
                     [&on_done]() -> LoopCtl<StatusFlag> {
                       on_done.Call(absl::OkStatus());
                       return Success{};
                     });
               }),
               [](auto) { LOG(INFO) << "Reached end of EnqueueTest"; });

  party2->Spawn("DequeueTest",
                Loop([&on_dequeue_done, this, &queue, &current_dequeue_count] {
                  return If(
                      DequeueAndCheck(queue, /*data=*/current_dequeue_count,
                                      /*allow_partial_dequeue=*/false,
                                      /*max_tokens=*/10),
                      [&current_dequeue_count,
                       &on_dequeue_done]() -> LoopCtl<absl::Status> {
                        if (++current_dequeue_count == dequeue_count) {
                          on_dequeue_done.Call(absl::OkStatus());
                          return absl::OkStatus();
                        } else {
                          return Continue();
                        }
                      },
                      [] {
                        return Map(Sleep(Duration::Seconds(1)),
                                   [](auto) -> LoopCtl<absl::Status> {
                                     return Continue();
                                   });
                      });
                }),
                [](auto) { LOG(INFO) << "Reached end of DequeueTest"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(SimpleQueueTest, BigMessageEnqueueTest) {
  // Tests that the first message is enqueued even if the tokens are more than
  // the max tokens.
  http2::SimpleQueue<int> queue(/*max_tokens=*/100);
  auto* party = GetParty();
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  StrictMock<MockFunction<void(absl::Status)>> on_dequeue_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_dequeue_done, Call(absl::OkStatus()));
  int dequeue_count = 1;
  std::string execution_order;

  party->Spawn(
      "EnqueueTest",
      TrySeq(EnqueueAndCheckSuccess(queue, /*data=*/1, /*tokens=*/1000),
             [this, &queue, &execution_order] {
               execution_order.push_back('1');
               return EnqueueAndCheckSuccess(queue, /*data=*/2, /*tokens=*/10);
             }),
      [&on_done, &execution_order](auto) {
        LOG(INFO) << "Reached end of EnqueueTest";
        on_done.Call(absl::OkStatus());
        execution_order.push_back('3');
      });

  party->Spawn(
      "DequeueTest",
      Loop([&dequeue_count, &on_dequeue_done, &queue, this, &execution_order] {
        return If(
            DequeueAndCheck(queue, /*data=*/1, /*allow_partial_dequeue=*/true,
                            /*max_tokens=*/10),
            [&dequeue_count, &on_dequeue_done,
             &execution_order]() -> LoopCtl<absl::Status> {
              if (--dequeue_count == 0) {
                execution_order.push_back('2');
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

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
  EXPECT_STREQ(execution_order.c_str(), "123");
}

}  // namespace grpc_core
