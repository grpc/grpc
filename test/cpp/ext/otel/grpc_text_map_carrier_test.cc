//
//
// Copyright 2025 gRPC authors.
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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/call/metadata_batch.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "test/core/promise/test_context.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace internal {
namespace testing {

TEST(GrpcTextMapCarrierTest, SimpleGet) {
  grpc_metadata_batch md;
  auto fn = [](absl::string_view, const grpc_core::Slice&) {
    FAIL() << "Failed to add tracing information in metadata.";
  };
  md.Append("key", grpc_core::Slice::FromCopiedString("value"), fn);
  md.Append("key1", grpc_core::Slice::FromCopiedString("value1"), fn);
  md.Append("key2", grpc_core::Slice::FromCopiedString("value2"), fn);
  GrpcTextMapCarrier carrier(&md);
  auto arena = grpc_core::SimpleArenaAllocator()->MakeArena();
  grpc_core::TestContext<grpc_core::Arena> context(arena.get());
  EXPECT_EQ(carrier.Get("key"), "value");
  EXPECT_EQ(carrier.Get("key1"), "value1");
  EXPECT_EQ(carrier.Get("key2"), "value2");
}

TEST(GrpcTextMapCarrierTest, GrpcTraceBinGet) {
  grpc_metadata_batch md;
  md.Set(grpc_core::GrpcTraceBinMetadata(),
         grpc_core::Slice::FromCopiedString("value"));
  GrpcTextMapCarrier carrier(&md);
  auto arena = grpc_core::SimpleArenaAllocator()->MakeArena();
  grpc_core::TestContext<grpc_core::Arena> context(arena.get());
  absl::string_view escaped_value =
      NoStdStringViewToAbslStringView(carrier.Get("grpc-trace-bin"));
  std::string value;
  ASSERT_TRUE(absl::Base64Unescape(escaped_value, &value));
  EXPECT_EQ(value, "value");
}

TEST(GrpcTextMapCarrierTest, OtherBinaryGet) {
  grpc_metadata_batch md;
  md.Append("random-bin", grpc_core::Slice::FromCopiedString("value"),
            [](absl::string_view, const grpc_core::Slice&) {
              FAIL() << "Failed to add tracing information in metadata.";
            });
  GrpcTextMapCarrier carrier(&md);
  EXPECT_EQ(carrier.Get("random-bin"), "");
}

TEST(GrpcTextMapCarrierTest, SimpleSet) {
  grpc_metadata_batch md;
  GrpcTextMapCarrier carrier(&md);
  carrier.Set("key", "value");
  carrier.Set("key1", "value1");
  carrier.Set("key2", "value2");
  std::string scratch;
  EXPECT_EQ(md.GetStringValue("key", &scratch), "value");
  EXPECT_EQ(md.GetStringValue("key1", &scratch), "value1");
  EXPECT_EQ(md.GetStringValue("key2", &scratch), "value2");
}

TEST(GrpcTextMapCarrierTest, GrpcTraceBinSet) {
  grpc_metadata_batch md;
  GrpcTextMapCarrier carrier(&md);
  carrier.Set("grpc-trace-bin",
              AbslStringViewToNoStdStringView(absl::Base64Escape("value")));
  std::string scratch;
  auto* slice = md.get_pointer(grpc_core::GrpcTraceBinMetadata());
  EXPECT_EQ(slice->as_string_view(), "value");
}

TEST(GrpcTextMapCarrierTest, OtherBinarySet) {
  grpc_metadata_batch md;
  GrpcTextMapCarrier carrier(&md);
  carrier.Set("random-bin", "value");
  std::string scratch;
  EXPECT_FALSE(md.GetStringValue("random-bin", &scratch).has_value());
}

}  // namespace testing
}  // namespace internal
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
