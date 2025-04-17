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

#include "src/core/ext/transport/chaotic_good_legacy/frame.h"

#include <cstdint>

#include "absl/log/check.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"

namespace grpc_core {
namespace chaotic_good_legacy {
namespace {

template <typename T>
void AssertRoundTrips(const T& input, FrameType expected_frame_type) {
  const auto hdr = input.MakeHeader();
  EXPECT_EQ(hdr.type, expected_frame_type);
  // Frames should always set connection id 0, though the transport may adjust
  // it.
  EXPECT_EQ(hdr.payload_connection_id, 0);
  SliceBuffer output_buffer;
  input.SerializePayload(output_buffer);
  EXPECT_EQ(hdr.payload_length, output_buffer.Length());
  T output;
  auto deser = output.Deserialize(hdr, std::move(output_buffer));
  CHECK_OK(deser);
  CHECK_EQ(output.ToString(), input.ToString());
}

TEST(FrameTest, SettingsFrameRoundTrips) {
  AssertRoundTrips(SettingsFrame{}, FrameType::kSettings);
}

}  // namespace
}  // namespace chaotic_good_legacy
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int r = RUN_ALL_TESTS();
  return r;
}
