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

#include "src/core/call/call_arena_allocator.h"

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

namespace grpc_core {

TEST(CallArenaAllocatorTest, SettlesEmpty) {
  auto allocator = MakeRefCounted<CallArenaAllocator>(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
          "test-allocator"),
      1);
  for (int i = 0; i < 10000; i++) {
    allocator->MakeArena();
  }
  auto estimate = allocator->CallSizeEstimate();
  for (int i = 0; i < 10000; i++) {
    allocator->MakeArena();
  }
  EXPECT_EQ(estimate, allocator->CallSizeEstimate());
}

TEST(CallArenaAllocatorTest, SettlesWithAllocation) {
  auto allocator = MakeRefCounted<CallArenaAllocator>(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
          "test-allocator"),
      1);
  for (int i = 0; i < 10000; i++) {
    allocator->MakeArena()->Alloc(10000);
  }
  auto estimate = allocator->CallSizeEstimate();
  for (int i = 0; i < 10000; i++) {
    allocator->MakeArena()->Alloc(10000);
  }
  LOG(INFO) << estimate;
}

}  // namespace grpc_core

int main(int argc, char* argv[]) {
  grpc::testing::TestEnvironment give_me_a_name(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
