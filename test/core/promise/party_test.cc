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

auto MakePromise(std::string& execution_order, int num) {
  return [i = num, &execution_order]() mutable -> Poll<int> {
    absl::StrAppend(&execution_order, "L");
    absl::StrAppend(&execution_order, i);
    return i;
  };
}

auto MakeOnDone(std::string& execution_order) {
  return [&execution_order](int num) mutable -> Poll<int> {
    absl::StrAppend(&execution_order, "D");
    absl::StrAppend(&execution_order, num);
    return num;
  };
}

TEST_F(PartyTest, TestLargeNumberOfSpawnedPromises) {
  // This test spawns a large number of promises
  // This test asserts the following:
  // 1. Promises are executed in the order they were spawned.
  // 2. on_done callback is called for each promise.
  // 3. The data type moves correctly from the promise to the on_done callback.
  // 4. A party is able to spawn a large number of promises, as long as they
  //    are not all Pending.
  const int kNumPromises = 256;
  std::string execution_order;
  auto party = MakeParty();
  for (int i = 1; i <= kNumPromises; ++i) {
    absl::StrAppend(&execution_order, ".");
    party->Spawn(absl::StrCat("p", i), MakePromise(execution_order, i),
                 MakeOnDone(execution_order));
  }
  std::string expected_execution_order;
  for (int i = 1; i <= kNumPromises; ++i) {
    absl::StrAppend(&expected_execution_order,
                    absl::StrFormat(".L%dD%d", i, i));
  }
  EXPECT_STREQ(execution_order.c_str(), expected_execution_order.c_str());
}

auto MakePendingPromise(std::string& execution_order, int num) {
  return [i = num, &execution_order]() mutable -> Poll<int> {
    absl::StrAppend(&execution_order, "P");
    absl::StrAppend(&execution_order, i);
    return Pending{};
  };
}

TEST_F(PartyTest, Test16SpawnedPendingPromises) {
  // This test spawns exactly 16 promises
  // This test asserts the following:
  // 1. Promises are executed in the order they were spawned.
  // 2. A party is able to spawn the Nth promise even if (N-1) are Pending for
  //    N<=16 (but not for N>16).
  // 3. If we try to spawn more than 16 promises, the code hangs because it is
  //    waiting for the promises to resolve (which they never will).
  // 4. on_done callback is never called for a promise that is not resolved.
  const int kNumPromises = 16;
  std::string execution_order;
  auto party = MakeParty();
  for (int i = 1; i <= kNumPromises; ++i) {
    absl::StrAppend(&execution_order, ".");
    party->Spawn(absl::StrCat("p", i), MakePendingPromise(execution_order, i),
                 MakeOnDone(execution_order));
  }
  std::string expected_execution_order;
  for (int i = 1; i <= kNumPromises; ++i) {
    absl::StrAppend(&expected_execution_order, ".P");
    absl::StrAppend(&expected_execution_order, i);
  }
  EXPECT_STREQ(execution_order.c_str(), expected_execution_order.c_str());
}

auto MakePendingPromiseResolve(std::string& execution_order, int num) {
  return [i = num, &execution_order]() mutable -> Poll<int> {
    if ((9 * i) < execution_order.length()) {
      // This condition makes sure that a few promises return pending a few
      // times before resolving.
      absl::StrAppend(&execution_order, "L");
      absl::StrAppend(&execution_order, i);
      return i;
    }
    GetContext<Activity>()->ForceImmediateRepoll();
    absl::StrAppend(&execution_order, "P");
    absl::StrAppend(&execution_order, i);
    return Pending{};
  };
}

TEST_F(PartyTest, TestSpawnedPendingPromisesResolve) {
  // This test spawns exactly 20 promises
  // This test asserts the following:
  // 1. A resolved promise is not polled again after it is resolved even if it
  //    was Pending in a previous poll.
  // 2. Earlier promises getting resolved creates a possibility for later
  //    promises to get spawned and executed.
  // 3. on_done callback is never called for a promise that is not resolved.
  //    It should be called immediately when the promise is resolved.
  const int kNumPromises = 20;
  std::string execution_order;
  auto party = MakeParty();
  for (int i = 1; i <= kNumPromises; ++i) {
    absl::StrAppend(&execution_order, ".");
    party->Spawn(absl::StrCat("p", i),
                 MakePendingPromiseResolve(execution_order, i),
                 MakeOnDone(execution_order));
  }
  EXPECT_STREQ(
      execution_order.c_str(),
      ".P1P1P1P1P1L1D1.P2P2L2D2.P3P3L3D3.P4P4L4D4.P5P5L5D5.P6P6L6D6.P7P7L7D7."
      "P8P8L8D8.P9P9L9D9.P10L10D10.P11L11D11.P12L12D12.L13D13.P14L14D14."
      "P15L15D15.L16D16.P17L17D17.P18L18D18.L19D19.P20L20D20");
}

