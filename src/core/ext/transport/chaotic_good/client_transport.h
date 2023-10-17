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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <initializer_list>  // IWYU pragma: keep
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"  // IWYU pragma: keep
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {

class ClientTransport {
 public:
  ClientTransport(std::unique_ptr<PromiseEndpoint> control_endpoint,
                  std::unique_ptr<PromiseEndpoint> data_endpoint,
                  std::shared_ptr<grpc_event_engine::experimental::EventEngine>
                      event_engine);
  ~ClientTransport() {
    if (writer_ != nullptr) {
      writer_.reset();
    }
  }
  auto AddStream(CallArgs call_args) {
    // At this point, the connection is set up.
    // Start sending data frames.
    uint32_t stream_id;
    {
      MutexLock lock(&mu_);
      stream_id = next_stream_id_++;
    }
    return Seq(
        // Continuously send data frame with client to server messages.
        ForEach(std::move(*call_args.client_to_server_messages),
                [stream_id, initial_frame = true,
                 client_initial_metadata =
                     std::move(call_args.client_initial_metadata),
                 outgoing_frames = outgoing_frames_.MakeSender(), this](
                    MessageHandle result) mutable {
                  ClientFragmentFrame frame;
                  // Construct frame header (flags, header_length and trailer_length will be added in serialization).
                  uint32_t message_length = result->payload()->Length();
                  uint32_t message_padding = message_length % aligned_bytes;
                  frame.frame_header = FrameHeader{
                      FrameType::kFragment, {}, stream_id, 0, message_length,
                      message_padding,      0};
                  frame.message = std::move(result);
                  if (initial_frame) {
                    // Send initial frame with client intial metadata.
                    frame.headers = std::move(client_initial_metadata);
                    initial_frame = false;
                  }
                  return Seq(
                      outgoing_frames.Send(ClientFrame(std::move(frame))),
                      [](bool success) -> absl::Status {
                        if (!success) {
                          return absl::InternalError(
                              "Send frame to outgoing_frames failed.");
                        }
                        return absl::OkStatus();
                      });
                }));
  }

 private:
  // Max buffer is set to 4, so that for stream writes each time it will queue
  // at most 2 frames.
  MpscReceiver<ClientFrame> outgoing_frames_;
  Mutex mu_;
  uint32_t next_stream_id_ ABSL_GUARDED_BY(mu_) = 1;
  // Assigned aligned bytes from setting frame.
  size_t aligned_bytes = 64;
  ActivityPtr writer_;
  ActivityPtr reader_;
  std::unique_ptr<PromiseEndpoint> control_endpoint_;
  std::unique_ptr<PromiseEndpoint> data_endpoint_;
  SliceBuffer control_endpoint_write_buffer_;
  SliceBuffer data_endpoint_write_buffer_;
  std::unique_ptr<HPackCompressor> hpack_compressor_;
  // Use to synchronize writer_ and reader_ activity with outside activities;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H
