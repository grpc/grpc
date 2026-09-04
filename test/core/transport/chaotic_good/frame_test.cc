// Copyright 2022 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/frame.h"

#include <cstdint>

#include "src/core/call/metadata_batch.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/grpc_check.h"
#include "test/core/transport/chaotic_good/test_frame.h"
#include "gtest/gtest.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_core {
namespace chaotic_good {
namespace {

void AssertRoundTrips(const Frame& input) {
  const auto& input_interface =
      absl::ConvertVariantTo<const FrameInterface&>(input);
  const auto hdr = input_interface.MakeHeader();
  // Frames should always set connection id 0, though the transport may adjust
  // it.
  SliceBuffer output_buffer;
  input_interface.SerializePayload(output_buffer);
  EXPECT_EQ(hdr.payload_length, output_buffer.Length());
  absl::StatusOr<Frame> output = ParseFrame(hdr, std::move(output_buffer));
  GRPC_CHECK_OK(output);
  GRPC_CHECK_EQ(
      absl::ConvertVariantTo<const FrameInterface&>(*output).ToString(),
      input_interface.ToString());
}
FUZZ_TEST(FrameTest, AssertRoundTrips).WithDomains(AnyFrame());

TEST(FrameTest, SettingsFrameRoundTrips) { AssertRoundTrips(SettingsFrame{}); }

TEST(FrameTest, ServerMetadataGrpcFromProtoOutOfRangeStatus) {
  chaotic_good_frame::ServerMetadata proto;
  proto.set_status(4294967295u);
  auto md = ServerMetadataGrpcFromProto(proto);
  ASSERT_TRUE(md.ok()) << md.status();
  auto status = (*md)->get(GrpcStatusMetadata());
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(*status, GRPC_STATUS_UNKNOWN);
}

}  // namespace
}  // namespace chaotic_good
}  // namespace grpc_core
