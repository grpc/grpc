// Copyright 2026 gRPC authors.
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

#include "src/core/lib/slice/byte_source.h"

#include <array>
#include <cstdint>

#include "gtest/gtest.h"

namespace grpc_core {
namespace {

TEST(ByteSourceTest, ReadU8AndSpan) {
  const std::array<uint8_t, 5> input = {1, 2, 3, 4, 5};
  ByteSource src(input.data(), input.size());

  EXPECT_EQ(src.ReadU8(), 1u);
  auto span = src.ReadSpan(3);
  ASSERT_TRUE(span.has_value());
  EXPECT_EQ(span->size(), 3u);
  EXPECT_EQ((*span)[0], 2u);
  EXPECT_EQ((*span)[1], 3u);
  EXPECT_EQ((*span)[2], 4u);
  EXPECT_EQ(src.remaining(), 1u);
  EXPECT_EQ(src.ReadU8(), 5u);
  EXPECT_FALSE(src.ReadU8().has_value());
}

TEST(ByteSinkTest, WriteU8AndU32BE) {
  std::array<uint8_t, 5> output = {0, 0, 0, 0, 0};
  ByteSink sink(output.data(), output.size());

  EXPECT_TRUE(sink.WriteU8(0x12));
  EXPECT_TRUE(sink.WriteU32BE(0x01020304));
  EXPECT_EQ(output[0], 0x12u);
  EXPECT_EQ(output[1], 0x01u);
  EXPECT_EQ(output[2], 0x02u);
  EXPECT_EQ(output[3], 0x03u);
  EXPECT_EQ(output[4], 0x04u);
  EXPECT_TRUE(sink.empty());
}

}  // namespace
}  // namespace grpc_core
