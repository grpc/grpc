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

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/notification.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"

namespace grpc_core {

// Testing Promise Parties with MPSC Queues

class PartyMpscTest : public ::testing::Test {
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

struct Payload {
  std::unique_ptr<int> x;
  bool operator==(const Payload& other) const {
    return (x == nullptr && other.x == nullptr) ||
           (x != nullptr && other.x != nullptr && *x == *other.x);
  }
  bool operator!=(const Payload& other) const { return !(*this == other); }
  explicit Payload(std::unique_ptr<int> x) : x(std::move(x)) {}
  Payload(const Payload& other)
      : x(other.x ? std::make_unique<int>(*other.x) : nullptr) {}

  friend std::ostream& operator<<(std::ostream& os, const Payload& payload) {
    if (payload.x == nullptr) return os << "Payload{nullptr}";
    return os << "Payload{" << *payload.x << "}";
  }
};

Payload MakePayload(int value) { return Payload{std::make_unique<int>(value)}; }

auto OnCompleteNoop() {
  return [](Empty) {};
}

constexpr int kMpscNumPayloads = 20;
constexpr int kMpscNumThreads = 8;

TEST_F(PartyMpscTest, MpscManySendersManyPartyIntegrationStressTest) {
  // This is a Integration and Stress Test.
  // It tests if Promise Party works well with MPSC in an multi-threaded
  // environment. Using multiple Party objects, with each Party on a different
  // thread. We will Spawn promises on each Party that write to the MPSC queue,
  // and this will ensure that we have multiple threads concurrently trying to
  // Send on the same MPSC. We will have one receiver running on a spearate
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

  std::vector<std::string> execution_order(kMpscNumThreads);
  MpscReceiver<Payload> receiver((kMpscNumThreads - 1) * kMpscNumPayloads);
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
    std::string& order = execution_order[i];
    RefCountedPtr<Party>& party = parties[i];
    threads.emplace_back([&order, &party, &sender]() {
      for (int j = 0; j < kMpscNumPayloads; j++) {
        party->Spawn(
            "send",
            [&sender, &order, value = j]() {
              auto send_promise = sender.Send(MakePayload(value));
              Poll<StatusFlag> send_result = send_promise();
              absl::StrAppend(&order, "S", value);
            },
            OnCompleteNoop());
      }
    });
  }

  // Spawn promises on the last Party object using the last thread.
  // These Spawned promises will read from the MPSC queue.
  int num_messages_sent = (kMpscNumThreads - 1) * kMpscNumPayloads;
  std::string& receive_order = execution_order[kMpscNumThreads - 1];
  RefCountedPtr<Party>& receive_party = parties[kMpscNumThreads - 1];
  threads.emplace_back([&receive_order, &receive_party, &receiver,
                        &num_messages_sent]() {
    for (int j = 0; j < num_messages_sent; j++) {
      receive_party->Spawn(
          "receive",
          [&receiver, &receive_order]() {
            auto receive_promise = receiver.Next();
            Poll<ValueOrFailure<Payload>> receive_result = receive_promise();
            absl::StrAppend(&receive_order, "R");
          },
          OnCompleteNoop());
    }
  });

  for (auto& thread : threads) {
    thread.join();  // Wait for all threads to finish and join.
  }

  // Asserting that all payloads were sent and received.
  for (int i = 0; i < kMpscNumThreads - 1; i++) {
    for (int j = 0; j < kMpscNumPayloads; j++) {
      // This check ensures that we sent all the payloads.
      EXPECT_TRUE(
          absl::StrContains(execution_order[i], absl::StrFormat("S%d", j)));
    }
  }
  // For every payload received, one "R" was appended to the receive order.
  // This check ensures that we received all the payloads.
  EXPECT_EQ(receive_order.length(), num_messages_sent);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
