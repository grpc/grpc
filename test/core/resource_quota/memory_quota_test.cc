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

#include "src/core/lib/resource_quota/memory_quota.h"

#include <gtest/gtest.h>

#include "absl/synchronization/notification.h"

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "test/core/resource_quota/call_checker.h"

namespace grpc_core {
namespace testing {

//
// Helpers
//

template <size_t kSize>
struct Sized {
  char blah[kSize];
  virtual ~Sized() {}
};

//
// MemoryRequestTest
//

TEST(MemoryRequestTest, ConversionFromSize) {
  MemoryRequest request = 3;
  EXPECT_EQ(request.min(), 3);
  EXPECT_EQ(request.max(), 3);
}

TEST(MemoryRequestTest, MinMax) {
  MemoryRequest request(3, 7);
  EXPECT_EQ(request.min(), 3);
  EXPECT_EQ(request.max(), 7);
}

//
// MemoryQuotaTest
//

TEST(MemoryQuotaTest, NoOp) { MemoryQuota(); }

TEST(MemoryQuotaTest, CreateAllocatorNoOp) {
  MemoryQuota memory_quota;
  auto memory_allocator = memory_quota.CreateMemoryAllocator();
}

TEST(MemoryQuotaTest, CreateObjectFromAllocator) {
  MemoryQuota memory_quota;
  auto memory_allocator = memory_quota.CreateMemoryAllocator();
  auto object = memory_allocator.MakeUnique<Sized<4096>>();
}

TEST(MemoryQuotaTest, CreateSomeObjectsAndExpectReclamation) {
  ExecCtx exec_ctx;

  MemoryQuota memory_quota;
  memory_quota.SetSize(4096);
  auto memory_allocator = memory_quota.CreateMemoryOwner();
  auto object = memory_allocator.allocator()->MakeUnique<Sized<2048>>();

  auto checker1 = CallChecker::Make();
  memory_allocator.PostReclaimer(
      ReclamationPass::kDestructive,
      [&object, checker1](absl::optional<ReclamationSweep> sweep) {
        checker1->Called();
        EXPECT_TRUE(sweep.has_value());
        object.reset();
      });
  auto object2 = memory_allocator.allocator()->MakeUnique<Sized<2048>>();
  exec_ctx.Flush();
  EXPECT_EQ(object.get(), nullptr);

  auto checker2 = CallChecker::Make();
  memory_allocator.PostReclaimer(
      ReclamationPass::kDestructive,
      [&object2, checker2](absl::optional<ReclamationSweep> sweep) {
        checker2->Called();
        EXPECT_TRUE(sweep.has_value());
        object2.reset();
      });
  auto object3 = memory_allocator.allocator()->MakeUnique<Sized<2048>>();
  exec_ctx.Flush();
  EXPECT_EQ(object2.get(), nullptr);
}

TEST(MemoryQuotaTest, BasicRebind) {
  ExecCtx exec_ctx;

  MemoryQuota memory_quota;
  memory_quota.SetSize(4096);
  MemoryQuota memory_quota2;
  memory_quota2.SetSize(4096);

  auto memory_allocator = memory_quota2.CreateMemoryOwner();
  auto object = memory_allocator.allocator()->MakeUnique<Sized<2048>>();

  memory_allocator.Rebind(&memory_quota);
  auto memory_allocator2 = memory_quota2.CreateMemoryOwner();

  auto checker1 = CallChecker::Make();
  memory_allocator2.PostReclaimer(
      ReclamationPass::kDestructive,
      [checker1](absl::optional<ReclamationSweep> sweep) {
        checker1->Called();
        // Taken memory should be reassigned to
        // memory_quota, so this should be cancelled
        EXPECT_FALSE(sweep.has_value());
      });

  auto checker2 = CallChecker::Make();
  memory_allocator.PostReclaimer(
      ReclamationPass::kDestructive,
      [&object, checker2](absl::optional<ReclamationSweep> sweep) {
        checker2->Called();
        EXPECT_TRUE(sweep.has_value());
        // The new memory allocator should reclaim
        // the object allocated against the previous
        // quota because that's now part of this
        // quota.
        object.reset();
      });

  auto object2 = memory_allocator.allocator()->MakeUnique<Sized<2048>>();
  exec_ctx.Flush();
  EXPECT_EQ(object.get(), nullptr);
}

TEST(MemoryQuotaTest, ReserveRangeNoPressure) {
  MemoryQuota memory_quota;
  auto memory_allocator = memory_quota.CreateMemoryAllocator();
  size_t total = 0;
  for (int i = 0; i < 10000; i++) {
    auto n = memory_allocator.Reserve(MemoryRequest(100, 40000));
    EXPECT_EQ(n, 40000);
    total += n;
  }
  memory_allocator.Release(total);
}

TEST(MemoryQuotaTest, MakeSlice) {
  MemoryQuota memory_quota;
  auto memory_allocator = memory_quota.CreateMemoryAllocator();
  std::vector<grpc_slice> slices;
  for (int i = 1; i < 1000; i++) {
    int min = i;
    int max = 10 * i - 9;
    slices.push_back(memory_allocator.MakeSlice(MemoryRequest(min, max)));
  }
  for (grpc_slice slice : slices) {
    grpc_slice_unref_internal(slice);
  }
}

TEST(MemoryQuotaTest, ContainerAllocator) {
  MemoryQuota memory_quota;
  auto memory_allocator = memory_quota.CreateMemoryAllocator();
  Vector<int> vec(&memory_allocator);
  for (int i = 0; i < 100000; i++) {
    vec.push_back(i);
  }
}

}  // namespace testing
}  // namespace grpc_core

// Hook needed to run ExecCtx outside of iomgr.
void grpc_set_default_iomgr_platform() {}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  gpr_log_verbosity_init();
  return RUN_ALL_TESTS();
}
