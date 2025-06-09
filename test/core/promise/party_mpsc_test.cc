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

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>
#include <stdio.h>

#include <memory>
#include <thread>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/notification.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/shared_bit_gen.h"
#include "test/core/promise/poll_matcher.h"

namespace grpc_core {

constexpr int kMpscNumPayloads = 2000;

// Testing Promise Parties with MPSC Queues

struct TestParams {
  int max_queued_tokens;
  int num_unbuffered_sender_threads;
  int num_buffered_sender_threads;
  int token_release_batch_size;
};

class PartyMpscTest : public ::testing::TestWithParam<TestParams> {
 protected:
  RefCountedPtr<Party> MakeParty() {
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    return Party::Make(std::move(arena));
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
};

INSTANTIATE_TEST_SUITE_P(
    PartyMpscTest, PartyMpscTest,
    ::testing::Values(TestParams{1, 0, 31, 0}, TestParams{100, 0, 31, 50},
                      TestParams{31 * kMpscNumPayloads, 0, 31, 1000},
                      TestParams{1, 31, 0, 0}, TestParams{100, 31, 0, 50},
                      TestParams{31 * kMpscNumPayloads, 31, 0, 1000},
                      TestParams{1, 15, 15, 0}, TestParams{100, 15, 15, 50},
                      TestParams{31 * kMpscNumPayloads, 15, 15, 1000}),
    [](const auto& info) {
      return absl::StrCat("Q", info.param.max_queued_tokens, "_U",
                          info.param.num_unbuffered_sender_threads, "_B",
                          info.param.num_buffered_sender_threads, "_T",
                          info.param.token_release_batch_size);
    });

struct Payload {
  std::unique_ptr<std::pair<int, int>> x;
  bool operator==(const Payload& other) const {
    return (x == nullptr && other.x == nullptr) ||
           (x != nullptr && other.x != nullptr && *x == *other.x);
  }
  bool operator!=(const Payload& other) const { return !(*this == other); }
  explicit Payload(std::unique_ptr<std::pair<int, int>> x) : x(std::move(x)) {}
  Payload(int x, int y) : x(std::make_unique<std::pair<int, int>>(x, y)) {}
  Payload(const Payload& other)
      : x(other.x ? std::make_unique<std::pair<int, int>>(*other.x) : nullptr) {
  }

