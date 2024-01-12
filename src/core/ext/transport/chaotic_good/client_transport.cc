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

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
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
#include "src/core/lib/promise/all_ok.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

auto ChaoticGoodClientTransport::TransportWriteLoop() {
  return Loop([this] {
    return TrySeq(
        // Get next outgoing frame.
        outgoing_frames_.Next(),
        // Serialize and write it out.
        [this](ClientFrame client_frame) {
          return transport_.WriteFrame(GetFrameInterface(client_frame));
        },
        []() -> LoopCtl<absl::Status> {
          // The write failures will be caught in TrySeq and exit loop.
          // Therefore, only need to return Continue() in the last lambda
          // function.
          return Continue();
        });
  });
}

absl::optional<CallHandler> ChaoticGoodClientTransport::LookupStream(
    uint32_t stream_id) {
  MutexLock lock(&mu_);
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    return absl::nullopt;
  }
  return it->second;
}

auto ChaoticGoodClientTransport::PushFrameIntoCall(ServerFragmentFrame frame,
                                                   CallHandler call_handler) {
  auto& headers = frame.headers;
  return TrySeq(
      If(
          headers != nullptr,
          [call_handler, &headers]() mutable {
            return call_handler.PushServerInitialMetadata(std::move(headers));
          },
          []() -> StatusFlag { return Success{}; }),
      [call_handler, message = std::move(frame.message)]() mutable {
        return If(
            message.has_value(),
            [&call_handler, &message]() mutable {
              return call_handler.PushMessage(std::move(message->message));
            },
            []() -> StatusFlag { return Success{}; });
      },
      [call_handler, trailers = std::move(frame.trailers)]() mutable {
        return If(
            trailers != nullptr,
            [&call_handler, &trailers]() mutable {
              return call_handler.PushServerTrailingMetadata(
                  std::move(trailers));
            },
            []() -> StatusFlag { return Success{}; });
      });
}

auto ChaoticGoodClientTransport::TransportReadLoop() {
  return Loop([this] {
    return TrySeq(
        transport_.ReadFrameBytes(),
        [](std::tuple<FrameHeader, BufferPair> frame_bytes)
            -> absl::StatusOr<std::tuple<FrameHeader, BufferPair>> {
          const auto& frame_header = std::get<0>(frame_bytes);
          if (frame_header.type != FrameType::kFragment) {
            return absl::InternalError(
                absl::StrCat("Expected fragment frame, got ",
                             static_cast<int>(frame_header.type)));
          }
          return frame_bytes;
        },
        [this](std::tuple<FrameHeader, BufferPair> frame_bytes) {
          const auto& frame_header = std::get<0>(frame_bytes);
          auto& buffers = std::get<1>(frame_bytes);
          absl::optional<CallHandler> call_handler =
              LookupStream(frame_header.stream_id);
          ServerFragmentFrame frame;
          absl::Status deserialize_status;
          if (call_handler.has_value()) {
            deserialize_status = transport_.DeserializeFrame(
                frame_header, std::move(buffers), call_handler->arena(), frame,
                FrameLimits{1024 * 1024 * 1024, aligned_bytes_ - 1});
          } else {
            // Stream not found, skip the frame.
            transport_.SkipFrame(frame_header, std::move(buffers));
            deserialize_status = absl::OkStatus();
          }
          return If(
              deserialize_status.ok() && call_handler.has_value(),
              [this, &frame, &call_handler]() {
                return call_handler->SpawnWaitable(
                    "push-frame", [this, call_handler = *call_handler,
                                   frame = std::move(frame)]() mutable {
                      return Map(call_handler.CancelIfFails(PushFrameIntoCall(
                                     std::move(frame), call_handler)),
                                 [](StatusFlag f) {
                                   return StatusCast<absl::Status>(f);
                                 });
                    });
              },
              [&deserialize_status]() -> absl::Status {
                // Stream not found, nothing to do.
                return std::move(deserialize_status);
              });
        },
        []() -> LoopCtl<absl::Status> { return Continue{}; });
  });
}

auto ChaoticGoodClientTransport::OnTransportActivityDone() {
  return [this](absl::Status status) {
    if (!(status.ok() || status.code() == absl::StatusCode::kCancelled)) {
      this->AbortWithError();
    }
  };
}

