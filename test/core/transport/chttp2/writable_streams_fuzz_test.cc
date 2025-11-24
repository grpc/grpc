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
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/sleep.h"
#include "test/core/call/yodel/yodel_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
using ::testing::MockFunction;
using ::testing::StrictMock;

class WritableStreamsFuzzTest : public YodelTest {
 protected:
  struct TestStream : public RefCounted<TestStream> {
    explicit TestStream(uint32_t stream_id) : stream_id(stream_id) {}
    uint32_t stream_id;
    uint32_t GetStreamId() const { return stream_id; }
  };
  using YodelTest::YodelTest;
  using Stream = TestStream;
  using WritableStreams = http2::WritableStreams<RefCountedPtr<Stream>>;

  Party* GetParty() { return party_.get(); }
  Party* GetParty2() { return party2_.get(); }

  void InitParty() {
    auto party_arena = SimpleArenaAllocator(0)->MakeArena();
    party_arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine().get());
    party_ = Party::Make(std::move(party_arena));
  }
  void InitParty2() {
    auto party_arena = SimpleArenaAllocator(0)->MakeArena();
    party_arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine().get());
    party2_ = Party::Make(std::move(party_arena));
  }

  void EnqueueAndCheckSuccess(WritableStreams& writable_streams,
                              RefCountedPtr<Stream> stream,
                              const http2::WritableStreamPriority priority) {
    absl::Status status = writable_streams.Enqueue(stream, priority);
    EXPECT_TRUE(status.ok()) << status;
    LOG(INFO) << "EnqueueAndCheckSuccess status: " << status
              << " for stream id: " << stream->GetStreamId();
  }

 private:
  void InitCoreConfiguration() override {}
  void InitTest() override {
    InitParty();
    InitParty2();
  }
  void Shutdown() override { party_.reset(); }

  RefCountedPtr<Party> party_;
  RefCountedPtr<Party> party2_;
};

YODEL_TEST(WritableStreamsFuzzTest, NoOp) {}

YODEL_TEST(WritableStreamsFuzzTest, EnqueueDequeueTest) {
  // This test fuzzes the WritableStreams class by simulating a producer and a
  // consumer operating concurrently.
  // A "producer" promise loop enqueues a new stream every 100ms.
  // A "consumer" promise loop waits for streams to become available and
  // dequeues them in batches.
  // The test verifies that all enqueued streams are successfully dequeued.

  constexpr uint32_t num_streams = 1000;
  constexpr uint32_t max_dequeue_batch = 20;
  WritableStreams writable_streams(std::numeric_limits<uint32_t>::max());
  StrictMock<MockFunction<void()>> on_enqueue_done;
  StrictMock<MockFunction<void()>> on_dequeue_done;
  EXPECT_CALL(on_enqueue_done, Call()).Times(num_streams);
  EXPECT_CALL(on_dequeue_done, Call()).Times(num_streams);

  GetParty2()->Spawn(
      "Enqueue", Loop([&, enqueue_count = 0u, idx = 1u]() mutable {
        return If(
            enqueue_count < num_streams,
            [&]() {
              return Map(Sleep(Duration::Milliseconds(100)),
                         [&](auto) -> LoopCtl<absl::Status> {
                           EnqueueAndCheckSuccess(
                               writable_streams,
                               /*stream=*/MakeRefCounted<Stream>(idx),
                               /*priority=*/
                               http2::WritableStreamPriority::kDefault);
                           on_enqueue_done.Call();
                           idx += 2;
                           enqueue_count++;
                           return Continue();
                         });
            },
            []() -> LoopCtl<absl::Status> { return absl::OkStatus(); });
      }),
      [](auto) {});

  GetParty()->Spawn(
      "Dequeue",
      Loop([&writable_streams, &on_dequeue_done, idx = 1u, dequeue_count = 0u,
            max_dequeue_batch]() mutable {
        return TrySeq(
            writable_streams.WaitForReady(/*transport_tokens_available=*/true),
            [&writable_streams, &on_dequeue_done, &idx, &dequeue_count,
             max_dequeue_batch]() {
              uint32_t count = 0;
              while (count <
                     std::min(max_dequeue_batch, num_streams - dequeue_count)) {
                std::optional<RefCountedPtr<Stream>> stream =
                    writable_streams.ImmediateNext(
                        /*transport_tokens_available=*/true);
                LOG(INFO) << "Dequeued stream id: "
                          << (stream.has_value()
                                  ? (*stream)->GetStreamId()
                                  : std::numeric_limits<uint32_t>::max());
                if (!stream.has_value()) {
                  break;
                }
                EXPECT_EQ(stream.value()->GetStreamId(), idx);
                count++;
                dequeue_count++;
                idx += 2;
                on_dequeue_done.Call();
              }
              EXPECT_GT(count, 0);

              return Map(Sleep(Duration::Seconds(1)),
                         [&dequeue_count](auto) -> LoopCtl<absl::Status> {
                           if (dequeue_count < num_streams) {
                             return Continue();
                           }
                           return absl::OkStatus();
                         });
            });
      }),
      [](auto) {});

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
  EXPECT_FALSE(writable_streams.TestOnlyPriorityQueueHasWritableStreams(
      /*transport_tokens_available=*/true));
}

}  // namespace grpc_core
