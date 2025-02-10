//
//
// Copyright 2017 gRPC authors.
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

#include "src/core/lib/resource_quota/arena.h"

#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <iosfwd>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "absl/strings/str_join.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/thd.h"
#include "test/core/test_util/test_config.h"

using testing::Mock;
using testing::Return;
using testing::StrictMock;

namespace grpc_core {

struct AllocShape {
  size_t initial_size;
  std::vector<size_t> allocs;
};

std::ostream& operator<<(std::ostream& out, const AllocShape& shape) {
  out << "AllocShape{initial_size=" << shape.initial_size
      << ", allocs=" << absl::StrJoin(shape.allocs, ",") << "}";
  return out;
}

class AllocTest : public ::testing::TestWithParam<AllocShape> {};

TEST_P(AllocTest, Works) {
  ExecCtx exec_ctx;
  RefCountedPtr<Arena> a =
      SimpleArenaAllocator(GetParam().initial_size)->MakeArena();
  std::vector<void*> allocated;
  for (auto alloc : GetParam().allocs) {
    void* p = a->Alloc(alloc);
    // ensure the returned address is aligned
    EXPECT_EQ(((intptr_t)p & 0xf), 0);
    // ensure no duplicate results
    for (auto other_p : allocated) {
      EXPECT_NE(p, other_p);
    }
    // ensure writable
    memset(p, 1, alloc);
    allocated.push_back(p);
  }
}

INSTANTIATE_TEST_SUITE_P(
    AllocTests, AllocTest,
    ::testing::Values(AllocShape{0, {1}}, AllocShape{1, {1}},
                      AllocShape{1, {2}}, AllocShape{1, {3}},
                      AllocShape{1, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
                      AllocShape{6, {1, 2, 3}}));

class MockMemoryAllocatorImpl
    : public grpc_event_engine::experimental::internal::MemoryAllocatorImpl {
 public:
  MOCK_METHOD(size_t, Reserve, (MemoryRequest));
  MOCK_METHOD(grpc_slice, MakeSlice, (MemoryRequest));
  MOCK_METHOD(void, Release, (size_t));
  MOCK_METHOD(void, Shutdown, ());
};

TEST(ArenaTest, InitialReservationCorrect) {
  auto allocator_impl = std::make_shared<StrictMock<MockMemoryAllocatorImpl>>();
  auto allocator = SimpleArenaAllocator(1024, MemoryAllocator(allocator_impl));
  EXPECT_CALL(*allocator_impl, Reserve(MemoryRequest(1024, 1024)))
      .WillOnce(Return(1024));
  auto arena = allocator->MakeArena();
  Mock::VerifyAndClearExpectations(allocator_impl.get());
  EXPECT_CALL(*allocator_impl, Release(1024));
  arena.reset();
  Mock::VerifyAndClearExpectations(allocator_impl.get());
  EXPECT_CALL(*allocator_impl, Shutdown());
}

TEST(ArenaTest, SubsequentReservationCorrect) {
  auto allocator_impl = std::make_shared<StrictMock<MockMemoryAllocatorImpl>>();
  auto allocator = SimpleArenaAllocator(1024, MemoryAllocator(allocator_impl));
  EXPECT_CALL(*allocator_impl, Reserve(MemoryRequest(1024, 1024)))
      .WillOnce(Return(1024));
  auto arena = allocator->MakeArena();
  Mock::VerifyAndClearExpectations(allocator_impl.get());
  EXPECT_CALL(*allocator_impl,
              Reserve(MemoryRequest(4096 + Arena::ArenaZoneOverhead(),
                                    4096 + Arena::ArenaZoneOverhead())))
      .WillOnce(Return(4096 + Arena::ArenaZoneOverhead()));
  arena->Alloc(4096);
  Mock::VerifyAndClearExpectations(allocator_impl.get());
  EXPECT_CALL(*allocator_impl,
              Release(1024 + 4096 + Arena::ArenaZoneOverhead()));
  arena.reset();
  Mock::VerifyAndClearExpectations(allocator_impl.get());
  EXPECT_CALL(*allocator_impl, Shutdown());
}

#define CONCURRENT_TEST_THREADS 10

size_t concurrent_test_iterations() {
  if (sizeof(void*) < 8) return 1000;
  return 100000;
}

typedef struct {
  gpr_event ev_start;
  RefCountedPtr<Arena> arena;
} concurrent_test_args;

TEST(ArenaTest, NoOp) { SimpleArenaAllocator()->MakeArena(); }

TEST(ArenaTest, ManagedNew) {
  ExecCtx exec_ctx;
  auto arena = SimpleArenaAllocator(1)->MakeArena();
  for (int i = 0; i < 100; i++) {
    arena->ManagedNew<std::unique_ptr<int>>(std::make_unique<int>(i));
  }
}

TEST(ArenaTest, ConcurrentAlloc) {
  concurrent_test_args args;
  gpr_event_init(&args.ev_start);
  args.arena = SimpleArenaAllocator()->MakeArena();

  Thread thds[CONCURRENT_TEST_THREADS];

  for (int i = 0; i < CONCURRENT_TEST_THREADS; i++) {
    thds[i] = Thread(
        "grpc_concurrent_test",
        [](void* arg) {
          concurrent_test_args* a = static_cast<concurrent_test_args*>(arg);
          gpr_event_wait(&a->ev_start, gpr_inf_future(GPR_CLOCK_REALTIME));
          for (size_t i = 0; i < concurrent_test_iterations(); i++) {
            *static_cast<char*>(a->arena->Alloc(1)) = static_cast<char>(i);
          }
        },
        &args);
    thds[i].Start();
  }

  gpr_event_set(&args.ev_start, reinterpret_cast<void*>(1));

  for (auto& th : thds) {
    th.Join();
  }
}

TEST(ArenaTest, ConcurrentManagedNew) {
  concurrent_test_args args;
  gpr_event_init(&args.ev_start);
  args.arena = SimpleArenaAllocator()->MakeArena();

  Thread thds[CONCURRENT_TEST_THREADS];

  for (int i = 0; i < CONCURRENT_TEST_THREADS; i++) {
    thds[i] = Thread(
        "grpc_concurrent_test",
        [](void* arg) {
          concurrent_test_args* a = static_cast<concurrent_test_args*>(arg);
          gpr_event_wait(&a->ev_start, gpr_inf_future(GPR_CLOCK_REALTIME));
          for (size_t i = 0; i < concurrent_test_iterations(); i++) {
            a->arena->ManagedNew<std::unique_ptr<int>>(
                std::make_unique<int>(static_cast<int>(i)));
          }
        },
        &args);
    thds[i].Start();
  }

  gpr_event_set(&args.ev_start, reinterpret_cast<void*>(1));

  for (auto& th : thds) {
    th.Join();
  }
}

template <typename Int>
void Scribble(Int* ints, int n, int offset) {
  for (int i = 0; i < n; i++) {
    ints[i] = static_cast<Int>(i + offset);
  }
}

template <typename Int>
bool IsScribbled(Int* ints, int n, int offset) {
  for (int i = 0; i < n; i++) {
    if (ints[i] != static_cast<Int>(i + offset)) return false;
  }
  return true;
}

TEST(ArenaTest, CreateManyObjects) {
  struct TestObj {
    char a[100];
  };
  auto arena = SimpleArenaAllocator()->MakeArena();
  std::vector<Arena::PoolPtr<TestObj>> objs;
  objs.reserve(1000);
  for (int i = 0; i < 1000; i++) {
    objs.emplace_back(arena->MakePooled<TestObj>());
    Scribble(objs.back()->a, 100, i);
  }
  for (int i = 0; i < 1000; i++) {
    EXPECT_TRUE(IsScribbled(objs[i]->a, 100, i));
  }
}

TEST(ArenaTest, CreateManyObjectsWithDestructors) {
  using TestObj = std::unique_ptr<int>;
  auto arena = SimpleArenaAllocator()->MakeArena();
  std::vector<Arena::PoolPtr<TestObj>> objs;
  objs.reserve(1000);
  for (int i = 0; i < 1000; i++) {
    objs.emplace_back(arena->MakePooled<TestObj>(new int(i)));
  }
}

TEST(ArenaTest, CreatePoolArray) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto p = arena->MakePooledArray<int>(1024);
  EXPECT_TRUE(p.get_deleter().has_freelist());
  p = arena->MakePooledArray<int>(5);
  EXPECT_TRUE(p.get_deleter().has_freelist());
  Scribble(p.get(), 5, 1);
  EXPECT_TRUE(IsScribbled(p.get(), 5, 1));
}

TEST(ArenaTest, ConcurrentMakePooled) {
  concurrent_test_args args;
  gpr_event_init(&args.ev_start);
  args.arena = SimpleArenaAllocator()->MakeArena();

  class BaseClass {
   public:
    virtual ~BaseClass() {}
    virtual int Foo() = 0;
  };

  class Type1 : public BaseClass {
   public:
    int Foo() override { return 1; }
  };

  class Type2 : public BaseClass {
   public:
    int Foo() override { return 2; }
  };

  Thread thds1[CONCURRENT_TEST_THREADS / 2];
  Thread thds2[CONCURRENT_TEST_THREADS / 2];

  for (int i = 0; i < CONCURRENT_TEST_THREADS / 2; i++) {
    thds1[i] = Thread(
        "grpc_concurrent_test",
        [](void* arg) {
          concurrent_test_args* a = static_cast<concurrent_test_args*>(arg);
          gpr_event_wait(&a->ev_start, gpr_inf_future(GPR_CLOCK_REALTIME));
          for (size_t i = 0; i < concurrent_test_iterations(); i++) {
            EXPECT_EQ(a->arena->MakePooled<Type1>()->Foo(), 1);
          }
        },
        &args);
    thds1[i].Start();

    thds2[i] = Thread(
        "grpc_concurrent_test",
        [](void* arg) {
          concurrent_test_args* a = static_cast<concurrent_test_args*>(arg);
          gpr_event_wait(&a->ev_start, gpr_inf_future(GPR_CLOCK_REALTIME));
          for (size_t i = 0; i < concurrent_test_iterations(); i++) {
            EXPECT_EQ(a->arena->MakePooled<Type2>()->Foo(), 2);
          }
        },
        &args);
    thds2[i].Start();
  }

  gpr_event_set(&args.ev_start, reinterpret_cast<void*>(1));

  for (auto& th : thds1) {
    th.Join();
  }
  for (auto& th : thds2) {
    th.Join();
  }
}

struct Foo {
  explicit Foo(int x) : p(std::make_unique<int>(x)) {}
  std::unique_ptr<int> p;
};

template <>
struct ArenaContextType<Foo> {
  static void Destroy(Foo* p) { p->~Foo(); }
};

struct alignas(16) VeryAligned {};

TEST(ArenaTest, FooContext) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  EXPECT_EQ(arena->GetContext<Foo>(), nullptr);
  arena->SetContext(arena->New<Foo>(42));
  ASSERT_NE(arena->GetContext<Foo>(), nullptr);
  EXPECT_EQ(*arena->GetContext<Foo>()->p, 42);
  arena->New<VeryAligned>();
  arena->New<VeryAligned>();
}

