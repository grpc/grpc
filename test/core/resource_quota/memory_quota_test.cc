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
  EXPECT_EQ(request.block_size(), 1);
}

TEST(MemoryRequestTest, MinMax) {
  MemoryRequest request(3, 7);
  EXPECT_EQ(request.min(), 3);
  EXPECT_EQ(request.max(), 7);
  EXPECT_EQ(request.block_size(), 1);
}

TEST(MemoryRequestTest, MinMaxWithBlockSize) {
  auto request = MemoryRequest(361, 2099).WithBlockSize(1024);
  EXPECT_EQ(request.min(), 1024);
  EXPECT_EQ(request.max(), 2048);
  EXPECT_EQ(request.block_size(), 1024);
}

//
// MemoryQuotaTest
//

TEST(MemoryQuotaTest, NoOp) {
  RefCountedPtr<MemoryQuota> memory_quota = MakeRefCounted<MemoryQuota>();
}

TEST(MemoryQuotaTest, CreateAllocatorNoOp) {
  RefCountedPtr<MemoryQuota> memory_quota = MakeRefCounted<MemoryQuota>();
  auto memory_allocator = memory_quota->MakeMemoryAllocator();
}

TEST(MemoryQuotaTest, CreateObjectFromAllocator) {
  RefCountedPtr<MemoryQuota> memory_quota = MakeRefCounted<MemoryQuota>();
  auto memory_allocator = memory_quota->MakeMemoryAllocator();
  auto object = memory_allocator->MakeUnique<Sized<4096>>();
}

TEST(MemoryQuotaTest, CreateSomeObjectsAndExpectReclamation) {
  RefCountedPtr<MemoryQuota> memory_quota = MakeRefCounted<MemoryQuota>();
  memory_quota->SetSize(4096);
  auto memory_allocator = memory_quota->MakeMemoryAllocator();
  auto object = memory_allocator->MakeUnique<Sized<2048>>();
  absl::Notification notification;
  memory_allocator->PostReclaimer(ReclamationPass::kDestructive,
                                  [&notification, &object](ReclamationSweep) {
                                    object.reset();
                                    notification.Notify();
                                  });
  auto object2 = memory_allocator->MakeUnique<Sized<2048>>();
  notification.WaitForNotification();
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
