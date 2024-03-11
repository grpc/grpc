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

#include <stdio.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"

namespace grpc_core {

///////////////////////////////////////////////////////////////////////////////
// PartySyncTest

template <typename T>
class PartySyncTest : public ::testing::Test {};

// PartySyncUsingMutex isn't working on Mac, but we don't use it for anything
// right now so that's fine.
#ifdef GPR_APPLE
using PartySyncTypes = ::testing::Types<PartySyncUsingAtomics>;
#else
using PartySyncTypes =
    ::testing::Types<PartySyncUsingAtomics, PartySyncUsingMutex>;
#endif
TYPED_TEST_SUITE(PartySyncTest, PartySyncTypes);

TYPED_TEST(PartySyncTest, NoOp) { TypeParam sync(1); }

TYPED_TEST(PartySyncTest, RefAndUnref) {
  Notification half_way;
  TypeParam sync(1);
  std::thread thread1([&] {
    for (int i = 0; i < 1000000; i++) {
      sync.IncrementRefCount();
    }
    half_way.Notify();
    for (int i = 0; i < 1000000; i++) {
      sync.IncrementRefCount();
    }
    for (int i = 0; i < 2000000; i++) {
      EXPECT_FALSE(sync.Unref());
    }
  });
  half_way.WaitForNotification();
  for (int i = 0; i < 2000000; i++) {
    sync.IncrementRefCount();
  }
  for (int i = 0; i < 2000000; i++) {
    EXPECT_FALSE(sync.Unref());
  }
  thread1.join();
  EXPECT_TRUE(sync.Unref());
}

TYPED_TEST(PartySyncTest, AddAndRemoveParticipant) {
  TypeParam sync(1);
  std::vector<std::thread> threads;
  std::atomic<std::atomic<bool>*> participants[party_detail::kMaxParticipants] =
      {};
  threads.reserve(8);
  for (int i = 0; i < 8; i++) {
    threads.emplace_back([&] {
      for (int i = 0; i < 100000; i++) {
        auto done = std::make_unique<std::atomic<bool>>(false);
        int slot = -1;
        bool run = sync.AddParticipantsAndRef(1, [&](size_t* idxs) {
          slot = idxs[0];
          participants[slot].store(done.get(), std::memory_order_release);
        });
        EXPECT_NE(slot, -1);
        if (run) {
          bool run_any = false;
          bool run_me = false;
          EXPECT_FALSE(sync.RunParty([&](int slot) {
            run_any = true;
            std::atomic<bool>* participant =
                participants[slot].exchange(nullptr, std::memory_order_acquire);
            if (participant == done.get()) run_me = true;
            if (participant == nullptr) {
              gpr_log(GPR_ERROR,
                      "Participant was null (spurious wakeup observed)");
              return false;
            }
            participant->store(true, std::memory_order_release);
            return true;
          }));
          EXPECT_TRUE(run_any);
          EXPECT_TRUE(run_me);
        }
        EXPECT_FALSE(sync.Unref());
        while (!done->load(std::memory_order_acquire)) {
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_TRUE(sync.Unref());
}

TYPED_TEST(PartySyncTest, AddAndRemoveTwoParticipants) {
  TypeParam sync(1);
  std::vector<std::thread> threads;
  std::atomic<std::atomic<int>*> participants[party_detail::kMaxParticipants] =
      {};
  threads.reserve(8);
  for (int i = 0; i < 4; i++) {
    threads.emplace_back([&] {
      for (int i = 0; i < 100000; i++) {
        auto done = std::make_unique<std::atomic<int>>(2);
        int slots[2] = {-1, -1};
        bool run = sync.AddParticipantsAndRef(2, [&](size_t* idxs) {
          for (int i = 0; i < 2; i++) {
            slots[i] = idxs[i];
            participants[slots[i]].store(done.get(), std::memory_order_release);
          }
        });
        EXPECT_NE(slots[0], -1);
        EXPECT_NE(slots[1], -1);
        EXPECT_GT(slots[1], slots[0]);
        if (run) {
          bool run_any = false;
          int run_me = 0;
          EXPECT_FALSE(sync.RunParty([&](int slot) {
            run_any = true;
            std::atomic<int>* participant =
                participants[slot].exchange(nullptr, std::memory_order_acquire);
            if (participant == done.get()) run_me++;
            if (participant == nullptr) {
              gpr_log(GPR_ERROR,
                      "Participant was null (spurious wakeup observed)");
              return false;
            }
            participant->fetch_sub(1, std::memory_order_release);
            return true;
          }));
          EXPECT_TRUE(run_any);
          EXPECT_EQ(run_me, 2);
        }
        EXPECT_FALSE(sync.Unref());
        while (done->load(std::memory_order_acquire) != 0) {
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_TRUE(sync.Unref());
}

TYPED_TEST(PartySyncTest, UnrefWhileRunning) {
  std::vector<std::thread> trials;
  std::atomic<int> delete_paths_taken[3] = {{0}, {0}, {0}};
  trials.reserve(100);
  for (int i = 0; i < 100; i++) {
    trials.emplace_back([&delete_paths_taken] {
      TypeParam sync(1);
      int delete_path = -1;
      EXPECT_TRUE(sync.AddParticipantsAndRef(
          1, [](size_t* slots) { EXPECT_EQ(slots[0], 0); }));
      std::thread run_party([&] {
        if (sync.RunParty([&sync, n = 0](int slot) mutable {
              EXPECT_EQ(slot, 0);
              ++n;
              if (n < 10) {
                sync.ForceImmediateRepoll(1);
                return false;
              }
              return true;
            })) {
          delete_path = 0;
        }
      });
      std::thread unref([&] {
        if (sync.Unref()) delete_path = 1;
      });
      if (sync.Unref()) delete_path = 2;
      run_party.join();
      unref.join();
      EXPECT_GE(delete_path, 0);
      delete_paths_taken[delete_path].fetch_add(1, std::memory_order_relaxed);
    });
  }
  for (auto& trial : trials) {
    trial.join();
  }
  fprintf(stderr, "DELETE_PATHS: RunParty:%d AsyncUnref:%d SyncUnref:%d\n",
          delete_paths_taken[0].load(), delete_paths_taken[1].load(),
          delete_paths_taken[2].load());
}

///////////////////////////////////////////////////////////////////////////////
// PartyTest

class AllocatorOwner {
 protected:
  ~AllocatorOwner() { arena_->Destroy(); }
  MemoryAllocator memory_allocator_ = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
  Arena* arena_ = Arena::Create(1024, &memory_allocator_);
};

class TestParty final : public AllocatorOwner, public Party {
 public:
  TestParty() : Party(AllocatorOwner::arena_, 1) {}
  ~TestParty() override {}
  std::string DebugTag() const override { return "TestParty"; }

  using Party::IncrementRefCount;
  using Party::Unref;

  bool RunParty() override {
    promise_detail::Context<grpc_event_engine::experimental::EventEngine>
        ee_ctx(ee_.get());
    return Party::RunParty();
  }

  void PartyOver() override {
    {
      promise_detail::Context<grpc_event_engine::experimental::EventEngine>
          ee_ctx(ee_.get());
      CancelRemainingParticipants();
    }
    delete this;
  }

 private:
  grpc_event_engine::experimental::EventEngine* event_engine() const final {
    return ee_.get();
  }

  std::shared_ptr<grpc_event_engine::experimental::EventEngine> ee_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
};

class PartyTest : public ::testing::Test {
 protected:
};

TEST_F(PartyTest, Noop) { auto party = MakeRefCounted<TestParty>(); }

TEST_F(PartyTest, CanSpawnAndRun) {
  auto party = MakeRefCounted<TestParty>();
  Notification n;
  party->Spawn(
      "TestSpawn",
      [i = 10]() mutable -> Poll<int> {
        EXPECT_EQ(GetContext<Activity>()->DebugTag(), "TestParty");
        gpr_log(GPR_DEBUG, "i=%d", i);
        GPR_ASSERT(i > 0);
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
  auto party1 = MakeRefCounted<TestParty>();
  auto party2 = MakeRefCounted<TestParty>();
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
  party1->Spawn(
      "party1_notify_latch",
      [&done]() {
        done.Set();
        return Empty{};
      },
      [](Empty) {});
  n.WaitForNotification();
}

TEST_F(PartyTest, CanSpawnFromSpawn) {
  auto party = MakeRefCounted<TestParty>();
  Notification n1;
  Notification n2;
  party->Spawn(
      "TestSpawn",
      [party, &n2]() -> Poll<int> {
        EXPECT_EQ(GetContext<Activity>()->DebugTag(), "TestParty");
        party->Spawn(
            "TestSpawnInner",
            [i = 10]() mutable -> Poll<int> {
              EXPECT_EQ(GetContext<Activity>()->DebugTag(), "TestParty");
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
  auto party = MakeRefCounted<TestParty>();
  Notification n[10];
  Notification complete;
  Waker waker;
  party->Spawn(
      "TestSpawn",
      [i = 0, &waker, &n]() mutable -> Poll<int> {
        EXPECT_EQ(GetContext<Activity>()->DebugTag(), "TestParty");
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
  auto party = MakeRefCounted<TestParty>();
  Notification n[10];
  Notification complete;
  Waker waker;
  party->Spawn(
      "TestSpawn",
      [i = 10, &waker, &n]() mutable -> Poll<int> {
        EXPECT_EQ(GetContext<Activity>()->DebugTag(), "TestParty");
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
  auto party = MakeRefCounted<TestParty>();
  Notification set_waker;
  Waker waker;
  party->Spawn(
      "TestSpawn",
      [&waker, &set_waker]() mutable -> Poll<int> {
        EXPECT_FALSE(set_waker.HasBeenNotified());
        EXPECT_EQ(GetContext<Activity>()->DebugTag(), "TestParty");
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
  auto party = MakeRefCounted<TestParty>();
  Notification set_waker;
  std::unique_ptr<Waker> waker;
  party->Spawn(
      "TestSpawn",
      [&waker, &set_waker]() mutable -> Poll<int> {
        EXPECT_FALSE(set_waker.HasBeenNotified());
        EXPECT_EQ(GetContext<Activity>()->DebugTag(), "TestParty");
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
  auto party = MakeRefCounted<TestParty>();
  Notification set_waker;
  Waker waker;
  party->Spawn(
      "TestSpawn",
      [&waker, &set_waker]() mutable -> Poll<int> {
        EXPECT_FALSE(set_waker.HasBeenNotified());
        EXPECT_EQ(GetContext<Activity>()->DebugTag(), "TestParty");
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
  auto party = MakeRefCounted<TestParty>();
  Notification n1;
  Notification n2;
  {
    Party::BulkSpawner spawner(party.get());
    spawner.Spawn(
        "spawn1", []() { return Empty{}; }, [&n1](Empty) { n1.Notify(); });
    spawner.Spawn(
        "spawn2", []() { return Empty{}; }, [&n2](Empty) { n2.Notify(); });
    for (int i = 0; i < 5000; i++) {
      EXPECT_FALSE(n1.HasBeenNotified());
      EXPECT_FALSE(n2.HasBeenNotified());
    }
  }
  n1.WaitForNotification();
  n2.WaitForNotification();
}

TEST_F(PartyTest, AfterCurrentPollWorks) {
  auto party = MakeRefCounted<TestParty>();
  Notification n;
  int state = 0;
  {
    Party::BulkSpawner spawner(party.get());
    // BulkSpawner will schedule and poll this promise first, but the
    // `AfterCurrentPoll` will pause it.
    // Then spawn1, spawn2, and spawn3 will run in order (with EXPECT_EQ checks
    // demonstrating this), at which point the poll will complete, causing
    // spawn_final to be awoken and scheduled and see the final state.
    spawner.Spawn(
        "spawn_final",
        [&state, &party]() {
          return Seq(party->AfterCurrentPoll(), [&state]() {
            EXPECT_EQ(state, 3);
            return Empty{};
          });
        },
        [&n](Empty) { n.Notify(); });
    spawner.Spawn(
        "spawn1",
        [&state]() {
          EXPECT_EQ(state, 0);
          state = 1;
          return Empty{};
        },
        [](Empty) {});
    spawner.Spawn(
        "spawn2",
        [&state]() {
          EXPECT_EQ(state, 1);
          state = 2;
          return Empty{};
        },
        [](Empty) {});
    spawner.Spawn(
        "spawn3",
        [&state]() {
          EXPECT_EQ(state, 2);
          state = 3;
          return Empty{};
        },
        [](Empty) {});
  }
  n.WaitForNotification();
}

TEST_F(PartyTest, ThreadStressTest) {
  auto party = MakeRefCounted<TestParty>();
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
  auto party = MakeRefCounted<TestParty>();
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
  auto party = MakeRefCounted<TestParty>();
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
  auto party = MakeRefCounted<TestParty>();
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
  auto party = MakeRefCounted<TestParty>();
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
  auto party = MakeRefCounted<TestParty>();
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
  auto party = MakeRefCounted<TestParty>();
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

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
