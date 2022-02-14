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

#include <gtest/gtest.h>

#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

static auto* g_memory_allocator = new MemoryAllocator(
    ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));

struct EmptyMetadataMap : public MetadataMap<EmptyMetadataMap> {
  using MetadataMap<EmptyMetadataMap>::MetadataMap;
};

struct TimeoutOnlyMetadataMap
    : public MetadataMap<TimeoutOnlyMetadataMap, GrpcTimeoutMetadata> {
  using MetadataMap<TimeoutOnlyMetadataMap, GrpcTimeoutMetadata>::MetadataMap;
};

struct StreamNetworkStateMetadataMap
    : public MetadataMap<StreamNetworkStateMetadataMap,
                         GrpcStreamNetworkState> {
  using MetadataMap<StreamNetworkStateMetadataMap,
                    GrpcStreamNetworkState>::MetadataMap;
};

TEST(MetadataMapTest, Noop) {
  auto arena = MakeScopedArena(1024, g_memory_allocator);
  EmptyMetadataMap(arena.get());
}

TEST(MetadataMapTest, NoopWithDeadline) {
  auto arena = MakeScopedArena(1024, g_memory_allocator);
  TimeoutOnlyMetadataMap(arena.get());
}

TEST(MetadataMapTest, SimpleOps) {
  auto arena = MakeScopedArena(1024, g_memory_allocator);
  TimeoutOnlyMetadataMap map(arena.get());
  EXPECT_EQ(map.get_pointer(GrpcTimeoutMetadata()), nullptr);
  EXPECT_EQ(map.get(GrpcTimeoutMetadata()), absl::nullopt);
  map.Set(GrpcTimeoutMetadata(), 1234);
  EXPECT_NE(map.get_pointer(GrpcTimeoutMetadata()), nullptr);
  EXPECT_EQ(*map.get_pointer(GrpcTimeoutMetadata()), 1234);
  EXPECT_EQ(map.get(GrpcTimeoutMetadata()), 1234);
  map.Remove(GrpcTimeoutMetadata());
  EXPECT_EQ(map.get_pointer(GrpcTimeoutMetadata()), nullptr);
  EXPECT_EQ(map.get(GrpcTimeoutMetadata()), absl::nullopt);
}

// Target for MetadataMap::Encode.
// Writes down some string representation of what it receives, so we can
// EXPECT_EQ it later.
class FakeEncoder {
 public:
  std::string output() { return output_; }

  void Encode(const Slice& key, const Slice& value) {
    output_ += absl::StrCat("UNKNOWN METADATUM: key=", key.as_string_view(),
                            " value=", value.as_string_view(), "\n");
  }

  void Encode(GrpcTimeoutMetadata, grpc_millis deadline) {
    output_ += absl::StrCat("grpc-timeout: deadline=", deadline, "\n");
  }

 private:
  std::string output_;
};

TEST(MetadataMapTest, EmptyEncodeTest) {
  FakeEncoder encoder;
  auto arena = MakeScopedArena(1024, g_memory_allocator);
  TimeoutOnlyMetadataMap map(arena.get());
  map.Encode(&encoder);
  EXPECT_EQ(encoder.output(), "");
}

TEST(MetadataMapTest, TimeoutEncodeTest) {
  FakeEncoder encoder;
  auto arena = MakeScopedArena(1024, g_memory_allocator);
  TimeoutOnlyMetadataMap map(arena.get());
  map.Set(GrpcTimeoutMetadata(), 1234);
  map.Encode(&encoder);
  EXPECT_EQ(encoder.output(), "grpc-timeout: deadline=1234\n");
}

TEST(MetadataMapTest, NonEncodableTrait) {
  struct EncoderWithNoTraitEncodeFunctions {
    void Encode(const Slice&, const Slice&) {
      abort();  // should not be called
    }
  };
  auto arena = MakeScopedArena(1024, g_memory_allocator);
  StreamNetworkStateMetadataMap map(arena.get());
  map.Set(GrpcStreamNetworkState(), GrpcStreamNetworkState::kNotSentOnWire);
  EXPECT_EQ(map.get(GrpcStreamNetworkState()),
            GrpcStreamNetworkState::kNotSentOnWire);
  EncoderWithNoTraitEncodeFunctions encoder;
  map.Encode(&encoder);
  EXPECT_EQ(map.DebugString(), "GrpcStreamNetworkState: not sent on wire");
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
};
