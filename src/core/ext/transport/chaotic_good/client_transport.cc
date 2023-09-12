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
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/resource_quota.h"
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
      hpack_parser_(std::make_unique<HPackParser>()),
      memory_allocator_(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "client_transport")),
      arena_(MakeScopedArena(1024, &memory_allocator_)),
      event_engine_(event_engine) {
  auto write_loop = Loop([this] {
    return TrySeq(
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
                  // TODO(ladynana): add message_padding calculation by
                  // accumulating bytes sent.
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
          return TryJoin(
              control_endpoint_->Write(
                  std::move(control_endpoint_write_buffer_)),
              data_endpoint_->Write(std::move(data_endpoint_write_buffer_)));
        },
        // Finish writes to difference endpoints and continue the loop.
        []() -> LoopCtl<absl::Status> {
          // The write failures will be caught in TrySeq and exit loop.
          // Therefore, only need to return Continue() in the last lambda
          // function.
          return Continue();
        });
  });
  writer_ = MakeActivity(
      // Continuously write next outgoing frames to promise endpoints.
      std::move(write_loop), EventEngineWakeupScheduler(event_engine_),
      [](absl::Status status) {
        GPR_ASSERT(status.code() == absl::StatusCode::kCancelled ||
                   status.code() == absl::StatusCode::kInternal);
        // TODO(ladynana): handle the promise endpoint write failures with
        // outgoing_frames.close() once available.
      });
  auto read_loop = Loop([this] {
    return TrySeq(
        // Read frame header from control endpoint.
        this->control_endpoint_->Read(FrameHeader::frame_header_size_),
        // Read different parts of the server frame from control/data endpoints
        // based on frame header.
        [this](SliceBuffer read_buffer) mutable {
          frame_header_ = std::make_shared<FrameHeader>(
              FrameHeader::Parse(
                  reinterpret_cast<const uint8_t*>(GRPC_SLICE_START_PTR(
                      read_buffer.c_slice_buffer()->slices[0])))
                  .value());
          // Read header and trailers from control endpoint.
          // Read message padding and message from data endpoint.
          return TryJoin(
              control_endpoint_->Read(frame_header_->GetFrameLength()),
              data_endpoint_->Read(frame_header_->message_padding +
                                   frame_header_->message_length));
        },
        // Construct and send the server frame to corresponding stream.
        [this](std::tuple<SliceBuffer, SliceBuffer> ret) mutable {
          control_endpoint_read_buffer_ = std::move(std::get<0>(ret));
          // Discard message padding and only keep message in data read buffer.
          std::get<1>(ret).MoveLastNBytesIntoSliceBuffer(
              frame_header_->message_length, data_endpoint_read_buffer_);
          ServerFragmentFrame frame;
          // Initialized to get this_cpu() info in global_stat().
          ExecCtx exec_ctx;
          frame.SetArena(arena_);
          // Deserialize frame from read buffer.
          auto status = frame.Deserialize(hpack_parser_.get(), *frame_header_,
                                          control_endpoint_read_buffer_);
          GPR_ASSERT(status.ok());
          // Move message into frame.
          frame.message = arena_->MakePooled<Message>(
              std::move(data_endpoint_read_buffer_), 0);
          std::shared_ptr<
              InterActivityPipe<ServerFrame, server_frame_queue_size_>::Sender>
              sender;
          {
            MutexLock lock(&mu_);
            sender = stream_map_[frame.stream_id];
          }
          return sender->Push(ServerFrame(std::move(frame)));
        },
        // Check if send frame to corresponding stream successfully.
        [](bool ret) -> LoopCtl<absl::Status> {
          if (ret) {
            // Send incoming frames successfully.
            return Continue();
          } else {
            return absl::InternalError("Send incoming frames failed.");
          }
        });
  });
  reader_ = MakeActivity(
      // Continuously read next incoming frames from promise endpoints.
      std::move(read_loop), EventEngineWakeupScheduler(event_engine_),
      [](absl::Status status) {
        GPR_ASSERT(status.code() == absl::StatusCode::kCancelled ||
                   status.code() == absl::StatusCode::kInternal);
        // TODO(ladynana): handle the promise endpoint read failures with
        // iterating stream_map_ and close all the pipes once available.
      });
}

}  // namespace chaotic_good
}  // namespace grpc_core
