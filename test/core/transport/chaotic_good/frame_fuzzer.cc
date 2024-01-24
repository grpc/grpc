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

#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <memory>

#include "absl/random/bit_gen_ref.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/promise/test_context.h"
#include "test/core/transport/chaotic_good/frame_fuzzer.pb.h"

bool squelch = false;

namespace grpc_core {
namespace chaotic_good {

struct DeterministicBitGen : public std::numeric_limits<uint64_t> {
  using result_type = uint64_t;
  uint64_t operator()() { return 42; }
};

FrameLimits FuzzerFrameLimits() { return FrameLimits{1024 * 1024 * 1024, 63}; }

template <typename T>
void AssertRoundTrips(const T& input, FrameType expected_frame_type) {
  HPackCompressor hpack_compressor;
  auto serialized = input.Serialize(&hpack_compressor);
  GPR_ASSERT(serialized.control.Length() >=
             24);  // Initial output buffer size is 64 byte.
  uint8_t header_bytes[24];
  serialized.control.MoveFirstNBytesIntoBuffer(24, header_bytes);
  auto header = FrameHeader::Parse(header_bytes);
  if (!header.ok()) {
    if (!squelch) {
      gpr_log(GPR_ERROR, "Failed to parse header: %s",
              header.status().ToString().c_str());
    }
    Crash("Failed to parse header");
  }
  GPR_ASSERT(header->type == expected_frame_type);
  T output;
  HPackParser hpack_parser;
  DeterministicBitGen bitgen;
  auto deser = output.Deserialize(&hpack_parser, header.value(),
                                  absl::BitGenRef(bitgen), GetContext<Arena>(),
                                  std::move(serialized), FuzzerFrameLimits());
  GPR_ASSERT(deser.ok());
  GPR_ASSERT(output == input);
}

template <typename T>
void FinishParseAndChecks(const FrameHeader& header, BufferPair buffers) {
  T parsed;
  ExecCtx exec_ctx;  // Initialized to get this_cpu() info in global_stat().
  HPackParser hpack_parser;
  DeterministicBitGen bitgen;
  auto deser = parsed.Deserialize(&hpack_parser, header,
                                  absl::BitGenRef(bitgen), GetContext<Arena>(),
                                  std::move(buffers), FuzzerFrameLimits());
  if (!deser.ok()) return;
  gpr_log(GPR_INFO, "Read frame: %s", parsed.ToString().c_str());
  AssertRoundTrips(parsed, header.type);
}

void Run(const frame_fuzzer::Test& test) {
  const uint8_t* control_data =
      reinterpret_cast<const uint8_t*>(test.control().data());
  size_t control_size = test.control().size();
  if (test.control().size() < 24) return;
  auto r = FrameHeader::Parse(control_data);
  if (!r.ok()) return;
  if (test.data().size() != r->message_length) return;
  gpr_log(GPR_INFO, "Read frame header: %s", r->ToString().c_str());
  control_data += 24;
  control_size -= 24;
  MemoryAllocator memory_allocator = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
  auto arena = MakeScopedArena(1024, &memory_allocator);
  TestContext<Arena> ctx(arena.get());
  BufferPair buffers{
      SliceBuffer(Slice::FromCopiedBuffer(control_data, control_size)),
      SliceBuffer(
          Slice::FromCopiedBuffer(test.data().data(), test.data().size())),
  };
  switch (r->type) {
    default:
      return;  // We don't know how to parse this frame type.
    case FrameType::kSettings:
      FinishParseAndChecks<SettingsFrame>(*r, std::move(buffers));
      break;
    case FrameType::kFragment:
      if (test.is_server()) {
        FinishParseAndChecks<ServerFragmentFrame>(*r, std::move(buffers));
      } else {
        FinishParseAndChecks<ClientFragmentFrame>(*r, std::move(buffers));
      }
      break;
    case FrameType::kCancel:
      FinishParseAndChecks<CancelFrame>(*r, std::move(buffers));
      break;
  }
}

}  // namespace chaotic_good
}  // namespace grpc_core

DEFINE_PROTO_FUZZER(const frame_fuzzer::Test& test) {
  grpc_core::chaotic_good::Run(test);
}
