// Copyright 2023 gRPC authors.
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

// Test to verify Fuzztest integration

#include <grpc/event_engine/memory_allocator.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <memory>
#include <optional>

#include "absl/random/random.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

void EncodeTimeouts(std::vector<uint32_t> timeouts) {
  absl::BitGen bitgen;
  ScopedTimeCache time_cache;
  time_cache.TestOnlySetNow(Timestamp::ProcessEpoch());
  hpack_encoder_detail::TimeoutCompressorImpl timeout_compressor;
  HPackCompressor compressor;
  HPackParser parser;
  MemoryAllocator memory_allocator = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
  auto arena = MakeScopedArena(1024, &memory_allocator);
  for (size_t i = 0; i < timeouts.size(); i++) {
    SliceBuffer encoded;
    hpack_encoder_detail::Encoder encoder(&compressor, false, encoded);
    timeout_compressor.EncodeWith(
        "grpc-timeout",
        Timestamp::ProcessEpoch() + Duration::Milliseconds(timeouts[i]),
        &encoder);
    grpc_metadata_batch b(arena.get());
    const uint32_t kMetadataSizeLimit = 3u * 1024 * 1024 * 1024;
    parser.BeginFrame(
        &b, kMetadataSizeLimit, kMetadataSizeLimit, HPackParser::Boundary::None,
        HPackParser::Priority::None,
        HPackParser::LogInfo{1, HPackParser::LogInfo::kHeaders, false});
    for (size_t j = 0; j < encoded.Count(); j++) {
      EXPECT_TRUE(parser
                      .Parse(encoded.c_slice_at(j), j == encoded.Count() - 1,
                             bitgen, nullptr)
                      .ok());
    }
    auto parsed = b.get(GrpcTimeoutMetadata());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_GE(*parsed,
              Timestamp::ProcessEpoch() + Duration::Milliseconds(timeouts[i]));
    EXPECT_LE(*parsed, Timestamp::ProcessEpoch() +
                           Duration::Milliseconds(timeouts[i]) * 1.05 +
                           Duration::Milliseconds(1));
  }
}
FUZZ_TEST(MyTestSuite, EncodeTimeouts);

}  // namespace grpc_core
