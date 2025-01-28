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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"

using grpc_core::chaotic_good::FrameHeader;
using HeaderBuffer = std::array<uint8_t, FrameHeader::kFrameHeaderSize>;

void RoundTrips(HeaderBuffer buffer) {
  auto r = FrameHeader::Parse(buffer.data());
  if (!r.ok()) return;
  HeaderBuffer reserialized;
  r->Serialize(reserialized.data());
  EXPECT_EQ(buffer, reserialized);
}
FUZZ_TEST(FrameHeaderTest, RoundTrips);