  friend std::ostream& operator<<(std::ostream& os, const Payload& payload) {
    if (payload.x == nullptr) return os << "Payload{nullptr}";
    return os << "Payload{" << payload.x->first << ", " << payload.x->second
              << "}";
  }
};

auto OnCompleteNoop() {
  return [](auto) {};
}

void WaitForNotificationWithTimeoutAndDumpAllPartyStateIfWeFail(
    Notification& n, absl::Span<const RefCountedPtr<Party>> parties) {
  if (!n.WaitForNotificationWithTimeout(absl::Seconds(30))) {
    struct PartyStateCollector {
      explicit PartyStateCollector(size_t parties)
          : done(parties), party_state(parties) {}
      Mutex mu;
      std::vector<Notification> done;
      std::vector<Json::Object> party_state ABSL_GUARDED_BY(mu);
    };
    Notification done;
    auto collector = std::make_shared<PartyStateCollector>(parties.size());
    for (size_t i = 0; i < parties.size(); i++) {
      parties[i]->ToJson([collector, i](Json::Object obj) {
        MutexLock lock(&collector->mu);
        collector->party_state[i] = std::move(obj);
        collector->done[i].Notify();
      });
    }
    for (size_t i = 0; i < parties.size(); i++) {
      CHECK(
          collector->done[i].WaitForNotificationWithTimeout(absl::Seconds(30)));
      MutexLock lock(&collector->mu);
      LOG(ERROR) << "Party " << i << " state: "
                 << JsonDump(
                        Json::FromObject(std::move(collector->party_state[i])));
    }
    LOG(FATAL) << "Test failed";
  }
}

TEST_P(PartyMpscTest, MpscManySendersManyPartyIntegrationStressTest) {
  // This is a Integration and Stress Test.
  // It tests if Promise Party works well with MPSC in an multi-threaded
  // environment. Using multiple Party objects, with each Party on a different
  // thread. We will Spawn promises on each Party that write to the MPSC queue,
  // and this will ensure that we have multiple threads concurrently trying to
  // Send on the same MPSC. We will have one receiver running on a separate
  // thread using a separate Party object.
  //
  // Asserts:
  // 1. If there is a bug in MPSC which causes any resource to be accessed
  // concurrently, we should see a TSAN failure with this test - because this
  // test is multi-threaded and using different Party objects.
  // 2. All payloads are sent and received.
  // Note : Both MPSC and Party can be used independent of each other.

  MpscReceiver<Payload> receiver(GetParam().max_queued_tokens);
  std::vector<std::thread> threads;
  std::vector<RefCountedPtr<Party>> parties;
  for (int i = 0; i < GetParam().num_buffered_sender_threads; i++) {
    auto party = MakeParty();
    threads.emplace_back([party, sender = receiver.MakeSender(), i]() mutable {
      party->Spawn("send-loop",
                   NTimes(kMpscNumPayloads,
                          [sender, i](int j) mutable {
                            return sender.Send(Payload(i, j), 1);
                          }),
                   OnCompleteNoop());
    });
    parties.emplace_back(std::move(party));
  }
  for (int i = 0; i < GetParam().num_unbuffered_sender_threads; i++) {
    threads.emplace_back([sender = receiver.MakeSender(), i]() mutable {
      for (size_t j = 0; j < kMpscNumPayloads; j++) {
        CHECK(sender.UnbufferedImmediateSend(Payload(-i - 1, j), 1).ok());
      }
    });
  }
  // Spawn the receiver.
  const int num_messages_sent = (GetParam().num_buffered_sender_threads +
                                 GetParam().num_unbuffered_sender_threads) *
                                kMpscNumPayloads;
  absl::flat_hash_map<int, std::vector<int>> receive_order;
  std::vector<MpscQueued<Payload>> received_payloads;
  Notification receive_complete;
  auto receive_party = MakeParty();
  std::vector<std::thread> cleanup_threads;
  threads.emplace_back([&receive_order, receive_party, &receiver,
                        num_messages_sent, &receive_complete,
                        &received_payloads, &cleanup_threads]() {
    receive_party->Spawn(
        "receive-loop",
        NTimes(num_messages_sent,
               [&receiver, &receive_order, &received_payloads,
                &cleanup_threads](int) mutable {
                 return Seq(
                     receiver.Next(),
                     [&receive_order, &received_payloads, &cleanup_threads](
                         ValueOrFailure<MpscQueued<Payload>> receive_result) {
                       CHECK_EQ(receive_result.ok(), true);
                       CHECK_EQ(receive_result->tokens(), 1);
                       receive_order[(*receive_result)->x->first].push_back(
                           (*receive_result)->x->second);
                       if (GetParam().token_release_batch_size > 0) {
                         received_payloads.push_back(
                             std::move(*receive_result));
                         if (received_payloads.size() ==
                             GetParam().token_release_batch_size) {
                           cleanup_threads.emplace_back(
                               [received_payloads =
                                    std::move(received_payloads)]() mutable {
                                 std::shuffle(received_payloads.begin(),
                                              received_payloads.end(),
                                              SharedBitGen());
                               });
                         }
                       }
                     });
               }),
        [&receive_complete](auto) { receive_complete.Notify(); });
  });
  parties.emplace_back(std::move(receive_party));

  WaitForNotificationWithTimeoutAndDumpAllPartyStateIfWeFail(receive_complete,
                                                             parties);

  for (auto& thread : threads) {
    thread.join();  // Wait for all threads to finish and join.
  }
  for (auto& thread : cleanup_threads) {
    thread.join();  // Wait for all cleanup threads to finish and join.
  }

  // Asserting that all payloads were sent and received.
  EXPECT_EQ(receive_order.size(), GetParam().num_buffered_sender_threads +
                                      GetParam().num_unbuffered_sender_threads);
  std::vector<int> expected_receive_order_for_each_thread;
  for (int i = 0; i < kMpscNumPayloads; i++) {
    expected_receive_order_for_each_thread.push_back(i);
  }
  for (int i = 0; i < GetParam().num_buffered_sender_threads; i++) {
    ASSERT_EQ(receive_order[i].size(), kMpscNumPayloads);
    EXPECT_EQ(receive_order[i], expected_receive_order_for_each_thread)
        << "Thread " << i << " [buffered] has unexpected receive order: "
        << absl::StrJoin(receive_order[i], ", ");
  }
  for (int i = 0; i < GetParam().num_unbuffered_sender_threads; i++) {
    ASSERT_EQ(receive_order[-i - 1].size(), kMpscNumPayloads);
    EXPECT_EQ(receive_order[-i - 1], expected_receive_order_for_each_thread)
        << "Thread " << i << " [unbuffered] has unexpected receive order: "
        << absl::StrJoin(receive_order[i], ", ");
  }
}

TEST_P(PartyMpscTest, ManySendersSeeCloseStressTest) {
  // This test starts a bunch of threads that send continuously, and one reader.
  // The reader closes after 10k messages are read, signalling the senders to
  // close.

  MpscReceiver<Payload> receiver(GetParam().max_queued_tokens);
  std::vector<RefCountedPtr<Party>> parties;
  std::vector<std::thread> threads;

  // Spawn on different Party objects using different threads.
  // Each Spawned promise will perform the MPSC Send operation.
  std::vector<Notification> send_complete(
      GetParam().num_buffered_sender_threads);
  for (int i = 0; i < GetParam().num_buffered_sender_threads; i++) {
    auto party = MakeParty();
    threads.emplace_back([party, sender = receiver.MakeSender(), i,
                          on_complete = &send_complete[i]]() mutable {
      party->Spawn("send-loop", WhilstSuccessful([sender, i, j = 0]() mutable {
                     return sender.Send(Payload(i, j++), 1);
                   }),
                   [on_complete](auto) { on_complete->Notify(); });
    });
    parties.emplace_back(std::move(party));
  }
  for (int i = 0; i < GetParam().num_unbuffered_sender_threads; i++) {
    threads.emplace_back([sender = receiver.MakeSender(), i]() mutable {
      int j = 0;
      while (sender.UnbufferedImmediateSend(Payload(-i - 1, j++), 1).ok()) {
      }
    });
  }

  // Spawn promises on the last Party object using the last thread.
  // These Spawned promises will read from the MPSC queue.
  absl::flat_hash_map<int, std::vector<int>> receive_order;
  Notification receive_complete;
  auto receive_party = MakeParty();
  threads.emplace_back([&receive_order, receive_party, &receiver,
                        &receive_complete]() {
    receive_party->Spawn(
        "receive-loop",
        Seq(NTimes(
                10000,
                [&receiver, &receive_order](int) mutable {
                  return Seq(
                      receiver.Next(),
                      [&receive_order](
                          ValueOrFailure<MpscQueued<Payload>> receive_result) {
                        CHECK_EQ(receive_result.ok(), true);
                        CHECK_EQ(receive_result->tokens(), 1);
                        receive_order[(*receive_result)->x->first].push_back(
                            (*receive_result)->x->second);
                      });
                }),
            [&receiver]() { receiver.MarkClosed(); }),
        [&receive_complete](auto) { receive_complete.Notify(); });
  });
  parties.emplace_back(std::move(receive_party));

  WaitForNotificationWithTimeoutAndDumpAllPartyStateIfWeFail(receive_complete,
                                                             parties);
  for (auto& n : send_complete) {
    WaitForNotificationWithTimeoutAndDumpAllPartyStateIfWeFail(n, parties);
  }

  for (auto& thread : threads) {
    thread.join();  // Wait for all threads to finish and join.
  }

  // Asserting that all payloads were sent and received.
  for (int i = 0; i < GetParam().num_buffered_sender_threads; i++) {
    const auto& received_order = receive_order[i];
    for (int j = 0; j < received_order.size(); j++) {
      EXPECT_EQ(received_order[j], j);
    }
  }
  for (int i = 0; i < GetParam().num_unbuffered_sender_threads; i++) {
    const auto& received_order = receive_order[-i - 1];
    for (int j = 0; j < received_order.size(); j++) {
      EXPECT_EQ(received_order[j], j);
    }
  }
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
