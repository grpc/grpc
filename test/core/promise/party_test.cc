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

#include <algorithm>
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
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"

namespace grpc_core {

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
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> ee_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
};

class PartyTest : public ::testing::Test {
 protected:
};

TEST_F(PartyTest, Noop) { auto party = MakeRefCounted<TestParty>(); }

TEST_F(PartyTest, CanSpawnAndRun) {
  auto party = MakeRefCounted<TestParty>();
  bool done = false;
  party->Spawn(
      "TestSpawn",
      [i = 10]() mutable -> Poll<int> {
        EXPECT_EQ(Activity::current()->DebugTag(), "TestParty");
        gpr_log(GPR_DEBUG, "i=%d", i);
        GPR_ASSERT(i > 0);
        Activity::current()->ForceImmediateRepoll();
        --i;
        if (i == 0) return 42;
        return Pending{};
      },
      [&done](int x) {
        EXPECT_EQ(x, 42);
        done = true;
      });
  EXPECT_TRUE(done);
}

TEST_F(PartyTest, CanSpawnFromSpawn) {
  auto party = MakeRefCounted<TestParty>();
  bool done1 = false;
  bool done2 = false;
  party->Spawn(
      "TestSpawn",
      [party, &done2]() -> Poll<int> {
        EXPECT_EQ(Activity::current()->DebugTag(), "TestParty");
        party->Spawn(
            "TestSpawnInner",
            [i = 10]() mutable -> Poll<int> {
              EXPECT_EQ(Activity::current()->DebugTag(), "TestParty");
              Activity::current()->ForceImmediateRepoll();
              --i;
              if (i == 0) return 42;
              return Pending{};
            },
            [&done2](int x) {
              EXPECT_EQ(x, 42);
              done2 = true;
            });
        return 1234;
      },
      [&done1](int x) {
        EXPECT_EQ(x, 1234);
        done1 = true;
      });
  EXPECT_TRUE(done1);
  EXPECT_TRUE(done2);
}

TEST_F(PartyTest, CanWakeupWithOwningWaker) {
  auto party = MakeRefCounted<TestParty>();
  bool done = false;
  Waker waker;
  party->Spawn(
      "TestSpawn",
      [i = 10, &waker]() mutable -> Poll<int> {
        EXPECT_EQ(Activity::current()->DebugTag(), "TestParty");
        waker = Activity::current()->MakeOwningWaker();
        --i;
        if (i == 0) return 42;
        return Pending{};
      },
      [&done](int x) {
        EXPECT_EQ(x, 42);
        done = true;
      });
  for (int i = 0; i < 9; i++) {
    EXPECT_FALSE(done) << i;
    waker.Wakeup();
  }
  EXPECT_TRUE(done);
}

TEST_F(PartyTest, CanWakeupWithNonOwningWaker) {
  auto party = MakeRefCounted<TestParty>();
  bool done = false;
  Waker waker;
  party->Spawn(
      "TestSpawn",
      [i = 10, &waker]() mutable -> Poll<int> {
        EXPECT_EQ(Activity::current()->DebugTag(), "TestParty");
        waker = Activity::current()->MakeNonOwningWaker();
        --i;
        if (i == 0) return 42;
        return Pending{};
      },
      [&done](int x) {
        EXPECT_EQ(x, 42);
        done = true;
      });
  for (int i = 0; i < 9; i++) {
    EXPECT_FALSE(done) << i;
    waker.Wakeup();
  }
  EXPECT_TRUE(done);
}

TEST_F(PartyTest, CanWakeupWithNonOwningWakerAfterOrphaning) {
  auto party = MakeRefCounted<TestParty>();
  bool done = false;
  Waker waker;
  party->Spawn(
      "TestSpawn",
      [i = 10, &waker]() mutable -> Poll<int> {
        EXPECT_EQ(Activity::current()->DebugTag(), "TestParty");
        waker = Activity::current()->MakeNonOwningWaker();
        --i;
        if (i == 0) return 42;
        return Pending{};
      },
      [&done](int x) {
        EXPECT_EQ(x, 42);
        done = true;
      });
  party.reset();
  EXPECT_FALSE(done);
  EXPECT_FALSE(waker.is_unwakeable());
  waker.Wakeup();
  EXPECT_TRUE(waker.is_unwakeable());
  EXPECT_FALSE(done);
}

TEST_F(PartyTest, CanDropNonOwningWakeAfterOrphaning) {
  auto party = MakeRefCounted<TestParty>();
  bool done = false;
  std::unique_ptr<Waker> waker;
  party->Spawn(
      "TestSpawn",
      [i = 10, &waker]() mutable -> Poll<int> {
        EXPECT_EQ(Activity::current()->DebugTag(), "TestParty");
        waker =
            std::make_unique<Waker>(Activity::current()->MakeNonOwningWaker());
        --i;
        if (i == 0) return 42;
        return Pending{};
      },
      [&done](int x) {
        EXPECT_EQ(x, 42);
        done = true;
      });
  party.reset();
  EXPECT_NE(waker, nullptr);
  waker.reset();
  EXPECT_FALSE(done);
}

TEST_F(PartyTest, CanWakeupNonOwningOrphanedWakerWithNoEffect) {
  auto party = MakeRefCounted<TestParty>();
  bool done = false;
  Waker waker;
  party->Spawn(
      "TestSpawn",
      [i = 10, &waker]() mutable -> Poll<int> {
        EXPECT_EQ(Activity::current()->DebugTag(), "TestParty");
        waker = Activity::current()->MakeNonOwningWaker();
        --i;
        if (i == 0) return 42;
        return Pending{};
      },
      [&done](int x) {
        EXPECT_EQ(x, 42);
        done = true;
      });
  EXPECT_FALSE(done);
  EXPECT_FALSE(waker.is_unwakeable());
  party.reset();
  waker.Wakeup();
  EXPECT_FALSE(done);
  EXPECT_TRUE(waker.is_unwakeable());
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
          waker_ = Activity::current()->MakeOwningWaker();
        } else {
          waker_ = Activity::current()->MakeNonOwningWaker();
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