class MockArenaFactory : public ArenaFactory {
 public:
  MockArenaFactory()
      : ArenaFactory(
            ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
                "test")) {}
  MOCK_METHOD(RefCountedPtr<Arena>, MakeArena, (), (override));
  MOCK_METHOD(void, FinalizeArena, (Arena * arena), (override));
};

TEST(ArenaTest, FinalizeArenaIsCalled) {
  auto factory = MakeRefCounted<StrictMock<MockArenaFactory>>();
  auto arena = Arena::Create(1, factory);
  EXPECT_CALL(*factory, FinalizeArena(arena.get()));
  arena.reset();
}

TEST(ArenaTest, AccurateBaseByteCount) {
  auto factory = MakeRefCounted<StrictMock<MockArenaFactory>>();
  auto arena = Arena::Create(1, factory);
  EXPECT_CALL(*factory, FinalizeArena(arena.get())).WillOnce([](Arena* a) {
    EXPECT_EQ(a->TotalUsedBytes(),
              Arena::ArenaOverhead() +
                  GPR_ROUND_UP_TO_ALIGNMENT_SIZE(
                      arena_detail::BaseArenaContextTraits::ContextSize()));
  });
  arena.reset();
}

TEST(ArenaTest, AccurateByteCountWithAllocation) {
  auto factory = MakeRefCounted<StrictMock<MockArenaFactory>>();
  auto arena = Arena::Create(1, factory);
  arena->Alloc(1000);
  EXPECT_CALL(*factory, FinalizeArena(arena.get())).WillOnce([](Arena* a) {
    EXPECT_EQ(a->TotalUsedBytes(),
              Arena::ArenaOverhead() +
                  GPR_ROUND_UP_TO_ALIGNMENT_SIZE(
                      arena_detail::BaseArenaContextTraits::ContextSize()) +
                  GPR_ROUND_UP_TO_ALIGNMENT_SIZE(1000));
  });
  arena.reset();
}

