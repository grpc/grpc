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

#include <stdint.h>

#include <memory>

#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/status.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

struct TestMap : public MetadataMap<TestMap, GrpcStatusMetadata> {
  using MetadataMap<TestMap, GrpcStatusMetadata>::MetadataMap;
};

TEST(PromiseTest, SucceedAndThenFail) {
  MemoryAllocator memory_allocator = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
  auto arena = MakeScopedArena(1024, &memory_allocator);
  Poll<TestMap> r = TrySeq(
      [&arena] {
        TestMap m(arena.get());
        m.Set(GrpcStatusMetadata(), GRPC_STATUS_OK);
        return m;
      },
      [&arena]() {
        TestMap m(arena.get());
        m.Set(GrpcStatusMetadata(), GRPC_STATUS_UNAVAILABLE);
        return m;
      })();
  EXPECT_EQ(r.value().get(GrpcStatusMetadata()), GRPC_STATUS_UNAVAILABLE);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
