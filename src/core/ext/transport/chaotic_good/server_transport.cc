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

#include "src/core/ext/transport/chaotic_good/server_transport.h"

#include <memory>
#include <string>
#include <tuple>

#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/switch.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {

auto ChaoticGoodServerTransport::TransportWriteLoop() {
  return Loop([this] {
    return TrySeq(
        // Get next outgoing frame.
        outgoing_frames_.Next(),
        // Serialize and write it out.
        [this](ServerFrame client_frame) {
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

auto ChaoticGoodServerTransport::PushFragmentIntoCall(
    CallInitiator call_initiator, ClientFragmentFrame frame) {
  auto& headers = frame.headers;
  return TrySeq(
      If(
          headers != nullptr,
          [call_initiator, &headers]() mutable {
            return call_initiator.PushClientInitialMetadata(std::move(headers));
          },
          []() -> StatusFlag { return Success{}; }),
      [call_initiator, message = std::move(frame.message)]() mutable {
        return If(
            message.has_value(),
            [&call_initiator, &message]() mutable {
              return call_initiator.PushMessage(std::move(message->message));
            },
            []() -> StatusFlag { return Success{}; });
      },
      [call_initiator,
       end_of_stream = frame.end_of_stream]() mutable -> StatusFlag {
        if (end_of_stream) call_initiator.FinishSends();
        return Success{};
      });
}

auto ChaoticGoodServerTransport::MaybePushFragmentIntoCall(
    absl::optional<CallInitiator> call_initiator, absl::Status error,
    ClientFragmentFrame frame) {
  return If(
      call_initiator.has_value() && error.ok(),
      [this, &call_initiator, &frame]() {
        return Map(
            call_initiator->SpawnWaitable(
                "push-fragment",
                [call_initiator, frame = std::move(frame), this]() mutable {
                  return call_initiator->CancelIfFails(
                      PushFragmentIntoCall(*call_initiator, std::move(frame)));
                }),
            [](StatusFlag status) { return StatusCast<absl::Status>(status); });
      },
      [error = std::move(error)]() { return error; });
}

auto ChaoticGoodServerTransport::CallOutboundLoop(
    uint32_t stream_id, CallInitiator call_initiator) {
  auto send_fragment = [stream_id,
                        outgoing_frames = outgoing_frames_.MakeSender()](
                           ServerFragmentFrame frame) mutable {
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
  return Seq(
      TrySeq(
          // Wait for initial metadata then send it out.
          call_initiator.PullServerInitialMetadata(),
          [send_fragment](ServerMetadataHandle md) mutable {
            ServerFragmentFrame frame;
            frame.headers = std::move(md);
            return send_fragment(std::move(frame));
          },
          // Continuously send client frame with client to server messages.
          ForEach(OutgoingMessages(call_initiator),
                  [send_fragment, aligned_bytes = aligned_bytes_](
                      MessageHandle message) mutable {
                    ServerFragmentFrame frame;
                    // Construct frame header (flags, header_length and
                    // trailer_length will be added in serialization).
                    const uint32_t message_length =
                        message->payload()->Length();
                    const uint32_t padding =
                        message_length % aligned_bytes == 0
                            ? 0
                            : aligned_bytes - message_length % aligned_bytes;
                    GPR_ASSERT((message_length + padding) % aligned_bytes == 0);
                    frame.message = FragmentMessage(std::move(message), padding,
                                                    message_length);
                    return send_fragment(std::move(frame));
                  })),
      call_initiator.PullServerTrailingMetadata(),
      [send_fragment](ServerMetadataHandle md) mutable {
        ServerFragmentFrame frame;
        frame.trailers = std::move(md);
        return send_fragment(std::move(frame));
      });
}

auto ChaoticGoodServerTransport::DeserializeAndPushFragmentToNewCall(
    FrameHeader frame_header, BufferPair buffers) {
  ClientFragmentFrame fragment_frame;
  ScopedArenaPtr arena(acceptor_->CreateArena());
  absl::Status status = transport_.DeserializeFrame(
      frame_header, std::move(buffers), arena.get(), fragment_frame,
      FrameLimits{1024 * 1024 * 1024, aligned_bytes_ - 1});
  absl::optional<CallInitiator> call_initiator;
  if (status.ok()) {
    auto create_call_result =
        acceptor_->CreateCall(*fragment_frame.headers, arena.release());
    if (create_call_result.ok()) {
      call_initiator.emplace(std::move(*create_call_result));
      call_initiator->SpawnGuarded(
          "server-write", [this, stream_id = frame_header.stream_id,
                           call_initiator = *call_initiator]() {
            return CallOutboundLoop(stream_id, call_initiator);
          });
    } else {
      status = create_call_result.status();
    }
  }
  return MaybePushFragmentIntoCall(std::move(call_initiator), std::move(status),
                                   std::move(fragment_frame));
}

auto ChaoticGoodServerTransport::DeserializeAndPushFragmentToExistingCall(
    FrameHeader frame_header, BufferPair buffers) {
  absl::optional<CallInitiator> call_initiator =
      LookupStream(frame_header.stream_id);
  Arena* arena = nullptr;
  if (call_initiator.has_value()) arena = call_initiator->arena();
  ClientFragmentFrame fragment_frame;
  absl::Status status = transport_.DeserializeFrame(
      frame_header, std::move(buffers), arena, fragment_frame,
      FrameLimits{1024 * 1024 * 1024, aligned_bytes_ - 1});
  return MaybePushFragmentIntoCall(std::move(call_initiator), std::move(status),
                                   std::move(fragment_frame));
}

auto ChaoticGoodServerTransport::TransportReadLoop() {
  return Loop([this] {
    return TrySeq(
        transport_.ReadFrameBytes(),
        [this](std::tuple<FrameHeader, BufferPair> frame_bytes) {
          const auto& frame_header = std::get<0>(frame_bytes);
          auto& buffers = std::get<1>(frame_bytes);
          return Switch(
              frame_header.type,
              Case(FrameType::kSettings,
                   []() -> absl::Status {
                     return absl::InternalError("Unexpected settings frame");
                   }),
              Case(FrameType::kFragment,
                   [this, &frame_header, &buffers]() {
                     return If(
                         frame_header.flags.is_set(0),
                         [this, &frame_header, &buffers]() {
                           return DeserializeAndPushFragmentToNewCall(
                               frame_header, std::move(buffers));
                         },
                         [this, &frame_header, &buffers]() {
                           return DeserializeAndPushFragmentToExistingCall(
                               frame_header, std::move(buffers));
                         });
                   }),
              Case(FrameType::kCancel,
                   [this, &frame_header]() {
                     absl::optional<CallInitiator> call_initiator =
                         ExtractStream(frame_header.stream_id);
                     return If(
                         call_initiator.has_value(),
                         [&call_initiator]() {
                           auto c = std::move(*call_initiator);
                           return c.SpawnWaitable("cancel", [c]() mutable {
                             c.Cancel();
                             return absl::OkStatus();
                           });
                         },
                         []() -> absl::Status {
                           return absl::InternalError(
                               "Unexpected cancel frame");
                         });
                   }),
              Default([frame_header]() {
                return absl::InternalError(
                    absl::StrCat("Unexpected frame type: ",
                                 static_cast<uint8_t>(frame_header.type)));
              }));
        },
        []() -> LoopCtl<absl::Status> { return Continue{}; });
  });
}

auto ChaoticGoodServerTransport::OnTransportActivityDone() {
  return [this](absl::Status status) {
    if (!(status.ok() || status.code() == absl::StatusCode::kCancelled)) {
      this->AbortWithError();
    }
  };
}

ChaoticGoodServerTransport::ChaoticGoodServerTransport(
    const ChannelArgs& args, std::unique_ptr<PromiseEndpoint> control_endpoint,
    std::unique_ptr<PromiseEndpoint> data_endpoint,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : outgoing_frames_(4),
      transport_(std::move(control_endpoint), std::move(data_endpoint)),
      allocator_(args.GetObject<ResourceQuota>()
                     ->memory_quota()
                     ->CreateMemoryAllocator("chaotic-good")),
      event_engine_(event_engine),
      writer_{MakeActivity(TransportWriteLoop(),
                           EventEngineWakeupScheduler(event_engine),
                           OnTransportActivityDone())},
      reader_{nullptr} {}

void ChaoticGoodServerTransport::SetAcceptor(Acceptor* acceptor) {
  GPR_ASSERT(acceptor_ == nullptr);
  GPR_ASSERT(acceptor != nullptr);
  acceptor_ = acceptor;
  reader_ = MakeActivity(TransportReadLoop(),
                         EventEngineWakeupScheduler(event_engine_),
                         OnTransportActivityDone());
}

ChaoticGoodServerTransport::~ChaoticGoodServerTransport() {
  if (writer_ != nullptr) {
    writer_.reset();
  }
  if (reader_ != nullptr) {
    reader_.reset();
  }
}

void ChaoticGoodServerTransport::AbortWithError() {
  // Mark transport as unavailable when the endpoint write/read failed.
  // Close all the available pipes.
  outgoing_frames_.MarkClosed();
  ReleasableMutexLock lock(&mu_);
  StreamMap stream_map = std::move(stream_map_);
  stream_map_.clear();
  lock.Release();
  for (const auto& pair : stream_map) {
    auto call_initiator = pair.second;
    call_initiator.SpawnInfallible("cancel", [call_initiator]() mutable {
      call_initiator.Cancel();
      return Empty{};
    });
  }
}

absl::optional<CallInitiator> ChaoticGoodServerTransport::LookupStream(
    uint32_t stream_id) {
  MutexLock lock(&mu_);
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) return absl::nullopt;
  return it->second;
}

absl::optional<CallInitiator> ChaoticGoodServerTransport::ExtractStream(
    uint32_t stream_id) {
  MutexLock lock(&mu_);
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) return absl::nullopt;
  auto r = std::move(it->second);
  stream_map_.erase(it);
  return std::move(r);
}

}  // namespace chaotic_good
}  // namespace grpc_core