ChaoticGoodClientTransport::ChaoticGoodClientTransport(
    std::unique_ptr<PromiseEndpoint> control_endpoint,
    std::unique_ptr<PromiseEndpoint> data_endpoint,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : outgoing_frames_(4),
      transport_(std::move(control_endpoint), std::move(data_endpoint)),
      writer_{
          MakeActivity(
              // Continuously write next outgoing frames to promise endpoints.
              TransportWriteLoop(), EventEngineWakeupScheduler(event_engine),
              OnTransportActivityDone()),
      },
      reader_{MakeActivity(
          // Continuously read next incoming frames from promise endpoints.
          TransportReadLoop(), EventEngineWakeupScheduler(event_engine),
          OnTransportActivityDone())} {}

ChaoticGoodClientTransport::~ChaoticGoodClientTransport() {
  if (writer_ != nullptr) {
    writer_.reset();
  }
  if (reader_ != nullptr) {
    reader_.reset();
  }
}

void ChaoticGoodClientTransport::AbortWithError() {
  // Mark transport as unavailable when the endpoint write/read failed.
  // Close all the available pipes.
  outgoing_frames_.MarkClosed();
  ReleasableMutexLock lock(&mu_);
  StreamMap stream_map = std::move(stream_map_);
  stream_map_.clear();
  lock.Release();
  for (const auto& pair : stream_map) {
    auto call_handler = pair.second;
    call_handler.SpawnInfallible("cancel", [call_handler]() mutable {
      call_handler.Cancel(ServerMetadataFromStatus(
          absl::UnavailableError("Transport closed.")));
      return Empty{};
    });
  }
}

uint32_t ChaoticGoodClientTransport::MakeStream(CallHandler call_handler) {
  ReleasableMutexLock lock(&mu_);
  const uint32_t stream_id = next_stream_id_++;
  stream_map_.emplace(stream_id, call_handler);
  lock.Release();
  call_handler.OnDone([this, stream_id]() {
    MutexLock lock(&mu_);
    stream_map_.erase(stream_id);
  });
  return stream_id;
}

auto ChaoticGoodClientTransport::CallOutboundLoop(uint32_t stream_id,
                                                  CallHandler call_handler) {
  auto send_fragment = [stream_id,
                        outgoing_frames = outgoing_frames_.MakeSender()](
                           ClientFragmentFrame frame) mutable {
    frame.stream_id = stream_id;
    return Map(outgoing_frames.Send(std::move(frame)),
               [](bool success) -> absl::Status {
                 if (!success) {
                   // Failed to send outgoing frame.
                   return absl::UnavailableError("Transport closed.");
                 }
                 return absl::OkStatus();
               });
  };
  return TrySeq(
      // Wait for initial metadata then send it out.
      call_handler.PullClientInitialMetadata(),
      [send_fragment](ClientMetadataHandle md) mutable {
        ClientFragmentFrame frame;
        frame.headers = std::move(md);
        return send_fragment(std::move(frame));
      },
      // Continuously send client frame with client to server messages.
      ForEach(OutgoingMessages(call_handler),
              [send_fragment,
               aligned_bytes = aligned_bytes_](MessageHandle message) mutable {
                ClientFragmentFrame frame;
                // Construct frame header (flags, header_length and
                // trailer_length will be added in serialization).
                const uint32_t message_length = message->payload()->Length();
                const uint32_t padding =
                    message_length % aligned_bytes == 0
                        ? 0
                        : aligned_bytes - message_length % aligned_bytes;
                GPR_ASSERT((message_length + padding) % aligned_bytes == 0);
                frame.message = FragmentMessage(std::move(message), padding,
                                                message_length);
                return send_fragment(std::move(frame));
              }),
      [send_fragment]() mutable {
        ClientFragmentFrame frame;
        frame.end_of_stream = true;
        return send_fragment(std::move(frame));
      });
}

void ChaoticGoodClientTransport::StartCall(CallHandler call_handler) {
  // At this point, the connection is set up.
  // Start sending data frames.
  call_handler.SpawnGuarded("outbound_loop", [this, call_handler]() mutable {
    return CallOutboundLoop(MakeStream(call_handler), call_handler);
  });
}

}  // namespace chaotic_good
}  // namespace grpc_core
