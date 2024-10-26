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

#include "absl/log/check.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"

namespace grpc_core {
namespace chaotic_good {
namespace {

template <typename T>
void AssertRoundTrips(const T& input, FrameType expected_frame_type,
                      uint32_t alignment) {
  SerializeContext ser_ctx{alignment};
  BufferPair output_buffer;
  input.Serialize(ser_ctx, &output_buffer);
  EXPECT_GE(output_buffer.control.Length(), FrameHeader::kFrameHeaderSize);
  uint8_t header_bytes[FrameHeader::kFrameHeaderSize];
  output_buffer.control.MoveFirstNBytesIntoBuffer(FrameHeader::kFrameHeaderSize,
                                                  header_bytes);
  auto header = FrameHeader::Parse(header_bytes);
  if (!header.ok()) {
    LOG(FATAL) << "Failed to parse header: " << header.status();
  }
  EXPECT_EQ(header->type, expected_frame_type);
  if (header->type == FrameType::kSettings) {
    EXPECT_EQ(header->payload_connection_id, 0);
  }
  SliceBuffer payload;
  if (header->payload_connection_id == 0) {
    payload = std::move(output_buffer.control);
    EXPECT_EQ(output_buffer.data.Length(), 0);
  } else {
    output_buffer.data.MoveFirstNBytesIntoSliceBuffer(header->payload_length,
                                                      payload);
    EXPECT_EQ(output_buffer.control.Length(), 0);
    EXPECT_EQ(output_buffer.data.Length(), header->Padding(alignment));
  }
  T output;
  DeserializeContext deser_ctx{alignment};
  auto deser =
      output.Deserialize(deser_ctx, header.value(), std::move(payload));
  CHECK_OK(deser);
  CHECK_EQ(output.ToString(), input.ToString());
}

TEST(FrameTest, SettingsFrameRoundTrips) {
  AssertRoundTrips(SettingsFrame{}, FrameType::kSettings, 64);
  AssertRoundTrips(SettingsFrame{}, FrameType::kSettings, 128);
}

}  // namespace
}  // namespace chaotic_good
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int r = RUN_ALL_TESTS();
  return r;
}
