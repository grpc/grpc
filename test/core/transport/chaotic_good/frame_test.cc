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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace chaotic_good {
namespace {

template <typename T>
void AssertRoundTrips(const T input, FrameType expected_frame_type) {
  HPackCompressor hpack_compressor;
  auto serialized = input.Serialize(&hpack_compressor);
  EXPECT_GE(serialized.Length(), 24);
  uint8_t header_bytes[24];
  serialized.MoveFirstNBytesIntoBuffer(24, header_bytes);
  auto header = FrameHeader::Parse(header_bytes);
  EXPECT_TRUE(header.ok()) << header.status();
  EXPECT_EQ(header->type, expected_frame_type);
  T output;
  HPackParser hpack_parser;
  auto deser = output.Deserialize(&hpack_parser, header.value(), serialized);
  EXPECT_TRUE(deser.ok()) << deser;
  EXPECT_EQ(output, input);
}

TEST(FrameTest, SettingsFrameRoundTrips) {
  AssertRoundTrips(SettingsFrame{}, FrameType::kSettings);
}

}  // namespace
}  // namespace chaotic_good
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int r = RUN_ALL_TESTS();
  return r;
}
