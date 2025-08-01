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

#include "src/core/ext/transport/chttp2/transport/lows.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/core/transport/util/transport_test.h"

namespace grpc_core {
namespace http2 {
namespace testing {

namespace {
using ::testing::MockFunction;
using ::testing::StrictMock;
using util::testing::TransportTest;

class LowsTest : public TransportTest {
 protected:
  LowsTest() { InitParty(); }

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

void EnqueueAndCheckSuccess(Lows& lows, const uint32_t stream_id,
                            const Lows::StreamPriority priority) {
  auto promise = lows.Enqueue(stream_id, priority);
  auto result = promise();
  EXPECT_TRUE(result.ready());
  EXPECT_EQ(result.value(), absl::OkStatus());
}
void SpawnEnqueueAndCheckSuccess(
    Party* party, Lows& lows, const uint32_t stream_id,
    const Lows::StreamPriority priority,
    absl::AnyInvocable<void(absl::Status)> on_complete) {
  LOG(INFO) << "Spawn EnqueueAndCheckSuccess for stream id " << stream_id
            << " with priority " << Lows::GetPriorityString(priority);
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
void DequeueAndCheckSuccess(Lows& lows, const uint32_t expected_stream_id) {
  auto promise = lows.Next(/*transport_tokens_available=*/true);
  auto result = promise();
  EXPECT_TRUE(result.ready());
  EXPECT_TRUE(result.value().ok());
  EXPECT_EQ(*result.value(), expected_stream_id);
}

}  // namespace

/////////////////////////////////////////////////////////////////////////////////
// Enqueue tests
TEST_F(LowsTest, EnqueueTest) {
  Lows lows(/*max_queue_size=*/1);
  SpawnEnqueueAndCheckSuccess(
      GetParty(), lows, /*stream_id=*/1, Lows::StreamPriority::kDefault,
      [](absl::Status status) { EXPECT_TRUE(status.ok()); });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(LowsTest, MultipleEnqueueTest) {
  Lows lows(/*max_queue_size=*/3);
  std::string execution_order;
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/1,
                              Lows::StreamPriority::kDefault,
                              [&execution_order](absl::Status status) {
                                execution_order.append("1");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/2,
                              Lows::StreamPriority::kStreamClosed,
                              [&execution_order](absl::Status status) {
                                execution_order.append("2");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/3,
                              Lows::StreamPriority::kTransportJail,
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
TEST_F(LowsTest, EnqueueDequeueTest) {
  Lows lows(/*max_queue_size=*/1);
  EnqueueAndCheckSuccess(lows, /*stream_id=*/1, Lows::StreamPriority::kDefault);
  DequeueAndCheckSuccess(lows, /*expected_stream_id=*/1);

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(LowsTest, MultipleEnqueueDequeueTest) {
  Lows lows(/*max_queue_size=*/1);
  uint dequeue_count = 0;
  std::vector<uint32_t> expected_stream_ids = {1, 2, 3};
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());

  SpawnEnqueueAndCheckSuccess(
      GetParty(), lows, /*stream_id=*/1, Lows::StreamPriority::kDefault,
      [](absl::Status status) { EXPECT_TRUE(status.ok()); });
  SpawnEnqueueAndCheckSuccess(
      GetParty(), lows, /*stream_id=*/2, Lows::StreamPriority::kDefault,
      [](absl::Status status) { EXPECT_TRUE(status.ok()); });
  SpawnEnqueueAndCheckSuccess(
      GetParty(), lows, /*stream_id=*/3, Lows::StreamPriority::kDefault,
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

TEST_F(LowsTest, EnqueueDequeueSamePriorityTest) {
  Lows lows(/*max_queue_size=*/3);
  std::string execution_order;
  uint dequeue_count = 0;
  std::vector<uint32_t> expected_stream_ids = {1, 2, 3};
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());

  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/1,
                              Lows::StreamPriority::kDefault,
                              [&execution_order](absl::Status status) {
                                execution_order.append("1");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/2,
                              Lows::StreamPriority::kDefault,
                              [&execution_order](absl::Status status) {
                                execution_order.append("2");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/3,
                              Lows::StreamPriority::kDefault,
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

TEST_F(LowsTest, EnqueueDequeueDifferentPriorityTest) {
  Lows lows(/*max_queue_size=*/3);
  std::string execution_order;
  uint dequeue_count = 0;
  std::vector<uint32_t> expected_stream_ids = {2, 3, 1};
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());

  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/1,
                              Lows::StreamPriority::kDefault,
                              [&execution_order](absl::Status status) {
                                execution_order.append("1");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/2,
                              Lows::StreamPriority::kStreamClosed,
                              [&execution_order](absl::Status status) {
                                execution_order.append("2");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/3,
                              Lows::StreamPriority::kTransportJail,
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

TEST_F(LowsTest, DequeueWithTransportTokensUnavailableTest) {
  Lows lows(/*max_queue_size=*/3);
  std::string execution_order;
  uint dequeue_count = 0;
  std::vector<uint32_t> expected_stream_ids = {2, 1, 3};
  std::vector<bool> transport_tokens_available = {true, false, true};
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());

  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/1,
                              Lows::StreamPriority::kDefault,
                              [&execution_order](absl::Status status) {
                                execution_order.append("1");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/2,
                              Lows::StreamPriority::kStreamClosed,
                              [&execution_order](absl::Status status) {
                                execution_order.append("2");
                                EXPECT_TRUE(status.ok());
                              });
  SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/3,
                              Lows::StreamPriority::kTransportJail,
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

// TEST_F(LowsTest, EnqueueDequeueFlowTest) {
//   Lows lows(/*max_queue_size=*/2);
//   std::string execution_order;
//   std::vector<uint32_t> expected_stream_ids = {2, 3, 1, 4};
//   std::vector<bool> transport_tokens_available = {true, false, true};
//   StrictMock<MockFunction<void()>> on_done;
//   EXPECT_CALL(on_done, Call()).Times(expected_stream_ids.size());
//   Latch<void> latch1;
//   Latch<void> latch2;
//   Latch<void> latch3;

//   SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/1,
//                               Lows::StreamPriority::kDefault,
//                               [&execution_order](absl::Status status) {
//                                 execution_order.append("1");
//                                 EXPECT_TRUE(status.ok());
//                               });
//   SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/2,
//                               Lows::StreamPriority::kStreamClosed,
//                               [&execution_order, &latch1](absl::Status
//                               status) {
//                                 execution_order.append("2");
//                                 EXPECT_TRUE(status.ok());
//                                 latch1.Set();
//                                 LOG(INFO) << "latch1 set";
//                               });
//   SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/3,
//                               Lows::StreamPriority::kTransportJail,
//                               [&execution_order, &latch2](absl::Status
//                               status) {
//                                 execution_order.append("3");
//                                 EXPECT_TRUE(status.ok());
//                                 latch2.Set();
//                                 LOG(INFO) << "latch2 set";
//                               });
//   SpawnEnqueueAndCheckSuccess(GetParty(), lows, /*stream_id=*/4,
//                               Lows::StreamPriority::kDefault,
//                               [&execution_order, &latch3](absl::Status
//                               status) {
//                                 execution_order.append("4");
//                                 EXPECT_TRUE(status.ok());
//                                 latch3.Set();
//                                 LOG(INFO) << "latch3 set";
//                               });

//   GetParty()->Spawn(
//       "Dequeue1",
//       [&lows, &latch1, &on_done]() {
//         return TrySeq(
//             latch1.Wait(),
//             [&lows]() {
//               LOG(INFO) << "Dequeue1";
//               return lows.Next(/*transport_tokens_available=*/true);
//             },
//             [&on_done](uint32_t stream_id) {
//               LOG(INFO) << "Dequeue1 stream id " << stream_id;
//               EXPECT_EQ(stream_id, 2);
//               on_done.Call();
//             });
//       },
//       [](auto) {});
//   GetParty()->Spawn(
//       "Dequeue2",
//       [&lows, &latch2, &on_done]() {
//         return TrySeq(
//             latch2.Wait(),
//             [&lows]() {
//               LOG(INFO) << "Dequeue2";
//               return lows.Next(/*transport_tokens_available=*/true);
//             },
//             [&on_done](uint32_t stream_id) {
//               LOG(INFO) << "Dequeue2 stream id " << stream_id;
//               EXPECT_EQ(stream_id, 3);
//               on_done.Call();
//             });
//       },
//       [](auto) {});
//   GetParty()->Spawn(
//       "Dequeue3",
//       [&lows, &latch2, &on_done]() {
//         return TrySeq(
//             latch2.Wait(),
//             [&lows]() {
//               LOG(INFO) << "Dequeue3";
//               return lows.Next(/*transport_tokens_available=*/true);
//             },
//             [&on_done](uint32_t stream_id) {
//               LOG(INFO) << "Dequeue3 stream id " << stream_id;
//               EXPECT_EQ(stream_id, 1);
//               on_done.Call();
//             });
//       },
//       [](auto) {});
//   GetParty()->Spawn(
//       "Dequeue4",
//       [&lows, &latch3, &on_done]() {
//         return TrySeq(
//             latch3.Wait(),
//             [&lows]() {
//               LOG(INFO) << "Dequeue4";
//               return lows.Next(/*transport_tokens_available=*/true);
//             },
//             [&on_done](uint32_t stream_id) {
//               LOG(INFO) << "Dequeue4 stream id " << stream_id;
//               EXPECT_EQ(stream_id, 4);
//               on_done.Call();
//             });
//       },
//       [](auto) {});

//   event_engine()->TickUntilIdle();
//   event_engine()->UnsetGlobalHooks();
// }

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
