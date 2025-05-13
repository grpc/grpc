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

#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/notification.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/promise/poll_matcher.h"
#include "gtest/gtest.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace grpc_core {

constexpr int kMpscNumPayloads = 2000;
constexpr int kMpscNumThreads = 32;

// Testing Promise Parties with MPSC Queues

class PartyMpscTest : public ::testing::TestWithParam<int> {
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
    ::testing::Values(1, 100, (kMpscNumThreads - 1) * kMpscNumPayloads),
    [](const auto& info) { return absl::StrCat(info.param); });

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
  //
  // Number of Receivers = 1  // Will be 1 always for MPSC
  // Number of Senders   = kMpscNumThreads - 1
  // Number of Payloads  = (kMpscNumThreads - 1) * kMpscNumPayloads
  // Number of Parties   = kMpscNumThreads
  // Number of Threads   = kMpscNumThreads

  MpscReceiver<Payload> receiver(GetParam());
  std::vector<MpscSender<Payload>> senders;
  std::vector<RefCountedPtr<Party>> parties;
  for (int i = 0; i < kMpscNumThreads; i++) {
    if (i < kMpscNumThreads - 1) {
      senders.emplace_back(receiver.MakeSender());
    }
    parties.emplace_back(MakeParty());
  }
  std::vector<std::thread> threads;
  threads.reserve(kMpscNumThreads);

  // Spawn on different Party objects using different threads.
  // Each Spawned promise will perform the MPSC Send operation.
  for (int i = 0; i < kMpscNumThreads - 1; i++) {
    MpscSender<Payload>& sender = senders[i];
    RefCountedPtr<Party>& party = parties[i];
    threads.emplace_back([&party, &sender, i]() {
      party->Spawn("send-loop",
                   NTimes(kMpscNumPayloads,
                          [sender, i](int j) mutable {
                            return sender.Send(Payload(i, j), 1);
                          }),
                   OnCompleteNoop());
    });
  }

  // Spawn promises on the last Party object using the last thread.
  // These Spawned promises will read from the MPSC queue.
  int num_messages_sent = (kMpscNumThreads - 1) * kMpscNumPayloads;
  absl::flat_hash_map<int, std::vector<int>> receive_order;
  RefCountedPtr<Party>& receive_party = parties[kMpscNumThreads - 1];
  Notification receive_complete;
  threads.emplace_back([&receive_order, &receive_party, &receiver,
                        &num_messages_sent, &receive_complete]() {
    receive_party->Spawn(
        "receive-loop",
        NTimes(num_messages_sent,
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
        [&receive_complete](auto) { receive_complete.Notify(); });
  });

  WaitForNotificationWithTimeoutAndDumpAllPartyStateIfWeFail(receive_complete,
                                                             parties);

  for (auto& thread : threads) {
    thread.join();  // Wait for all threads to finish and join.
  }

  // Asserting that all payloads were sent and received.
  EXPECT_EQ(receive_order.size(), kMpscNumThreads - 1);
  std::vector<int> expected_receive_order_for_each_thread;
  for (int i = 0; i < kMpscNumPayloads; i++) {
    expected_receive_order_for_each_thread.push_back(i);
  }
  for (int i = 0; i < kMpscNumThreads - 1; i++) {
    ASSERT_EQ(receive_order[i].size(), kMpscNumPayloads);
    EXPECT_EQ(receive_order[i], expected_receive_order_for_each_thread)
        << "Thread " << i << " has unexpected receive order: "
        << absl::StrJoin(receive_order[i], ", ");
  }
}

TEST_P(PartyMpscTest, ManySendersSeeCloseStressTest) {
  // This test starts a bunch of threads that send continuously, and one reader.
  // The reader closes after 10k messages are read, signalling the senders to
  // close.

  MpscReceiver<Payload> receiver(GetParam());
  std::vector<MpscSender<Payload>> senders;
  std::vector<RefCountedPtr<Party>> parties;
  for (int i = 0; i < kMpscNumThreads; i++) {
    if (i < kMpscNumThreads - 1) {
      senders.emplace_back(receiver.MakeSender());
    }
    parties.emplace_back(MakeParty());
  }
  std::vector<std::thread> threads;
  threads.reserve(kMpscNumThreads);

  // Spawn on different Party objects using different threads.
  // Each Spawned promise will perform the MPSC Send operation.
  Notification send_complete[kMpscNumThreads - 1];
  for (int i = 0; i < kMpscNumThreads - 1; i++) {
    MpscSender<Payload>& sender = senders[i];
    RefCountedPtr<Party>& party = parties[i];
    threads.emplace_back([&party, &sender, i,
                          on_complete = &send_complete[i]]() {
      party->Spawn("send-loop", WhilstSuccessful([sender, i, j = 0]() mutable {
                     return sender.Send(Payload(i, j++), 1);
                   }),
                   [on_complete](auto) { on_complete->Notify(); });
    });
  }

  // Spawn promises on the last Party object using the last thread.
  // These Spawned promises will read from the MPSC queue.
  absl::flat_hash_map<int, std::vector<int>> receive_order;
  RefCountedPtr<Party>& receive_party = parties[kMpscNumThreads - 1];
  Notification receive_complete;
  threads.emplace_back([&receive_order, &receive_party, &receiver,
                        &receive_complete]() {
    receive_party->Spawn(
        "receive-loop",
        Seq(
            NTimes(
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
            [&receiver]() { receiver.MarkClosed(); },
            NTimes(100,
                   [&receiver](int) {
                     auto r = receiver.Next()();
                     CHECK(!r.pending());
                     CHECK(!r.value().ok());
                   })),
        [&receive_complete](auto) { receive_complete.Notify(); });
  });

  WaitForNotificationWithTimeoutAndDumpAllPartyStateIfWeFail(receive_complete,
                                                             parties);
  for (int i = 0; i < kMpscNumThreads - 1; i++) {
    WaitForNotificationWithTimeoutAndDumpAllPartyStateIfWeFail(send_complete[i],
                                                               parties);
  }

  for (auto& thread : threads) {
    thread.join();  // Wait for all threads to finish and join.
  }

  // Asserting that all payloads were sent and received.
  for (int i = 0; i < kMpscNumThreads - 1; i++) {
    const auto& received_order = receive_order[i];
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
