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

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <iosfwd>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "absl/strings/str_join.h"
#include "gtest/gtest.h"

#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/util/test_config.h"

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
  MemoryAllocator memory_allocator = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
  Arena* a = Arena::Create(GetParam().initial_size, &memory_allocator);
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
  a->Destroy();
}

INSTANTIATE_TEST_SUITE_P(
    AllocTests, AllocTest,
    ::testing::Values(AllocShape{0, {1}}, AllocShape{1, {1}},
                      AllocShape{1, {2}}, AllocShape{1, {3}},
                      AllocShape{1, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
                      AllocShape{6, {1, 2, 3}}));

#define CONCURRENT_TEST_THREADS 10

size_t concurrent_test_iterations() {
  if (sizeof(void*) < 8) return 1000;
  return 100000;
}

typedef struct {
  gpr_event ev_start;
  Arena* arena;
} concurrent_test_args;

class ArenaTest : public ::testing::Test {
 protected:
  MemoryAllocator memory_allocator_ = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
};

TEST_F(ArenaTest, NoOp) {
  ExecCtx exec_ctx;
  Arena::Create(1, &memory_allocator_)->Destroy();
}

TEST_F(ArenaTest, ManagedNew) {
  ExecCtx exec_ctx;
  Arena* arena = Arena::Create(1, &memory_allocator_);
  for (int i = 0; i < 100; i++) {
    arena->ManagedNew<std::unique_ptr<int>>(std::make_unique<int>(i));
  }
  arena->Destroy();
}

TEST_F(ArenaTest, ConcurrentAlloc) {
  concurrent_test_args args;
  gpr_event_init(&args.ev_start);
  args.arena = Arena::Create(1024, &memory_allocator_);

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

  args.arena->Destroy();
}

TEST_F(ArenaTest, ConcurrentManagedNew) {
  concurrent_test_args args;
  gpr_event_init(&args.ev_start);
  args.arena = Arena::Create(1024, &memory_allocator_);

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

  args.arena->Destroy();
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

TEST_F(ArenaTest, PooledObjectsArePooled) {
  struct TestObj {
    char a[100];
  };

  auto arena = MakeScopedArena(1024, &memory_allocator_);
  auto obj = arena->MakePooled<TestObj>();
  Scribble(obj->a, 100, 1);
  EXPECT_TRUE(IsScribbled(obj->a, 100, 1));
  void* p = obj.get();
  obj.reset();
  obj = arena->MakePooled<TestObj>();
  EXPECT_FALSE(IsScribbled(obj->a, 100, 1));
  EXPECT_EQ(p, obj.get());
  Scribble(obj->a, 100, 2);
  EXPECT_TRUE(IsScribbled(obj->a, 100, 2));
}

TEST_F(ArenaTest, CreateManyObjects) {
  struct TestObj {
    char a[100];
  };
  auto arena = MakeScopedArena(1024, &memory_allocator_);
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

TEST_F(ArenaTest, CreateManyObjectsWithDestructors) {
  using TestObj = std::unique_ptr<int>;
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  std::vector<Arena::PoolPtr<TestObj>> objs;
  objs.reserve(1000);
  for (int i = 0; i < 1000; i++) {
    objs.emplace_back(arena->MakePooled<TestObj>(new int(i)));
  }
}

TEST_F(ArenaTest, CreatePoolArray) {
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  auto p = arena->MakePooledArray<int>(1024);
  EXPECT_FALSE(p.get_deleter().has_freelist());
  p = arena->MakePooledArray<int>(5);
  EXPECT_TRUE(p.get_deleter().has_freelist());
  Scribble(p.get(), 5, 1);
  EXPECT_TRUE(IsScribbled(p.get(), 5, 1));
}

TEST_F(ArenaTest, ConcurrentMakePooled) {
  concurrent_test_args args;
  gpr_event_init(&args.ev_start);
  args.arena = Arena::Create(1024, &memory_allocator_);

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

  args.arena->Destroy();
}

}  // namespace grpc_core

int main(int argc, char* argv[]) {
  grpc::testing::TestEnvironment give_me_a_name(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
