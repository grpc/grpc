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
#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/ref_counted_ptr.h"
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

template <typename T>
void AssertRoundTrips(const T& input, FrameType expected_frame_type,
                      uint32_t alignment) {
  BufferPair serialized;
  input.Serialize(SerializeContext{alignment}, &serialized);
  CHECK_GE(serialized.control.Length(), FrameHeader::kFrameHeaderSize);
  uint8_t header_bytes[FrameHeader::kFrameHeaderSize];
  serialized.control.MoveFirstNBytesIntoBuffer(FrameHeader::kFrameHeaderSize,
                                               header_bytes);
  auto header = FrameHeader::Parse(header_bytes);
  if (!header.ok()) {
    if (!squelch) {
      LOG(ERROR) << "Failed to parse header: " << header.status().ToString();
    }
    Crash("Failed to parse header");
  }
  CHECK_EQ(header->type, expected_frame_type);
  T output;
  auto deser = output.Deserialize(
      DeserializeContext{
          alignment,
      },
      header.value(),
      std::move(header->payload_connection_id == 0 ? serialized.control
                                                   : serialized.data));
  CHECK_OK(deser);
  CHECK_EQ(input.ToString(), output.ToString());
}

template <typename T>
void FinishParseAndChecks(const FrameHeader& header, BufferPair buffers,
                          uint32_t alignment) {
  T parsed;
  ExecCtx exec_ctx;  // Initialized to get this_cpu() info in global_stat().
  auto deser = parsed.Deserialize(
      DeserializeContext{alignment}, header,
      std::move(header.payload_connection_id == 0 ? buffers.control
                                                  : buffers.data));
  if (!deser.ok()) return;
  LOG(INFO) << "Read frame: " << parsed.ToString();
  AssertRoundTrips(parsed, header.type, alignment);
}

void Run(const frame_fuzzer::Test& test) {
  if (test.alignment() == 0) return;
  if (test.alignment() > 1024) return;
  const uint8_t* control_data =
      reinterpret_cast<const uint8_t*>(test.control().data());
  size_t control_size = test.control().size();
  if (test.control().size() < FrameHeader::kFrameHeaderSize) return;
  auto r = FrameHeader::Parse(control_data);
  if (!r.ok()) return;
  if (test.data().size() != r->payload_length + r->Padding(test.alignment())) {
    return;
  }
  LOG(INFO) << "Read frame header: " << r->ToString();
  control_data += FrameHeader::kFrameHeaderSize;
  control_size -= FrameHeader::kFrameHeaderSize;
  auto arena = SimpleArenaAllocator()->MakeArena();
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
      FinishParseAndChecks<SettingsFrame>(*r, std::move(buffers),
                                          test.alignment());
      break;
    case FrameType::kClientInitialMetadata:
      FinishParseAndChecks<ClientInitialMetadataFrame>(*r, std::move(buffers),
                                                       test.alignment());
      break;
    case FrameType::kClientEndOfStream:
      FinishParseAndChecks<ClientEndOfStream>(*r, std::move(buffers),
                                              test.alignment());
      break;
    case FrameType::kServerInitialMetadata:
      FinishParseAndChecks<ServerInitialMetadataFrame>(*r, std::move(buffers),
                                                       test.alignment());
      break;
    case FrameType::kServerTrailingMetadata:
      FinishParseAndChecks<ServerTrailingMetadataFrame>(*r, std::move(buffers),
                                                        test.alignment());
      break;
    case FrameType::kMessage:
      FinishParseAndChecks<MessageFrame>(*r, std::move(buffers),
                                         test.alignment());
      break;
    case FrameType::kCancel:
      FinishParseAndChecks<CancelFrame>(*r, std::move(buffers),
                                        test.alignment());
      break;
  }
}

}  // namespace chaotic_good
}  // namespace grpc_core

DEFINE_PROTO_FUZZER(const frame_fuzzer::Test& test) {
  grpc_core::chaotic_good::Run(test);
}
