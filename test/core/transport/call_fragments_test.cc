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

#include "src/core/lib/transport/call_fragments.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/promise/test_context.h"
#include "test/core/util/test_config.h"

using testing::Each;

namespace grpc_core {
namespace testing {

class CallFragmentsTest : public ::testing::Test {
 protected:
  CallFragmentsTest() {}
  ~CallFragmentsTest() override {}

 private:
  MemoryAllocator memory_allocator_ =
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test");
  ScopedArenaPtr arena_ = MakeScopedArena(4096, &memory_allocator_);
  FragmentAllocator fragment_allocator_;

  TestContext<Arena> arena_context_{arena_.get()};
  TestContext<FragmentAllocator> fragment_allocator_context_{
      &fragment_allocator_};
};

// Ensure test fixture can init/destroy successfully.
TEST_F(CallFragmentsTest, Nothing) {}

// Ensure we can create/destroy some client metadata.
TEST_F(CallFragmentsTest, ClientMetadata) {
  GetContext<FragmentAllocator>()->MakeClientMetadata();
}

// Ensure we can create/destroy some server metadata.
TEST_F(CallFragmentsTest, ServerMetadata) {
  GetContext<FragmentAllocator>()->MakeServerMetadata();
}

// Ensure repeated allocation/deallocations reuse memory.
TEST_F(CallFragmentsTest, RepeatedAllocationsReuseMemory) {
  void* p = GetContext<FragmentAllocator>()->MakeClientMetadata().get();
  void* q = GetContext<FragmentAllocator>()->MakeClientMetadata().get();
  EXPECT_EQ(p, q);
}

// Ensure repeated allocation reinitializes.
TEST_F(CallFragmentsTest, RepeatedAllocationsReinitialize) {
  std::vector<void*> addresses;
  for (int i = 0; i < 4; i++) {
    ClientMetadataHandle metadata =
        GetContext<FragmentAllocator>()->MakeClientMetadata();
    EXPECT_EQ(metadata->get_pointer(HttpPathMetadata()), nullptr);
    metadata->Set(HttpPathMetadata(), Slice::FromCopiedString("/"));
    EXPECT_EQ(metadata->get_pointer(HttpPathMetadata())->as_string_view(), "/");
    addresses.push_back(metadata.get());
  }
  EXPECT_THAT(addresses, Each(addresses[0]));
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
};
