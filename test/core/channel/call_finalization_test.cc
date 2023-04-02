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

#include "src/core/lib/channel/call_finalization.h"

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/promise/test_context.h"

namespace grpc_core {

TEST(CallFinalizationTest, Works) {
  auto memory_allocator = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
  auto arena = MakeScopedArena(1024, &memory_allocator);
  std::string evidence;
  TestContext<Arena> context(arena.get());
  CallFinalization finalization;
  auto p = std::make_shared<int>(42);
  finalization.Add([&evidence, p](const grpc_call_final_info* final_info) {
    evidence += absl::StrCat("FIRST", final_info->error_string, *p, "\n");
  });
  finalization.Add([&evidence, p](const grpc_call_final_info* final_info) {
    evidence += absl::StrCat("SECOND", final_info->error_string, *p, "\n");
  });
  grpc_call_final_info final_info{};
  final_info.error_string = "123";
  finalization.Run(&final_info);
  EXPECT_EQ(evidence, "SECOND12342\nFIRST12342\n");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
