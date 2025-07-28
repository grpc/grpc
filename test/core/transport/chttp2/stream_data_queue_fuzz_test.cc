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

}  // namespace grpc_core
