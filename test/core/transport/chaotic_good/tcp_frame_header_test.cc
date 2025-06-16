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

#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chaotic_good/tcp_frame_transport.h"

namespace grpc_core {
namespace chaotic_good {
namespace {

std::vector<uint8_t> Serialize(TcpFrameHeader h) {
  uint8_t buffer[TcpFrameHeader::kFrameHeaderSize];
  h.Serialize(buffer);
  return std::vector<uint8_t>(buffer,
                              buffer + TcpFrameHeader::kFrameHeaderSize);
}

absl::StatusOr<TcpFrameHeader> Deserialize(std::vector<uint8_t> data) {
  if (data.size() != TcpFrameHeader::kFrameHeaderSize) {
    return absl::InvalidArgumentError("bad length");
  }
  return TcpFrameHeader::Parse(data.data());
}

TEST(TcpFrameHeader, SimpleSerialize) {
  EXPECT_EQ(Serialize(TcpFrameHeader{
                {FrameType::kCancel, 0x01020304, 0x05060708}, 1}),
            std::vector<uint8_t>({
                0xff, 0x01,                          // type
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // payload_tag
                0x04, 0x03, 0x02, 0x01,              // stream_id
                0x08, 0x07, 0x06, 0x05,              // payload_length
            }));
}

TEST(TcpFrameHeader, SimpleDeserialize) {
  EXPECT_EQ(Deserialize(std::vector<uint8_t>({
                0xff, 0x01,                          // type
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // payload_tag
                0x04, 0x03, 0x02, 0x01,              // stream_id
                0x08, 0x07, 0x06, 0x05,              // payload_length
            })),
            absl::StatusOr<TcpFrameHeader>(TcpFrameHeader{
                {FrameType::kCancel, 0x01020304, 0x05060708}, 1}));
}

}  // namespace
}  // namespace chaotic_good
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
