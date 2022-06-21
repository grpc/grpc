//
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
//

#include "src/core/lib/transport/metadata_allocator.h"

#include <gtest/gtest.h>

#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/promise/test_context.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

class MetadataAllocatorTest : public ::testing::Test {
 protected:
  MetadataAllocatorTest() {}
  ~MetadataAllocatorTest() override {}

 private:
  MemoryAllocator memory_allocator_ =
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test");
  ScopedArenaPtr arena_ = MakeScopedArena(4096, &memory_allocator_);
  MetadataAllocator metadata_allocator_;

  TestContext<Arena> arena_context_{arena_.get()};
  TestContext<MetadataAllocator> metadata_allocator_context_{
      &metadata_allocator_};
};

// Ensure test fixture can init/destroy successfully.
TEST_F(MetadataAllocatorTest, Nothing) {}

// Ensure we can create/destroy some client metadata.
TEST_F(MetadataAllocatorTest, ClientMetadata) {
  GetContext<MetadataAllocator>()->MakeMetadata<ClientMetadata>();
}

// Ensure we can create/destroy some server metadata.
TEST_F(MetadataAllocatorTest, ServerMetadata) {
  GetContext<MetadataAllocator>()->MakeMetadata<ServerMetadata>();
}

// Ensure repeated allocation/deallocations reuse memory.
TEST_F(MetadataAllocatorTest, RepeatedAllocation) {
  void* p =
      GetContext<MetadataAllocator>()->MakeMetadata<ClientMetadata>().get();
  void* q =
      GetContext<MetadataAllocator>()->MakeMetadata<ClientMetadata>().get();
  EXPECT_EQ(p, q);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
};
