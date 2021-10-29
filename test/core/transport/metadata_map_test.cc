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

#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

TEST(MetadataMapTest, Noop) {
  auto arena = MakeScopedArena(1024);
  MetadataMap<>(arena.get());
}

TEST(MetadataMapTest, NoopWithDeadline) {
  auto arena = MakeScopedArena(1024);
  MetadataMap<GrpcTimeoutMetadata>(arena.get());
}

TEST(MetadataMapTest, SimpleOps) {
  auto arena = MakeScopedArena(1024);
  MetadataMap<GrpcTimeoutMetadata> map(arena.get());
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

  void Encode(grpc_mdelem md) {
    output_ +=
        absl::StrCat("LEGACY CALL: key=", StringViewFromSlice(GRPC_MDKEY(md)),
                     " value=", StringViewFromSlice(GRPC_MDVALUE(md)), "\n");
  }

  void Encode(GrpcTimeoutMetadata, grpc_millis deadline) {
    output_ += absl::StrCat("grpc-timeout: deadline=", deadline, "\n");
  }

 private:
  std::string output_;
};

TEST(MetadataMapTest, EmptyEncodeTest) {
  FakeEncoder encoder;
  auto arena = MakeScopedArena(1024);
  MetadataMap<GrpcTimeoutMetadata> map(arena.get());
  map.Encode(&encoder);
  EXPECT_EQ(encoder.output(), "");
}

TEST(MetadataMapTest, TimeoutEncodeTest) {
  FakeEncoder encoder;
  auto arena = MakeScopedArena(1024);
  MetadataMap<GrpcTimeoutMetadata> map(arena.get());
  map.Set(GrpcTimeoutMetadata(), 1234);
  map.Encode(&encoder);
  EXPECT_EQ(encoder.output(), "grpc-timeout: deadline=1234\n");
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
};
