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

#include "src/core/ext/transport/chaotic_good/serialize_little_endian.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cstdint>

#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace grpc_core::chaotic_good {

void RoundTrips32(uint32_t x) {
  uint8_t buffer[4];
  WriteLittleEndianUint32(x, buffer);
  uint32_t y = ReadLittleEndianUint32(buffer);
  EXPECT_EQ(x, y);
}
FUZZ_TEST(SerializeLittleEndianTest, RoundTrips32);

void RoundTrips64(uint64_t x) {
  uint8_t buffer[8];
  WriteLittleEndianUint64(x, buffer);
  uint64_t y = ReadLittleEndianUint64(buffer);
  EXPECT_EQ(x, y);
}
FUZZ_TEST(SerializeLittleEndianTest, RoundTrips64);

}  // namespace grpc_core::chaotic_good
