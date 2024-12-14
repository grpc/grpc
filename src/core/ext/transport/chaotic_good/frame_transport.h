// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_TRANSPORT_H

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/match_promise.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/promise.h"

namespace grpc_core {
namespace chaotic_good {

// One received frame: the header, and the serialized bytes of the payload.
// The payload may not yet be received into memory, so the accessor for that
// returns a promise that will need to be resolved prior to inspecting the
// bytes.
// In this way we can pull bytes from various different data connections and
// read them in any order, but still have a trivial reassembly in the receiving
// call promise.
class IncomingFrame {
 public:
  IncomingFrame(FrameHeader header, absl::StatusOr<SliceBuffer> payload)
      : header_(header), payload_(std::move(payload)) {}
  IncomingFrame(FrameHeader header,
                Promise<absl::StatusOr<SliceBuffer>> payload)
      : header_(header), payload_(std::move(payload)) {}

  const FrameHeader& header() { return header_; }

  auto Payload() {
    return Map(
        MatchPromise(
            std::move(payload_),
            [](absl::StatusOr<SliceBuffer> status) { return status; },
            [](Promise<absl::StatusOr<SliceBuffer>> promise) {
              return promise;
            }),
        [this](absl::StatusOr<SliceBuffer> payload) -> absl::StatusOr<Frame> {
          if (!payload.ok()) return payload.status();
          return ParseFrame(header_, std::move(*payload));
        });
  }

 private:
  FrameHeader header_;
  absl::variant<absl::StatusOr<SliceBuffer>,
                Promise<absl::StatusOr<SliceBuffer>>>
      payload_;
};

class FrameTransport : public RefCounted<FrameTransport> {
 public:
  // Spawn a read loop onto party - read frames from the wire, push them onto
  // frames.
  // TODO(ctiller): can likely use a buffered intra-party SpscSender here once
  // we write one.
  virtual void StartReading(Party* party, MpscSender<IncomingFrame> frames,
                            absl::AnyInvocable<void(absl::Status)> on_done) = 0;
  // Spawn a write loop onto party - write frames from frames to the wire.
  virtual void StartWriting(Party* party, MpscReceiver<Frame> frames,
                            absl::AnyInvocable<void(absl::Status)> on_done) = 0;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_FRAME_TRANSPORT_H
