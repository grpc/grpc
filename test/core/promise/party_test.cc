// Copyright 2023 gRPC authors.
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

#include "src/core/lib/promise/party.h"

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
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/promise/mpsc.h"
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

///////////////////////////////////////////////////////////////////////////////
// PartyTest

class PartyTest : public ::testing::Test {
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

TEST_F(PartyTest, Noop) { auto party = MakeParty(); }

TEST_F(PartyTest, CanSpawnAndRun) {
  auto party = MakeParty();
  Notification n;
  party->Spawn(
      "TestSpawn",
      [i = 10]() mutable -> Poll<int> {
        EXPECT_GT(i, 0);
        GetContext<Activity>()->ForceImmediateRepoll();
        --i;
        if (i == 0) return 42;
        return Pending{};
      },
      [&n](int x) {
        EXPECT_EQ(x, 42);
        n.Notify();
      });
  n.WaitForNotification();
}

TEST_F(PartyTest, CanSpawnWaitableAndRun) {
  auto party1 = MakeParty();
  auto party2 = MakeParty();
  Notification n;
  InterActivityLatch<void> done;
  // Spawn a task on party1 that will wait for a task on party2.
  // The party2 task will wait on the latch `done`.
  party1->Spawn(
      "party1_main",
      [&party2, &done]() {
        return party2->SpawnWaitable("party2_main",
                                     [&done]() { return done.Wait(); });
      },
      [&n](Empty) { n.Notify(); });
  ASSERT_FALSE(n.HasBeenNotified());
  party1->Spawn("party1_notify_latch", [&done]() { done.Set(); }, [](Empty) {});
  n.WaitForNotification();
}

TEST_F(PartyTest, CanSpawnFromSpawn) {
  auto party = MakeParty();
  Notification n1;
  Notification n2;
  party->Spawn(
      "TestSpawn",
      [party, &n2]() -> Poll<int> {
        party->Spawn(
            "TestSpawnInner",
            [i = 10]() mutable -> Poll<int> {
              GetContext<Activity>()->ForceImmediateRepoll();
              --i;
              if (i == 0) return 42;
              return Pending{};
            },
            [&n2](int x) {
              EXPECT_EQ(x, 42);
              n2.Notify();
            });
        return 1234;
      },
      [&n1](int x) {
        EXPECT_EQ(x, 1234);
        n1.Notify();
      });
  n1.WaitForNotification();
  n2.WaitForNotification();
}

TEST_F(PartyTest, CanWakeupWithOwningWaker) {
  auto party = MakeParty();
  Notification n[10];
  Notification complete;
  Waker waker;
  party->Spawn(
      "TestSpawn",
      [i = 0, &waker, &n]() mutable -> Poll<int> {
        waker = GetContext<Activity>()->MakeOwningWaker();
        n[i].Notify();
        i++;
        if (i == 10) return 42;
        return Pending{};
      },
      [&complete](int x) {
        EXPECT_EQ(x, 42);
        complete.Notify();
      });
  for (int i = 0; i < 10; i++) {
    n[i].WaitForNotification();
    waker.Wakeup();
  }
  complete.WaitForNotification();
}

TEST_F(PartyTest, CanWakeupWithNonOwningWaker) {
  auto party = MakeParty();
  Notification n[10];
  Notification complete;
  Waker waker;
  party->Spawn(
      "TestSpawn",
      [i = 10, &waker, &n]() mutable -> Poll<int> {
        waker = GetContext<Activity>()->MakeNonOwningWaker();
        --i;
        n[9 - i].Notify();
        if (i == 0) return 42;
        return Pending{};
      },
      [&complete](int x) {
        EXPECT_EQ(x, 42);
        complete.Notify();
      });
  for (int i = 0; i < 9; i++) {
    n[i].WaitForNotification();
    EXPECT_FALSE(n[i + 1].HasBeenNotified());
    waker.Wakeup();
  }
  complete.WaitForNotification();
}

TEST_F(PartyTest, CanWakeupWithNonOwningWakerAfterOrphaning) {
  auto party = MakeParty();
  Notification set_waker;
  Waker waker;
  party->Spawn(
      "TestSpawn",
      [&waker, &set_waker]() mutable -> Poll<int> {
        EXPECT_FALSE(set_waker.HasBeenNotified());
        waker = GetContext<Activity>()->MakeNonOwningWaker();
        set_waker.Notify();
        return Pending{};
      },
      [](int) { Crash("unreachable"); });
  set_waker.WaitForNotification();
  party.reset();
  EXPECT_FALSE(waker.is_unwakeable());
  waker.Wakeup();
  EXPECT_TRUE(waker.is_unwakeable());
}

TEST_F(PartyTest, CanDropNonOwningWakeAfterOrphaning) {
  auto party = MakeParty();
  Notification set_waker;
  std::unique_ptr<Waker> waker;
  party->Spawn(
      "TestSpawn",
      [&waker, &set_waker]() mutable -> Poll<int> {
        EXPECT_FALSE(set_waker.HasBeenNotified());
        waker = std::make_unique<Waker>(
            GetContext<Activity>()->MakeNonOwningWaker());
        set_waker.Notify();
        return Pending{};
      },
      [](int) { Crash("unreachable"); });
  set_waker.WaitForNotification();
  party.reset();
  EXPECT_NE(waker, nullptr);
  waker.reset();
}

TEST_F(PartyTest, CanWakeupNonOwningOrphanedWakerWithNoEffect) {
  auto party = MakeParty();
  Notification set_waker;
  Waker waker;
  party->Spawn(
      "TestSpawn",
      [&waker, &set_waker]() mutable -> Poll<int> {
        EXPECT_FALSE(set_waker.HasBeenNotified());
        waker = GetContext<Activity>()->MakeNonOwningWaker();
        set_waker.Notify();
        return Pending{};
      },
      [](int) { Crash("unreachable"); });
  set_waker.WaitForNotification();
  EXPECT_FALSE(waker.is_unwakeable());
  party.reset();
  waker.Wakeup();
  EXPECT_TRUE(waker.is_unwakeable());
}

TEST_F(PartyTest, CanBulkSpawn) {
  auto party = MakeParty();
  Notification n1;
  Notification n2;
  {
    Party::WakeupHold hold(party.get());
    party->Spawn("spawn1", []() {}, [&n1](Empty) { n1.Notify(); });
    party->Spawn("spawn2", []() {}, [&n2](Empty) { n2.Notify(); });
    for (int i = 0; i < 5000; i++) {
      EXPECT_FALSE(n1.HasBeenNotified());
      EXPECT_FALSE(n2.HasBeenNotified());
    }
  }
  n1.WaitForNotification();
  n2.WaitForNotification();
}

TEST_F(PartyTest, CanNestWakeupHold) {
  auto party = MakeParty();
  Notification n1;
  Notification n2;
  {
    Party::WakeupHold hold1(party.get());
    Party::WakeupHold hold2(party.get());
    party->Spawn("spawn1", []() {}, [&n1](Empty) { n1.Notify(); });
    party->Spawn("spawn2", []() {}, [&n2](Empty) { n2.Notify(); });
    for (int i = 0; i < 5000; i++) {
      EXPECT_FALSE(n1.HasBeenNotified());
      EXPECT_FALSE(n2.HasBeenNotified());
    }
  }
  n1.WaitForNotification();
  n2.WaitForNotification();
}

TEST_F(PartyTest, ThreadStressTest) {
  auto party = MakeParty();
  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int i = 0; i < 8; i++) {
    threads.emplace_back([party]() {
      for (int i = 0; i < 100; i++) {
        ExecCtx ctx;  // needed for Sleep
        Notification promise_complete;
        party->Spawn("TestSpawn",
                     Seq(Sleep(Timestamp::Now() + Duration::Milliseconds(10)),
                         []() -> Poll<int> { return 42; }),
                     [&promise_complete](int i) {
                       EXPECT_EQ(i, 42);
                       promise_complete.Notify();
                     });
        promise_complete.WaitForNotification();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

class PromiseNotification {
 public:
  explicit PromiseNotification(bool owning_waker)
      : owning_waker_(owning_waker) {}

  auto Wait() {
    return [this]() -> Poll<int> {
      MutexLock lock(&mu_);
      if (done_) return 42;
      if (!polled_) {
        if (owning_waker_) {
          waker_ = GetContext<Activity>()->MakeOwningWaker();
        } else {
          waker_ = GetContext<Activity>()->MakeNonOwningWaker();
        }
        polled_ = true;
      }
      return Pending{};
    };
  }

  void Notify() {
    Waker waker;
    {
      MutexLock lock(&mu_);
      done_ = true;
      waker = std::move(waker_);
    }
    waker.Wakeup();
  }

  void NotifyUnderLock() {
    MutexLock lock(&mu_);
    done_ = true;
    waker_.WakeupAsync();
  }

 private:
  Mutex mu_;
  const bool owning_waker_;
  bool done_ ABSL_GUARDED_BY(mu_) = false;
  bool polled_ ABSL_GUARDED_BY(mu_) = false;
  Waker waker_ ABSL_GUARDED_BY(mu_);
};

TEST_F(PartyTest, ThreadStressTestWithOwningWaker) {
  auto party = MakeParty();
  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int i = 0; i < 8; i++) {
    threads.emplace_back([party]() {
      for (int i = 0; i < 100; i++) {
        ExecCtx ctx;  // needed for Sleep
        PromiseNotification promise_start(true);
        Notification promise_complete;
        party->Spawn("TestSpawn",
                     Seq(promise_start.Wait(),
                         Sleep(Timestamp::Now() + Duration::Milliseconds(10)),
                         []() -> Poll<int> { return 42; }),
                     [&promise_complete](int i) {
                       EXPECT_EQ(i, 42);
                       promise_complete.Notify();
                     });
        promise_start.Notify();
        promise_complete.WaitForNotification();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(PartyTest, ThreadStressTestWithOwningWakerHoldingLock) {
  auto party = MakeParty();
  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int i = 0; i < 8; i++) {
    threads.emplace_back([party]() {
      for (int i = 0; i < 100; i++) {
        ExecCtx ctx;  // needed for Sleep
        PromiseNotification promise_start(true);
        Notification promise_complete;
        party->Spawn("TestSpawn",
                     Seq(promise_start.Wait(),
                         Sleep(Timestamp::Now() + Duration::Milliseconds(10)),
                         []() -> Poll<int> { return 42; }),
                     [&promise_complete](int i) {
                       EXPECT_EQ(i, 42);
                       promise_complete.Notify();
                     });
        promise_start.NotifyUnderLock();
        promise_complete.WaitForNotification();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(PartyTest, ThreadStressTestWithNonOwningWaker) {
  auto party = MakeParty();
  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int i = 0; i < 8; i++) {
    threads.emplace_back([party]() {
      for (int i = 0; i < 100; i++) {
        ExecCtx ctx;  // needed for Sleep
        PromiseNotification promise_start(false);
        Notification promise_complete;
        party->Spawn("TestSpawn",
                     Seq(promise_start.Wait(),
                         Sleep(Timestamp::Now() + Duration::Milliseconds(10)),
                         []() -> Poll<int> { return 42; }),
                     [&promise_complete](int i) {
                       EXPECT_EQ(i, 42);
                       promise_complete.Notify();
                     });
        promise_start.Notify();
        promise_complete.WaitForNotification();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(PartyTest, ThreadStressTestWithOwningWakerNoSleep) {
  auto party = MakeParty();
  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int i = 0; i < 8; i++) {
    threads.emplace_back([party]() {
      for (int i = 0; i < 10000; i++) {
        PromiseNotification promise_start(true);
        Notification promise_complete;
        party->Spawn(
            "TestSpawn",
            Seq(promise_start.Wait(), []() -> Poll<int> { return 42; }),
            [&promise_complete](int i) {
              EXPECT_EQ(i, 42);
              promise_complete.Notify();
            });
        promise_start.Notify();
        promise_complete.WaitForNotification();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(PartyTest, ThreadStressTestWithNonOwningWakerNoSleep) {
  auto party = MakeParty();
  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int i = 0; i < 8; i++) {
    threads.emplace_back([party]() {
      for (int i = 0; i < 10000; i++) {
        PromiseNotification promise_start(false);
        Notification promise_complete;
        party->Spawn(
            "TestSpawn",
            Seq(promise_start.Wait(), []() -> Poll<int> { return 42; }),
            [&promise_complete](int i) {
              EXPECT_EQ(i, 42);
              promise_complete.Notify();
            });
        promise_start.Notify();
        promise_complete.WaitForNotification();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(PartyTest, ThreadStressTestWithInnerSpawn) {
  auto party = MakeParty();
  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int i = 0; i < 8; i++) {
    threads.emplace_back([party]() {
      for (int i = 0; i < 100; i++) {
        ExecCtx ctx;  // needed for Sleep
        PromiseNotification inner_start(true);
        PromiseNotification inner_complete(false);
        Notification promise_complete;
        party->Spawn(
            "TestSpawn",
            Seq(
                [party, &inner_start, &inner_complete]() -> Poll<int> {
                  party->Spawn("TestSpawnInner",
                               Seq(inner_start.Wait(), []() { return 0; }),
                               [&inner_complete](int i) {
                                 EXPECT_EQ(i, 0);
                                 inner_complete.Notify();
                               });
                  return 0;
                },
                Sleep(Timestamp::Now() + Duration::Milliseconds(10)),
                [&inner_start]() {
                  inner_start.Notify();
                  return 0;
                },
                inner_complete.Wait(), []() -> Poll<int> { return 42; }),
            [&promise_complete](int i) {
              EXPECT_EQ(i, 42);
              promise_complete.Notify();
            });
        promise_complete.WaitForNotification();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(PartyTest, NestedWakeup) {
  auto party1 = MakeParty();
  auto party2 = MakeParty();
  auto party3 = MakeParty();
  int whats_going_on = 0;
  Notification done1;
  Notification started2;
  Notification done2;
  Notification started3;
  Notification notify_done;
  party1->Spawn(
      "p1",
      [&]() {
        EXPECT_EQ(whats_going_on, 0);
        whats_going_on = 1;
        party2->Spawn(
            "p2",
            [&]() {
              done1.WaitForNotification();
              started2.Notify();
              started3.WaitForNotification();
              EXPECT_EQ(whats_going_on, 3);
              whats_going_on = 4;
            },
            [&](Empty) {
              EXPECT_EQ(whats_going_on, 4);
              whats_going_on = 5;
              done2.Notify();
            });
        party3->Spawn(
            "p3",
            [&]() {
              started2.WaitForNotification();
              started3.Notify();
              done2.WaitForNotification();
              EXPECT_EQ(whats_going_on, 5);
              whats_going_on = 6;
            },
            [&](Empty) {
              EXPECT_EQ(whats_going_on, 6);
              whats_going_on = 7;
              notify_done.Notify();
            });
        EXPECT_EQ(whats_going_on, 1);
        whats_going_on = 2;
      },
      [&](Empty) {
        EXPECT_EQ(whats_going_on, 2);
        whats_going_on = 3;
        done1.Notify();
      });
  notify_done.WaitForNotification();
}

// Testing Promise Parties with MPSC Queues

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

auto MakeSenderPromise(MpscSender<Payload>& sender, Notification& sent,
                       std::string& execution_order, int value) {
  return [&sender, &sent, &execution_order, value]() {
    auto send_promise = sender.Send(MakePayload(value));
    Poll<bool> send_result = send_promise();
    // Even though we know that sending is not complete, we don't know when it
    // will be complete. There is no callback mechanism. We may send our sent
    // notification before the send is complete. What is the solution for this?
    // EXPECT_TRUE(send_result.ready());
    absl::StrAppend(&execution_order, "S", value);
    sent.Notify();
  };
}

auto MakeReceiverPromise(MpscReceiver<Payload>& receiver, Notification& sent,
                         std::string& execution_order, int value) {
  return [&receiver, &sent, &execution_order, value]() {
    sent.WaitForNotification();
    auto receive_promise = receiver.Next();
    Poll<ValueOrFailure<Payload>> receive_result = receive_promise();
    absl::StrAppend(&execution_order, "R", value);
    LOG(INFO) << "Received " << value;
  };
}

auto OnCompleteNotify(Notification& notification) {
  return [&notification](Empty) { notification.Notify(); };
}

auto OnCompleteNoop() {
  return [](Empty) {};
}

TEST_F(PartyTest, MpscOneSenderOneReceiverTest) {
  // Number of Receivers = 1  // Will be 1 always for MPSC
  // Number of Senders   = 1
  // Number of Payloads  = 1
  // Number of Parties   = 1
  // Number of Threads   = 1

  // Very basic MPSC & Party test.
  // Just send one and receive one message using one party.
  MpscReceiver<Payload> receiver(1);
  MpscSender<Payload> sender = receiver.MakeSender();
  auto party = MakeParty();
  Notification sent;
  Notification done;
  std::string execution_order;
  party->Spawn("sender", MakeSenderPromise(sender, sent, execution_order, 42),
               OnCompleteNoop());
  party->Spawn("receiver",
               MakeReceiverPromise(receiver, sent, execution_order, 42),
               OnCompleteNotify(done));
  done.WaitForNotification();
  // TODO(tjagtap): This crashes!! Is that expected?
  // How do we know that we have read till the very end?
  // auto nothing = receiver.Next()();
}

constexpr int kMpscNumPayloads = 20;
constexpr int kMpscNumThreads = 8;
constexpr int kMpscSleepMs = 10;

TEST_F(PartyTest, MpscManySendersOnePartyStressTest) {
  // Number of Receivers = 1  // Will be 1 always for MPSC
  // Number of Senders   = kMpscNumThreads
  // Number of Payloads  = kMpscNumThreads * kMpscNumPayloads
  // Number of Parties   = 1
  // Number of Threads   = kMpscNumThreads

  // Stress testing MPSC & Party.
  // This test has several threads, each thread having its own MPSC sender.
  // Each thread sends multiple payloads to the receiver.
  // The payloads one a single thread are sent and received in order, but the
  // threads can interleave.
  // Asserts
  // 1. All payloads are sent and received.
  // 2. The payloads on a single thread are received in order.
  // 3. If there is a bug in MPSC which causes TSAN failure, there is a small
  // chance that this test will trigger that case and fail TSAN.

  std::vector<std::string> execution_order(kMpscNumThreads);
  MpscReceiver<Payload> receiver(1);
  std::vector<MpscSender<Payload>> senders;
  for (int i = 0; i < kMpscNumThreads; i++) {
    senders.emplace_back(receiver.MakeSender());
  }
  auto party = MakeParty();
  std::vector<std::thread> threads;
  threads.reserve(kMpscNumThreads);
  for (int i = 0; i < kMpscNumThreads; i++) {
    MpscSender<Payload>& sender = senders[i];
    std::string& order = execution_order[i];
    threads.emplace_back([&order, &party, &sender, &receiver]() {
      for (int j = 0; j < kMpscNumPayloads; j++) {
        ExecCtx ctx;  // needed for Sleep
        Notification sent;
        Notification done;
        party->Spawn(
            "sned_and_receive",
            Seq(MakeSenderPromise(sender, sent, order, j),
                Sleep(Timestamp::Now() + Duration::Milliseconds(kMpscSleepMs)),
                MakeReceiverPromise(receiver, sent, order, j)),
            OnCompleteNotify(done));
        done.WaitForNotification();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  std::vector<std::string> expected_order(kMpscNumThreads);
  for (int i = 0; i < kMpscNumThreads; i++) {
    // Generate the expected order for each thread.
    for (int j = 0; j < kMpscNumPayloads; j++) {
      absl::StrAppend(&expected_order[i], "S", j, "R", j);
    }
    // For the given test, the order is guaranteed because we wait for the
    // done notification before the next loop iteration can start.
    EXPECT_STREQ(execution_order[i].c_str(), expected_order[i].c_str());
  }
}

TEST_F(PartyTest, MpscManySendersManyPartyStressTest) {
  // Number of Receivers = 1  // Will be 1 always for MPSC
  // Number of Senders   = kMpscNumThreads - 1
  // Number of Payloads  = (kMpscNumThreads -1) * kMpscNumPayloads
  // Number of Parties   = kMpscNumThreads
  // Number of Threads   = kMpscNumThreads

  // Stress testing MPSC & Party.
  // This is the same as the above test, but with multiple parties.
  // Using multiple parties, on a different thread will ensure that we have
  // multiple threads concurrently trying to send on the same MPSC.
  // We will have only one receiver.
  // Asserts
  // 1. All payloads are sent and received.
  // 2. If there is a bug in MPSC which causes TSAN failure, there is a high
  // chance that this test will trigger that case and fail TSAN.

  std::vector<std::string> execution_order(kMpscNumThreads);
  MpscReceiver<Payload> receiver(1);
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

  // Send payloads from all senders on different parties and different threads.
  for (int i = 0; i < kMpscNumThreads - 1; i++) {
    MpscSender<Payload>& sender = senders[i];
    std::string& order = execution_order[i];
    RefCountedPtr<Party>& party = parties[i];
    threads.emplace_back([&order, &party, &sender]() {
      for (int j = 0; j < kMpscNumPayloads; j++) {
        ExecCtx ctx;  // needed for Sleep
        Notification sent;
        party->Spawn(
            "send",
            [&sender, &order, value = j]() {
              auto send_promise = sender.Send(MakePayload(value));
              Poll<bool> send_result = send_promise();
              absl::StrAppend(&order, "S", value);
            },
            OnCompleteNoop());
      }
    });
  }

  // Receive payloads on the last party and last thread.
  const int num_messages_sent = (kMpscNumThreads - 1) * kMpscNumPayloads;
  std::string& receive_order = execution_order[kMpscNumThreads - 1];
  RefCountedPtr<Party>& party = parties[kMpscNumThreads - 1];
  threads.emplace_back([&receive_order, &party, &receiver]() {
    for (int j = 0; j < num_messages_sent; j++) {
      ExecCtx ctx;  // needed for Sleep
      party->Spawn(
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
    thread.join();
  }
  for (int i = 0; i < kMpscNumThreads - 1; i++) {
    // Generate the expected order for each thread.
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