TEST_F(PartyTest, SpawnAndRunOneParty) {
  // Simple test to run on promise on one party.
  // This test asserts the following:
  // 1. A promise can be spawned and run on a party.
  // 2. When a promise returns Pending and we ForceImmediateRepoll, the promise
  //    is polled over and over till it resolves.
  // 3. The on_done callback is called with the correct data.
  // 4. The promises are executed in the order we expect.
  std::string execution_order;
  auto party = MakeParty();
  Notification n;
  party->Spawn(
      "TestSpawn",
      [i = 5, &execution_order]() mutable -> Poll<int> {
        EXPECT_GT(i, 0);
        GetContext<Activity>()->ForceImmediateRepoll();
        --i;
        if (i == 0) {
          absl::StrAppend(&execution_order, "1");
          return 42;
        }
        absl::StrAppend(&execution_order, "P");
        return Pending{};
      },
      [&n, &execution_order](int x) {
        absl::StrAppend(&execution_order, "2");
        EXPECT_EQ(x, 42);
        n.Notify();
      });
  absl::StrAppend(&execution_order, "3");
  n.WaitForNotification();
  absl::StrAppend(&execution_order, "4");
  EXPECT_STREQ(execution_order.c_str(), "PPPP1234");
}

TEST_F(PartyTest, SpawnWaitableAndRunTwoParties) {
  // Test to run two promises on two parties.
  // The promise spawned on party1 will in turn spawn a promise on party2.
  // This test asserts the following:
  // 1. Testing the working of latches between two parties.
  // 2. The promises are executed in the order we expect.
  std::string execution_order;
  auto party1 = MakeParty();
  auto party2 = MakeParty();
  Notification n;
  InterActivityLatch<void> done;
  // Spawn a task on party1 that will wait for a task on party2.
  // The party2 task will wait on the latch `done`.
  party1->Spawn(
      "party1_main",
      [&party2, &done, &execution_order]() {
        absl::StrAppend(&execution_order, "1");
        return party2->SpawnWaitable("party2_main",
                                     [&done, &execution_order]() {
                                       absl::StrAppend(&execution_order, "2");
                                       return done.Wait();
                                     });
      },
      [&n, &execution_order](Empty) {
        absl::StrAppend(&execution_order, "3");
        n.Notify();
      });
  ASSERT_FALSE(n.HasBeenNotified());
  party1->Spawn(
      "party1_notify_latch",
      [&done, &execution_order]() {
        absl::StrAppend(&execution_order, "4");
        done.Set();
      },
      [&execution_order](Empty) { absl::StrAppend(&execution_order, "5"); });
  n.WaitForNotification();
  EXPECT_STREQ(execution_order.c_str(), "12453");
}

TEST_F(PartyTest, CanSpawnFromSpawn) {
  // Test to run two promises on two parties.
  // The promise spawned a party will in turn spawn another promise on the same
  // party. This test asserts the following:
  // 1. Spawning of one promise from inside another spawned promise onto the
  //    same party.
  // 2. The promises are executed in the order we expect.
  std::string execution_order;
  auto party = MakeParty();
  Notification n1;
  Notification n2;
  party->Spawn(
      "TestSpawn",
      [party, &n2, &execution_order]() -> Poll<int> {
        absl::StrAppend(&execution_order, "1");
        party->Spawn(
            "TestSpawnInner",
            [i = 5, &execution_order]() mutable -> Poll<int> {
              GetContext<Activity>()->ForceImmediateRepoll();
              --i;
              if (i == 0) {
                absl::StrAppend(&execution_order, "2");
                return 42;
              }
              absl::StrAppend(&execution_order, "P");
              return Pending{};
            },
            [&n2, &execution_order](int x) {
              absl::StrAppend(&execution_order, "3");
              EXPECT_EQ(x, 42);
              n2.Notify();
            });
        return 1234;
      },
      [&n1, &execution_order](int x) {
        absl::StrAppend(&execution_order, "4");
        EXPECT_EQ(x, 1234);
        n1.Notify();
      });
  n1.WaitForNotification();
  n2.WaitForNotification();
  EXPECT_STREQ(execution_order.c_str(), "14PPPP23");
}

