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

#include <grpc/event_engine/memory_allocator.h>
#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <memory>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/status/statusor.h"
#include "fuzztest/fuzztest.h"
#include "src/core/ext/transport/chaotic_good_legacy/frame.h"
#include "src/core/ext/transport/chaotic_good_legacy/frame_header.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/promise/test_context.h"
#include "test/core/transport/chaotic_good_legacy/frame_fuzzer.pb.h"

namespace grpc_core {
namespace chaotic_good_legacy {

struct DeterministicBitGen : public std::numeric_limits<uint64_t> {
  using result_type = uint64_t;
  uint64_t operator()() { return 42; }
};

template <typename T>
void AssertRoundTrips(const T& input, FrameType expected_frame_type) {
  FrameHeader hdr = input.MakeHeader();
  CHECK_EQ(hdr.type, expected_frame_type);
  CHECK_EQ(hdr.payload_connection_id, 0);
  SliceBuffer payload;
  input.SerializePayload(payload);
  CHECK_GE(hdr.payload_length, payload.Length());
  T output;
  auto deser = output.Deserialize(hdr, std::move(payload));
  CHECK_OK(deser);
  CHECK_EQ(input.ToString(), output.ToString());
}

template <typename T>
void FinishParseAndChecks(const FrameHeader& header, SliceBuffer payload) {
  T parsed;
  ExecCtx exec_ctx;  // Initialized to get this_cpu() info in global_stat().
  auto deser = parsed.Deserialize(header, std::move(payload));
  if (!deser.ok()) return;
  AssertRoundTrips(parsed, header.type);
}

void Run(const frame_fuzzer::Test& test) {
  if (test.header().size() != FrameHeader::kFrameHeaderSize) return;
  auto r = FrameHeader::Parse(
      reinterpret_cast<const uint8_t*>(test.header().data()));
  if (!r.ok()) return;
  if (test.payload().size() != r->payload_length) return;
  auto arena = SimpleArenaAllocator()->MakeArena();
  TestContext<Arena> ctx(arena.get());
  SliceBuffer payload(
      Slice::FromCopiedBuffer(test.payload().data(), test.payload().size()));
  switch (r->type) {
    default:
      return;  // We don't know how to parse this frame type.
    case FrameType::kSettings:
      FinishParseAndChecks<SettingsFrame>(*r, std::move(payload));
      break;
    case FrameType::kClientInitialMetadata:
      FinishParseAndChecks<ClientInitialMetadataFrame>(*r, std::move(payload));
      break;
    case FrameType::kClientEndOfStream:
      FinishParseAndChecks<ClientEndOfStream>(*r, std::move(payload));
      break;
    case FrameType::kServerInitialMetadata:
      FinishParseAndChecks<ServerInitialMetadataFrame>(*r, std::move(payload));
      break;
    case FrameType::kServerTrailingMetadata:
      FinishParseAndChecks<ServerTrailingMetadataFrame>(*r, std::move(payload));
      break;
    case FrameType::kMessage:
      FinishParseAndChecks<MessageFrame>(*r, std::move(payload));
      break;
    case FrameType::kCancel:
      FinishParseAndChecks<CancelFrame>(*r, std::move(payload));
      break;
  }
}
FUZZ_TEST(FrameFuzzer, Run);

}  // namespace chaotic_good_legacy
}  // namespace grpc_core
