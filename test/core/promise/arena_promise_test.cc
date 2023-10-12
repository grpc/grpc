// Copyright 2021 gRPC authors.
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

#include "src/core/lib/promise/arena_promise.h"

#include <array>
#include <memory>

#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/promise/test_context.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

class ArenaPromiseTest : public ::testing::Test {
 protected:
  MemoryAllocator memory_allocator_ = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
};

TEST_F(ArenaPromiseTest, DefaultInitializationYieldsNoValue) {
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TestContext<Arena> context(arena.get());
  ArenaPromise<int> p;
  EXPECT_FALSE(p.has_value());
}

TEST_F(ArenaPromiseTest, AllocatedWorks) {
  ExecCtx exec_ctx;
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TestContext<Arena> context(arena.get());
  int x = 42;
  ArenaPromise<int> p([x] { return Poll<int>(x); });
  EXPECT_TRUE(p.has_value());
  EXPECT_EQ(p(), Poll<int>(42));
  p = ArenaPromise<int>([] { return Poll<int>(43); });
  EXPECT_EQ(p(), Poll<int>(43));
}

TEST_F(ArenaPromiseTest, DestructionWorks) {
  ExecCtx exec_ctx;
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TestContext<Arena> context(arena.get());
  auto x = std::make_shared<int>(42);
  auto p = ArenaPromise<int>([x] { return Poll<int>(*x); });
  ArenaPromise<int> q(std::move(p));
  EXPECT_EQ(q(), Poll<int>(42));
}

TEST_F(ArenaPromiseTest, MoveAssignmentWorks) {
  ExecCtx exec_ctx;
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TestContext<Arena> context(arena.get());
  auto x = std::make_shared<int>(42);
  auto p = ArenaPromise<int>([x] { return Poll<int>(*x); });
  p = ArenaPromise<int>();
}

TEST_F(ArenaPromiseTest, AllocatedUniquePtrWorks) {
  ExecCtx exec_ctx;
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TestContext<Arena> context(arena.get());
  std::array<int, 5> garbage = {0, 1, 2, 3, 4};
  auto freer = [garbage](int* p) { free(p + garbage[0]); };
  using Ptr = std::unique_ptr<int, decltype(freer)>;
  Ptr x(([] {
          int* p = static_cast<decltype(p)>(malloc(sizeof(*p)));
          *p = 42;
          return p;
        })(),
        freer);
  static_assert(sizeof(x) > sizeof(arena_promise_detail::ArgType),
                "This test assumes the unique ptr will go down the allocated "
                "path for ArenaPromise");
  ArenaPromise<Ptr> initial_promise(
      [x = std::move(x)]() mutable { return Poll<Ptr>(std::move(x)); });
  ArenaPromise<Ptr> p(std::move(initial_promise));
  EXPECT_EQ(*p().value(), 42);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment give_me_a_name(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