// TODO(tjagtap)
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

// TODO(tjagtap)
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

// TODO(tjagtap)
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

// TODO(tjagtap)
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

// TODO(tjagtap)
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
  // Test for bulk spawning of promises.
  // One way to do bulk spawning is to use the Party::WakeupHold class.
  // When a WakeupHold is in scope, the party will not be polled until the
  // WakeupHold goes out of scope.
  // This test asserts the following:
  // 1. Spawning multiple promises in a WakeupHold works as expected. The
  // promises should not be polled until the WakeupHold goes out of scope.
  // 2. The promises are executed in the order we expect.
  auto party = MakeParty();
  Notification n1;
  Notification n2;
  std::string execution_order;
  {
    Party::WakeupHold hold(party.get());
    party->Spawn(
        "spawn1",
        [&execution_order]() { absl::StrAppend(&execution_order, "A"); },
        [&n1, &execution_order](Empty) {
          absl::StrAppend(&execution_order, "1");
          n1.Notify();
        });
    party->Spawn(
        "spawn2",
        [&execution_order]() { absl::StrAppend(&execution_order, "B"); },
        [&n2, &execution_order](Empty) {
          absl::StrAppend(&execution_order, "2");
          n2.Notify();
        });
    for (int i = 0; i < 5000; i++) {
      EXPECT_FALSE(n1.HasBeenNotified());
      EXPECT_FALSE(n2.HasBeenNotified());
      EXPECT_STREQ(execution_order.c_str(), "");
    }
  }
  n1.WaitForNotification();
  n2.WaitForNotification();
  EXPECT_STREQ(execution_order.c_str(), "A1B2");
}

TEST_F(PartyTest, CanNestWakeupHold) {
  // Test for bulk spawning of promises with nested WakeupHold objects.
  // One way to do bulk spawning is to use the Party::WakeupHold class.
  // When a WakeupHold is in scope, the party will not be polled until the
  // WakeupHold goes out of scope.
  // This test asserts the following:
  // 1. Spawning multiple promises in a WakeupHold works as expected. The
  //    promises should not be polled until the WakeupHold goes out of scope.
  // 2. The two Wakehold objects don't interfere with each other. Nesting the
  //    WakeupHold objects should not cause any change in the behavior.
  // 3. The promises are executed in the order we expect.
  // @Craig : Help please !! I don't understand how this is different from the
  // previous test. Why would we nest like this?
  auto party = MakeParty();
  Notification n1;
  Notification n2;
  std::string execution_order;
  {
    Party::WakeupHold hold1(party.get());
    Party::WakeupHold hold2(party.get());
    party->Spawn(
        "spawn1",
        [&execution_order]() { absl::StrAppend(&execution_order, "A"); },
        [&n1, &execution_order](Empty) {
          absl::StrAppend(&execution_order, "1");
          n1.Notify();
        });
    party->Spawn(
        "spawn2",
        [&execution_order]() { absl::StrAppend(&execution_order, "B"); },
        [&n2, &execution_order](Empty) {
          absl::StrAppend(&execution_order, "2");
          n2.Notify();
        });
    for (int i = 0; i < 5000; i++) {
      EXPECT_FALSE(n1.HasBeenNotified());
      EXPECT_FALSE(n2.HasBeenNotified());
      EXPECT_STREQ(execution_order.c_str(), "");
    }
  }
  n1.WaitForNotification();
  n2.WaitForNotification();
  EXPECT_STREQ(execution_order.c_str(), "A1B2");
}

