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

#include <memory>
#include <thread>

#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"

namespace grpc_core {

class TestParty final : public Party {
 public:
  using Party::Party;
  std::string DebugTag() const override { return "TestParty"; }
};

class PartyTest : public ::testing::Test {
 protected:
  MemoryAllocator memory_allocator_ = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
};

TEST_F(PartyTest, Noop) {
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  auto party = MakeOrphanable<TestParty>(arena.get());
}

TEST_F(PartyTest, CanSpawnAndRun) {
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  auto party = MakeOrphanable<TestParty>(arena.get());
  bool done = false;
  party->Spawn(
      [i = 10]() mutable -> Poll<int> {
        EXPECT_EQ(Activity::current()->DebugTag(), "TestParty");
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
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  auto party = MakeOrphanable<TestParty>(arena.get());
  bool done1 = false;
  bool done2 = false;
  party->Spawn(
      [party = party.get(), &done2]() -> Poll<int> {
        EXPECT_EQ(Activity::current()->DebugTag(), "TestParty");
        party->Spawn(
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
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  auto party = MakeOrphanable<TestParty>(arena.get());
  bool done = false;
  Waker waker;
  party->Spawn(
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
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  auto party = MakeOrphanable<TestParty>(arena.get());
  bool done = false;
  Waker waker;
  party->Spawn(
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
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  auto party = MakeOrphanable<TestParty>(arena.get());
  bool done = false;
  Waker waker;
  party->Spawn(
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
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  auto party = MakeOrphanable<TestParty>(arena.get());
  bool done = false;
  std::unique_ptr<Waker> waker;
  party->Spawn(
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
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  auto party = MakeOrphanable<TestParty>(arena.get());
  bool done = false;
  Waker waker;
  party->Spawn(
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
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  auto party = MakeOrphanable<TestParty>(arena.get());
  std::vector<std::thread> threads;
  threads.reserve(11);
  for (int i = 0; i < 10; i++) {
    threads.emplace_back([party = party.get()]() {
      for (int i = 0; i < 100; i++) {
        party->Spawn(
            [party]() -> Poll<int> {
              EXPECT_EQ(Activity::current()->DebugTag(), "TestParty");
              party->Spawn(
                  [i = 10]() mutable -> Poll<int> {
                    EXPECT_EQ(Activity::current()->DebugTag(), "TestParty");
                    Activity::current()->ForceImmediateRepoll();
                    --i;
                    if (i == 0) return 42;
                    return Pending{};
                  },
                  [](int x) { EXPECT_EQ(x, 42); });
              return 1234;
            },
            [](int x) { EXPECT_EQ(x, 1234); });
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
  return RUN_ALL_TESTS();
}
