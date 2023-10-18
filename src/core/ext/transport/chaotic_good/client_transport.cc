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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chaotic_good/client_transport.h"

#include <memory>
#include <string>
#include <tuple>

#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

ClientTransport::ClientTransport(
    std::unique_ptr<PromiseEndpoint> control_endpoint,
    std::unique_ptr<PromiseEndpoint> data_endpoint,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : outgoing_frames_(MpscReceiver<ClientFrame>(4)),
      control_endpoint_(std::move(control_endpoint)),
      data_endpoint_(std::move(data_endpoint)),
      control_endpoint_write_buffer_(SliceBuffer()),
      data_endpoint_write_buffer_(SliceBuffer()),
      hpack_compressor_(std::make_unique<HPackCompressor>()),
      event_engine_(event_engine) {
  auto write_loop = Loop([this] {
    return Seq(
        // Get next outgoing frame.
        this->outgoing_frames_.Next(),
        // Construct data buffers that will be sent to the endpoints.
        [this](ClientFrame client_frame) {
          MatchMutable(
              &client_frame,
              [this](ClientFragmentFrame* frame) mutable {
                control_endpoint_write_buffer_.Append(
                    frame->Serialize(hpack_compressor_.get()));
                if (frame->message != nullptr) {
                  auto frame_header =
                      FrameHeader::Parse(
                          reinterpret_cast<const uint8_t*>(GRPC_SLICE_START_PTR(
                              control_endpoint_write_buffer_.c_slice_buffer()
                                  ->slices[0])))
                          .value();
                  std::string message_padding(frame_header.message_padding,
                                              '0');
                  Slice slice(grpc_slice_from_cpp_string(message_padding));
                  // Append message payload to data_endpoint_buffer.
                  data_endpoint_write_buffer_.Append(std::move(slice));
                  // Append message payload to data_endpoint_buffer.
                  frame->message->payload()->MoveFirstNBytesIntoSliceBuffer(
                      frame->message->payload()->Length(),
                      data_endpoint_write_buffer_);
                }
              },
              [this](CancelFrame* frame) mutable {
                control_endpoint_write_buffer_.Append(
                    frame->Serialize(hpack_compressor_.get()));
              });
          return absl::OkStatus();
        },
        // Write buffers to corresponding endpoints concurrently.
        [this]() {
          return Join(this->control_endpoint_->Write(
                          std::move(control_endpoint_write_buffer_)),
                      this->data_endpoint_->Write(
                          std::move(data_endpoint_write_buffer_)));
        },
        // Finish writes and return status.
        [](std::tuple<absl::Status, absl::Status> ret)
            -> LoopCtl<absl::Status> {
          // If writes failed, return failure status.
          if (!(std::get<0>(ret).ok() || std::get<1>(ret).ok())) {
            // TODO(ladynana): handle the promise endpoint write failures with
            // closing the transport.
            return absl::InternalError("Promise endpoint writes failed.");
          }
          return Continue();
        });
  });
  writer_ = MakeActivity(
      // Continuously write next outgoing frames to promise endpoints.
      std::move(write_loop), EventEngineWakeupScheduler(event_engine_),
      [](absl::Status status) {
        GPR_ASSERT(status.code() == absl::StatusCode::kCancelled ||
                   status.code() == absl::StatusCode::kInternal);
      });
}

}  // namespace chaotic_good
}  // namespace grpc_core
