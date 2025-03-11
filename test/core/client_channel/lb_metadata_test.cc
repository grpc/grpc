//
// Copyright 2024 gRPC authors.
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

#include "src/core/client_channel/lb_metadata.h"

#include <string>

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/lib/slice/slice.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(LbMetadataMutation, SetsUnknownHeader) {
  grpc_metadata_batch metadata;
  LoadBalancingPolicy::MetadataMutations mutations;
  mutations.Set("key", "value");
  MetadataMutationHandler::Apply(mutations, &metadata);
  std::string buffer;
  EXPECT_EQ(metadata.GetStringValue("key", &buffer), "value");
}

TEST(LbMetadataMutation, SetsTraitHeader) {
  grpc_metadata_batch metadata;
  LoadBalancingPolicy::MetadataMutations mutations;
  mutations.Set("user-agent", "value");
  MetadataMutationHandler::Apply(mutations, &metadata);
  std::string buffer;
  EXPECT_EQ(metadata.GetStringValue("user-agent", &buffer), "value");
}

TEST(LbMetadataMutation, OverwritesExistingHeader) {
  grpc_metadata_batch metadata;
  metadata.Append("key", Slice::FromCopiedString("value1"),
                  [&](absl::string_view error, const Slice& value) {
                    FAIL() << error << " value:" << value.as_string_view();
                  });
  metadata.Append("key", Slice::FromCopiedString("value2"),
                  [&](absl::string_view error, const Slice& value) {
                    FAIL() << error << " value:" << value.as_string_view();
                  });
  std::string buffer;
  EXPECT_EQ(metadata.GetStringValue("key", &buffer), "value1,value2");
  LoadBalancingPolicy::MetadataMutations mutations;
  mutations.Set("key", "value3");
  MetadataMutationHandler::Apply(mutations, &metadata);
  EXPECT_EQ(metadata.GetStringValue("key", &buffer), "value3");
}

TEST(LbMetadataMutation, OverwritesTraitHeader) {
  grpc_metadata_batch metadata;
  metadata.Append("user-agent", Slice::FromCopiedString("value1"),
                  [&](absl::string_view error, const Slice& value) {
                    FAIL() << error << " value:" << value.as_string_view();
                  });
  std::string buffer;
  EXPECT_EQ(metadata.GetStringValue("user-agent", &buffer), "value1");
  LoadBalancingPolicy::MetadataMutations mutations;
  mutations.Set("user-agent", "value2");
  MetadataMutationHandler::Apply(mutations, &metadata);
  EXPECT_EQ(metadata.GetStringValue("user-agent", &buffer), "value2");
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