//////////////////////////////////////////////////////////////////////////
// ArenaSpsc tests

TEST(ArenaSpscTest, NoOp) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  ArenaSpsc<int> x(arena.get());
}

TEST(ArenaSpscTest, Pop1) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  ArenaSpsc<int> x(arena.get());
  auto y = x.Pop();
  EXPECT_FALSE(y.has_value());
}

TEST(ArenaSpscTest, Push1Pop1SingleThreaded) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  ArenaSpsc<int> x(arena.get());
  x.Push(1);
  auto y = x.Pop();
  ASSERT_TRUE(y.has_value());
  EXPECT_EQ(*y, 1);
  y = x.Pop();
  EXPECT_FALSE(y.has_value());
}

TEST(ArenaSpscTest, Push3Pop3SingleThreaded) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  ArenaSpsc<int> x(arena.get());
  x.Push(1);
  x.Push(2);
  x.Push(3);
  auto y = x.Pop();
  ASSERT_TRUE(y.has_value());
  EXPECT_EQ(*y, 1);
  y = x.Pop();
  ASSERT_TRUE(y.has_value());
  EXPECT_EQ(*y, 2);
  y = x.Pop();
  ASSERT_TRUE(y.has_value());
  EXPECT_EQ(*y, 3);
  y = x.Pop();
  EXPECT_FALSE(y.has_value());
}

