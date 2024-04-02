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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CHAOTIC_GOOD_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CHAOTIC_GOOD_TRANSPORT_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <utility>

#include "absl/random/random.h"

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/promise_endpoint.h"

extern grpc_core::TraceFlag grpc_chaotic_good_trace;

namespace grpc_core {
namespace chaotic_good {

class ChaoticGoodTransport : public RefCounted<ChaoticGoodTransport> {
 public:
  ChaoticGoodTransport(PromiseEndpoint control_endpoint,
                       PromiseEndpoint data_endpoint, HPackParser hpack_parser,
                       HPackCompressor hpack_encoder)
      : control_endpoint_(std::move(control_endpoint)),
        data_endpoint_(std::move(data_endpoint)),
        encoder_(std::move(hpack_encoder)),
        parser_(std::move(hpack_parser)) {
    // Enable RxMemoryAlignment and RPC receive coalescing after the transport
    // setup is complete. At this point all the settings frames should have
    // been read.
    data_endpoint_.EnforceRxMemoryAlignmentAndCoalescing();
  }

  auto WriteFrame(const FrameInterface& frame) {
    auto buffers = frame.Serialize(&encoder_);
    if (grpc_chaotic_good_trace.enabled()) {
      gpr_log(GPR_INFO, "CHAOTIC_GOOD: WriteFrame to:%s %s",
              ResolvedAddressToString(control_endpoint_.GetPeerAddress())
                  .value_or("<<unknown peer address>>")
                  .c_str(),
              frame.ToString().c_str());
    }
    return TryJoin<absl::StatusOr>(
        control_endpoint_.Write(std::move(buffers.control)),
        data_endpoint_.Write(std::move(buffers.data)));
  }

  // Read frame header and payloads for control and data portions of one frame.
  // Resolves to StatusOr<tuple<FrameHeader, BufferPair>>.
  auto ReadFrameBytes() {
    return TrySeq(
        control_endpoint_.ReadSlice(FrameHeader::kFrameHeaderSize),
        [this](Slice read_buffer) {
          auto frame_header =
              FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
                  GRPC_SLICE_START_PTR(read_buffer.c_slice())));
          if (grpc_chaotic_good_trace.enabled()) {
            gpr_log(GPR_INFO, "CHAOTIC_GOOD: ReadHeader from:%s %s",
                    ResolvedAddressToString(control_endpoint_.GetPeerAddress())
                        .value_or("<<unknown peer address>>")
                        .c_str(),
                    frame_header.ok()
                        ? frame_header->ToString().c_str()
                        : frame_header.status().ToString().c_str());
          }
          // Read header and trailers from control endpoint.
          // Read message padding and message from data endpoint.
          return If(
              frame_header.ok(),
              [this, &frame_header] {
                const uint32_t message_padding = frame_header->message_padding;
                const uint32_t message_length = frame_header->message_length;
                return Map(
                    TryJoin<absl::StatusOr>(
                        control_endpoint_.Read(frame_header->GetFrameLength()),
                        data_endpoint_.Read(message_length + message_padding)),
                    [frame_header = *frame_header, message_padding](
                        absl::StatusOr<std::tuple<SliceBuffer, SliceBuffer>>
                            buffers)
                        -> absl::StatusOr<std::tuple<FrameHeader, BufferPair>> {
                      if (!buffers.ok()) return buffers.status();
                      SliceBuffer data_read = std::move(std::get<1>(*buffers));
                      if (message_padding > 0) {
                        data_read.RemoveLastNBytesNoInline(message_padding);
                      }
                      return std::tuple<FrameHeader, BufferPair>(
                          frame_header,
                          BufferPair{std::move(std::get<0>(*buffers)),
                                     std::move(data_read)});
                    });
              },
              [&frame_header]() {
                return [status = frame_header.status()]() mutable
                       -> absl::StatusOr<std::tuple<FrameHeader, BufferPair>> {
                  return std::move(status);
                };
              });
        });
  }

  absl::Status DeserializeFrame(FrameHeader header, BufferPair buffers,
                                Arena* arena, FrameInterface& frame,
                                FrameLimits limits) {
    auto s = frame.Deserialize(&parser_, header, bitgen_, arena,
                               std::move(buffers), limits);
    if (grpc_chaotic_good_trace.enabled()) {
      gpr_log(GPR_INFO, "CHAOTIC_GOOD: DeserializeFrame %s",
              s.ok() ? frame.ToString().c_str() : s.ToString().c_str());
    }
    return s;
  }

 private:
  PromiseEndpoint control_endpoint_;
  PromiseEndpoint data_endpoint_;
  HPackCompressor encoder_;
  HPackParser parser_;
  absl::BitGen bitgen_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CHAOTIC_GOOD_TRANSPORT_H
