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

#include "src/core/ext/transport/chaotic_good/frame_header.h"

#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace chaotic_good {
namespace {

std::vector<uint8_t> Serialize(FrameHeader h) {
  uint8_t buffer[24];
  h.Serialize(buffer);
  return std::vector<uint8_t>(buffer, buffer + 24);
}

absl::StatusOr<FrameHeader> Deserialize(std::vector<uint8_t> data) {
  if (data.size() != 24) return absl::InvalidArgumentError("bad length");
  return FrameHeader::Parse(data.data());
}

TEST(FrameHeaderTest, SimpleSerialize) {
  EXPECT_EQ(Serialize(FrameHeader{FrameType::kCancel, BitSet<3>::FromInt(0),
                                  0x01020304, 0x05060708, 0x090a0b0c,
                                  0x00000034, 0x0d0e0f10}),
            std::vector<uint8_t>({
                0x81, 0,    0,    0,     // type, flags
                0x04, 0x03, 0x02, 0x01,  // stream_id
                0x08, 0x07, 0x06, 0x05,  // header_length
                0x0c, 0x0b, 0x0a, 0x09,  // message_length
                0x34, 0x00, 0x00, 0x00,  // mesage_padding
                0x10, 0x0f, 0x0e, 0x0d   // trailer_length
            }));
}

TEST(FrameHeaderTest, SimpleDeserialize) {
  EXPECT_EQ(Deserialize(std::vector<uint8_t>({
                0x81, 0,    0,    0,     // type, flags
                0x04, 0x03, 0x02, 0x01,  // stream_id
                0x08, 0x07, 0x06, 0x05,  // header_length
                0x0c, 0x0b, 0x0a, 0x09,  // message_length
                0x34, 0x00, 0x00, 0x00,  // mesage_padding
                0x10, 0x0f, 0x0e, 0x0d   // trailer_length
            })),
            absl::StatusOr<FrameHeader>(FrameHeader{
                FrameType::kCancel, BitSet<3>::FromInt(0), 0x01020304,
                0x05060708, 0x090a0b0c, 0x00000034, 0x0d0e0f10}));
  EXPECT_EQ(Deserialize(std::vector<uint8_t>({
                            0x81, 88,   88,   88,    // type, flags
                            0x04, 0x03, 0x02, 0x01,  // stream_id
                            0x08, 0x07, 0x06, 0x05,  // header_length
                            0x0c, 0x0b, 0x0a, 0x09,  // message_length
                            0x34, 0x00, 0x00, 0x00,  // mesage_padding
                            0x10, 0x0f, 0x0e, 0x0d   // trailer_length
                        }))
                .status(),
            absl::InvalidArgumentError("Invalid flags"));
}

TEST(FrameHeaderTest, GetFrameLength) {
  EXPECT_EQ(
      (FrameHeader{FrameType::kFragment, BitSet<3>::FromInt(5), 1, 0, 0, 0, 0})
          .GetFrameLength(),
      0);
  EXPECT_EQ(
      (FrameHeader{FrameType::kFragment, BitSet<3>::FromInt(5), 1, 14, 0, 0, 0})
          .GetFrameLength(),
      14);
  EXPECT_EQ((FrameHeader{FrameType::kFragment, BitSet<3>::FromInt(5), 1, 0, 14,
                         50, 0})
                .GetFrameLength(),
            0);
  EXPECT_EQ(
      (FrameHeader{FrameType::kFragment, BitSet<3>::FromInt(5), 1, 0, 0, 0, 14})
          .GetFrameLength(),
      14);
}

}  // namespace
}  // namespace chaotic_good
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
