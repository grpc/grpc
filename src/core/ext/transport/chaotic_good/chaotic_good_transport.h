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

#include "absl/random/random.h"

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

class ChaoticGoodTransport {
 public:
  ChaoticGoodTransport(std::unique_ptr<PromiseEndpoint> control_endpoint,
                       std::unique_ptr<PromiseEndpoint> data_endpoint)
      : control_endpoint_(std::move(control_endpoint)),
        data_endpoint_(std::move(data_endpoint)) {}

  auto WriteFrame(const FrameInterface& frame) {
    auto buffers = frame.Serialize(&encoder_);
    return TryJoin<absl::StatusOr>(
        control_endpoint_->Write(std::move(buffers.control)),
        data_endpoint_->Write(std::move(buffers.data)));
  }

  // Read frame header and payloads for control and data portions of one frame.
  // Resolves to StatusOr<tuple<FrameHeader, BufferPair>>.
  auto ReadFrameBytes() {
    return TrySeq(
        control_endpoint_->ReadSlice(FrameHeader::kFrameHeaderSize),
        [this](Slice read_buffer) {
          auto frame_header =
              FrameHeader::Parse(reinterpret_cast<const uint8_t*>(
                  GRPC_SLICE_START_PTR(read_buffer.c_slice())));
          // Read header and trailers from control endpoint.
          // Read message padding and message from data endpoint.
          return If(
              frame_header.ok(),
              [this, &frame_header] {
                const uint32_t message_padding = std::exchange(
                    last_message_padding_, frame_header->message_padding);
                const uint32_t message_length = frame_header->message_length;
                return Map(
                    TryJoin<absl::StatusOr>(
                        control_endpoint_->Read(frame_header->GetFrameLength()),
                        TrySeq(data_endpoint_->Read(message_padding),
                               [this, message_length]() {
                                 return data_endpoint_->Read(message_length);
                               })),
                    [frame_header = *frame_header](
                        absl::StatusOr<std::tuple<SliceBuffer, SliceBuffer>>
                            buffers)
                        -> absl::StatusOr<std::tuple<FrameHeader, BufferPair>> {
                      if (!buffers.ok()) return buffers.status();
                      return std::tuple<FrameHeader, BufferPair>(
                          frame_header,
                          BufferPair{std::move(std::get<0>(*buffers)),
                                     std::move(std::get<1>(*buffers))});
                    });
              },
              [&frame_header]()
                  -> absl::StatusOr<std::tuple<FrameHeader, BufferPair>> {
                return frame_header.status();
              });
        });
  }

  absl::Status DeserializeFrame(FrameHeader header, BufferPair buffers,
                                Arena* arena, FrameInterface& frame,
                                FrameLimits limits) {
    return frame.Deserialize(&parser_, header, bitgen_, arena,
                             std::move(buffers), limits);
  }

  // Skip a frame, but correctly handle any hpack state updates.
  void SkipFrame(FrameHeader, BufferPair) { Crash("not implemented"); }

 private:
  const std::unique_ptr<PromiseEndpoint> control_endpoint_;
  const std::unique_ptr<PromiseEndpoint> data_endpoint_;
  uint32_t last_message_padding_ = 0;
  HPackCompressor encoder_;
  HPackParser parser_;
  absl::BitGen bitgen_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CHAOTIC_GOOD_TRANSPORT_H