TEST(ArenaSpscTest, Push1Pop1TwoThreads) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  ArenaSpsc<int> x(arena.get());
  Thread thd("test", [&x]() { x.Push(1); });
  thd.Start();
  std::optional<int> y;
  while (!y.has_value()) {
    y = x.Pop();
  }
  EXPECT_EQ(*y, 1);
  y = x.Pop();
  EXPECT_FALSE(y.has_value());
  thd.Join();
}

TEST(ArenaSpscTest, Push3Pop3TwoThreads) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  ArenaSpsc<int> x(arena.get());
  Thread thd("test", [&x]() {
    x.Push(1);
    x.Push(2);
    x.Push(3);
  });
  thd.Start();
  std::optional<int> y;
  while (!y.has_value()) {
    y = x.Pop();
  }
  EXPECT_EQ(*y, 1);
  y.reset();
  while (!y.has_value()) {
    y = x.Pop();
  }
  EXPECT_EQ(*y, 2);
  y.reset();
  while (!y.has_value()) {
    y = x.Pop();
  }
  EXPECT_EQ(*y, 3);
  thd.Join();
  y = x.Pop();
  EXPECT_FALSE(y.has_value());
}

TEST(ArenaSpscTest, Push1MPop1MTwoThreads) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  ArenaSpsc<std::unique_ptr<int>> x(arena.get());
  Thread thd("test", [&x]() {
    for (int i = 0; i < 1000000; i++) {
      x.Push(std::make_unique<int>(i));
    }
  });
  thd.Start();
  for (int i = 0; i < 1000000; i++) {
    std::optional<std::unique_ptr<int>> y;
    while (!y.has_value()) {
      y = x.Pop();
    }
    ASSERT_TRUE(*y != nullptr);
    EXPECT_EQ(**y, i);
  }
  thd.Join();
  EXPECT_FALSE(x.Pop().has_value());
}

TEST(ArenaSpscTest, Drain) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  ArenaSpsc<std::unique_ptr<int>> x(arena.get());
  for (int i = 0; i < 1000000; i++) {
    x.Push(std::make_unique<int>(i));
  }
}

}  // namespace grpc_core

int main(int argc, char* argv[]) {
  grpc::testing::TestEnvironment give_me_a_name(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
