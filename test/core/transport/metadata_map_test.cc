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

#include <stdlib.h>

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

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

class MetadataMapTest : public ::testing::Test {
 protected:
  MemoryAllocator memory_allocator_ = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
};

TEST_F(MetadataMapTest, Noop) {
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  EmptyMetadataMap(arena.get());
}

TEST_F(MetadataMapTest, NoopWithDeadline) {
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TimeoutOnlyMetadataMap(arena.get());
}

TEST_F(MetadataMapTest, SimpleOps) {
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TimeoutOnlyMetadataMap map(arena.get());
  EXPECT_EQ(map.get_pointer(GrpcTimeoutMetadata()), nullptr);
  EXPECT_EQ(map.get(GrpcTimeoutMetadata()), absl::nullopt);
  map.Set(GrpcTimeoutMetadata(),
          Timestamp::FromMillisecondsAfterProcessEpoch(1234));
  EXPECT_NE(map.get_pointer(GrpcTimeoutMetadata()), nullptr);
  EXPECT_EQ(*map.get_pointer(GrpcTimeoutMetadata()),
            Timestamp::FromMillisecondsAfterProcessEpoch(1234));
  EXPECT_EQ(map.get(GrpcTimeoutMetadata()),
            Timestamp::FromMillisecondsAfterProcessEpoch(1234));
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

  void Encode(GrpcTimeoutMetadata, Timestamp deadline) {
    output_ += absl::StrCat("grpc-timeout: deadline=",
                            deadline.milliseconds_after_process_epoch(), "\n");
  }

 private:
  std::string output_;
};

TEST_F(MetadataMapTest, EmptyEncodeTest) {
  FakeEncoder encoder;
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TimeoutOnlyMetadataMap map(arena.get());
  map.Encode(&encoder);
  EXPECT_EQ(encoder.output(), "");
}

TEST_F(MetadataMapTest, TimeoutEncodeTest) {
  FakeEncoder encoder;
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  TimeoutOnlyMetadataMap map(arena.get());
  map.Set(GrpcTimeoutMetadata(),
          Timestamp::FromMillisecondsAfterProcessEpoch(1234));
  map.Encode(&encoder);
  EXPECT_EQ(encoder.output(), "grpc-timeout: deadline=1234\n");
}

TEST_F(MetadataMapTest, NonEncodableTrait) {
  struct EncoderWithNoTraitEncodeFunctions {
    void Encode(const Slice&, const Slice&) {
      abort();  // should not be called
    }
  };
  auto arena = MakeScopedArena(1024, &memory_allocator_);
  StreamNetworkStateMetadataMap map(arena.get());
  map.Set(GrpcStreamNetworkState(), GrpcStreamNetworkState::kNotSentOnWire);
  EXPECT_EQ(map.get(GrpcStreamNetworkState()),
            GrpcStreamNetworkState::kNotSentOnWire);
  EncoderWithNoTraitEncodeFunctions encoder;
  map.Encode(&encoder);
  EXPECT_EQ(map.DebugString(), "GrpcStreamNetworkState: not sent on wire");
}

TEST(DebugStringBuilderTest, AddOne) {
  metadata_detail::DebugStringBuilder b;
  b.Add("a", "b");
  EXPECT_EQ(b.TakeOutput(), "a: b");
}

TEST(DebugStringBuilderTest, AddTwo) {
  metadata_detail::DebugStringBuilder b;
  b.Add("a", "b");
  b.Add("c", "d");
  EXPECT_EQ(b.TakeOutput(), "a: b, c: d");
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
};