TEST_F(PartyTest, ThreadStressTest) {
  constexpr int kNumThreads = 8;
  constexpr int kNumSpawns = 100;
  auto party = MakeParty();
  std::vector<std::string> execution_order(kNumThreads);
  std::vector<std::string> thread_order(kNumThreads);
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);

  for (int i = 0; i < kNumThreads; i++) {
    std::string& order = execution_order[i];
    absl::StrAppend(&order, absl::StrFormat("Thread %d : ", i));
    threads.emplace_back([&thread_order, thread_num = i, &order,
                          party]() mutable {
      for (int j = 0; j < kNumSpawns; j++) {
        ExecCtx ctx;  // needed for Sleep
        Notification promise_complete;
        party->Spawn(
            "TestSpawn",
            Seq(Sleep(Timestamp::Now() + Duration::Milliseconds(10)),
                [&thread_order, thread_num, &order,
                 spawn_num = j]() mutable -> Poll<int> {
                  thread_order[thread_num] = absl::StrFormat("%d", thread_num);
                  absl::StrAppend(&order, absl::StrFormat("%d(P%d,", thread_num,
                                                          spawn_num));
                  return spawn_num + 42;
                }),
            [&order, &promise_complete, spawn_num = j](int val) {
              EXPECT_EQ(val, spawn_num + 42);
              absl::StrAppend(&order, absl::StrFormat("D%d)", spawn_num));
              promise_complete.Notify();
            });
        promise_complete.WaitForNotification();
        absl::StrAppend(&order, ".");
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  std::vector<std::string> expected_order(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    absl::StrAppend(&expected_order[i], absl::StrFormat("Thread %d : ", i));
    for (int j = 0; j < kNumSpawns; j++) {
      absl::StrAppend(&expected_order[i],
                      absl::StrFormat("%d(P%d,D%d).", i, j, j));
    }
    EXPECT_STREQ(execution_order[i].c_str(), expected_order[i].c_str());
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

// TODO(tjagtap)
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

// TODO(tjagtap)
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

// TODO(tjagtap)
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

// TODO(tjagtap)
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

// TODO(tjagtap)
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

// TODO(tjagtap)
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
  // We have 3 parties - party1 , party2 and party3. party1 spawns promises onto
  // party2 and party3. When party1 finishes, party3 and party2 are run. Then
  // party2 and party3 go to sleep and are woken up alternately because they
  // depend on one another for notifications. This test asserts the following
  // 1. party2 and party3 should not run before party1 finishes running
  // 2. party2 and party3 need each other to do some processing and notification
  //    before they can proceed. So when party2 is waiting for a notification,
  //    party3 is woken up and begins its processing. When party3 is waiting for
  //    notification, party2 is woken up. We want to assert this expected sleep
  //    and wake cycle.
  // 3. WaitForNotification should cause a party to sleep if the Notification is
  //    not yet received.
  // 4. Asserting on the general expected order of execution
  auto party1 = MakeParty();
  auto party2 = MakeParty();
  auto party3 = MakeParty();
  std::string execution_order;
  Notification done1;
  Notification started2;
  Notification done2;
  Notification started3;
  Notification notify_done;
  party1->Spawn(
      "p1",
      [&]() {
        absl::StrAppend(&execution_order, "1");
        party2->Spawn(
            "p2",
            [&]() {
              done1.WaitForNotification();
              absl::StrAppend(&execution_order, "6");
              started2.Notify();
              started3.WaitForNotification();
              absl::StrAppend(&execution_order, "8");
            },
            [&](Empty) {
              absl::StrAppend(&execution_order, "9");
              done2.Notify();
              // absl::StrAppend(&execution_order, "A");
            });
        absl::StrAppend(&execution_order, "2");
        party3->Spawn(
            "p3",
            [&]() {
              started2.WaitForNotification();
              absl::StrAppend(&execution_order, "7");
              started3.Notify();
              done2.WaitForNotification();
              absl::StrAppend(&execution_order, "B");
            },
            [&](Empty) {
              absl::StrAppend(&execution_order, "C");
              notify_done.Notify();
            });
        absl::StrAppend(&execution_order, "3");
      },
      [&](Empty) {
        absl::StrAppend(&execution_order, "4");
        done1.Notify();
        // absl::StrAppend(&execution_order, "5");
      });
  absl::StrAppend(&execution_order, "D");
  notify_done.WaitForNotification();
  absl::StrAppend(&execution_order, "E");
  EXPECT_STREQ(execution_order.c_str(), "12346789BCDE");
  // EXPECT_STREQ(execution_order.c_str(), "123456789ABCDE");
  // Craig : Is the order of execution of party2 and party3 deterministic?
  // If yes - what is it based on?
  // Also why isn't the last line in the on_done getting executed in few cases?
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
