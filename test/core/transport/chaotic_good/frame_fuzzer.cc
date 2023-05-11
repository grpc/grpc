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

#include <memory>

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
#include "test/core/promise/test_context.h"

bool squelch = false;

namespace grpc_core {
namespace chaotic_good {

template <typename T>
void AssertRoundTrips(const T& input, FrameType expected_frame_type) {
  HPackCompressor hpack_compressor;
  auto serialized = input.Serialize(&hpack_compressor);
  GPR_ASSERT(serialized.Length() >=
             24);  // Initial output buffer size is 64 byte.
  uint8_t header_bytes[24];
  serialized.MoveFirstNBytesIntoBuffer(24, header_bytes);
  auto header = FrameHeader::Parse(header_bytes);
  GPR_ASSERT(header.ok());
  GPR_ASSERT(header->type == expected_frame_type);
  T output;
  HPackParser hpack_parser;
  auto deser = output.Deserialize(&hpack_parser, header.value(), serialized);
  GPR_ASSERT(deser.ok());
  GPR_ASSERT(output == input);
}

template <typename T>
void FinishParseAndChecks(const FrameHeader& header, const uint8_t* data,
                          size_t size) {
  T parsed;
  ExecCtx exec_ctx;  // Initialized to get this_cpu() info in global_stat().
  HPackParser hpack_parser;
  SliceBuffer serialized;
  serialized.Append(Slice::FromCopiedBuffer(data, size));
  auto deser = parsed.Deserialize(&hpack_parser, header, serialized);
  if (!deser.ok()) return;
  AssertRoundTrips(parsed, header.type);
}

int Run(const uint8_t* data, size_t size) {
  if (size < 1) return 0;
  const bool is_server = (data[0] & 1) != 0;
  size--;
  data++;
  if (size < 24) return 0;
  auto r = FrameHeader::Parse(data);
  if (!r.ok()) return 0;
  size -= 24;
  data += 24;
  MemoryAllocator memory_allocator = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
  auto arena = MakeScopedArena(1024, &memory_allocator);
  TestContext<Arena> ctx(arena.get());
  switch (r->type) {
    default:
      return 0;  // We don't know how to parse this frame type.
    case FrameType::kSettings:
      FinishParseAndChecks<SettingsFrame>(*r, data, size);
      break;
    case FrameType::kFragment:
      if (is_server) {
        FinishParseAndChecks<ServerFragmentFrame>(*r, data, size);
      } else {
        FinishParseAndChecks<ClientFragmentFrame>(*r, data, size);
      }
      break;
    case FrameType::kCancel:
      FinishParseAndChecks<CancelFrame>(*r, data, size);
      break;
  }
  return 0;
}

}  // namespace chaotic_good
}  // namespace grpc_core

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  return grpc_core::chaotic_good::Run(data, size);
}
