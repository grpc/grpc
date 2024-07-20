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

#include "src/core/ext/transport/chaotic_good/server_transport.h"

#include <memory>
#include <string>
#include <tuple>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chaotic_good/chaotic_good_transport.h"
#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/lib/event_engine/event_engine_context.h"
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

auto ChaoticGoodServerTransport::TransportWriteLoop(
    RefCountedPtr<ChaoticGoodTransport> transport) {
  return Loop([this, transport = std::move(transport)] {
    return TrySeq(
        // Get next outgoing frame.
        outgoing_frames_.Next(),
        // Serialize and write it out.
        [transport = transport.get()](ServerFrame client_frame) {
          return transport->WriteFrame(GetFrameInterface(client_frame));
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
    CallInitiator call_initiator, ClientFragmentFrame frame,
    uint32_t stream_id) {
  DCHECK(frame.headers == nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(chaotic_good)) {
    LOG(INFO) << "CHAOTIC_GOOD: PushFragmentIntoCall: frame="
              << frame.ToString();
  }
  return Seq(If(
                 frame.message.has_value(),
                 [&call_initiator, &frame]() mutable {
                   return call_initiator.PushMessage(
                       std::move(frame.message->message));
                 },
                 []() -> StatusFlag { return Success{}; }),
             [this, call_initiator, end_of_stream = frame.end_of_stream,
              stream_id](StatusFlag status) mutable -> StatusFlag {
               if (!status.ok() && GRPC_TRACE_FLAG_ENABLED(chaotic_good)) {
                 LOG(INFO) << "CHAOTIC_GOOD: Failed PushFragmentIntoCall";
               }
               if (end_of_stream || !status.ok()) {
                 call_initiator.FinishSends();
                 // We have received end_of_stream. It is now safe to remove
                 // the call from the stream map.
                 MutexLock lock(&mu_);
                 stream_map_.erase(stream_id);
               }
               return Success{};
             });
}

auto ChaoticGoodServerTransport::MaybePushFragmentIntoCall(
    absl::optional<CallInitiator> call_initiator, absl::Status error,
    ClientFragmentFrame frame, uint32_t stream_id) {
  return If(
      call_initiator.has_value() && error.ok(),
      [this, &call_initiator, &frame, &stream_id]() {
        return Map(
            call_initiator->SpawnWaitable(
                "push-fragment",
                [call_initiator, frame = std::move(frame), stream_id,
                 this]() mutable {
                  return call_initiator->CancelIfFails(PushFragmentIntoCall(
                      *call_initiator, std::move(frame), stream_id));
                }),
            [](StatusFlag status) { return StatusCast<absl::Status>(status); });
      },
      [&error, &frame]() {
        // EOF frames may arrive after the call_initiator's OnDone callback
        // has been invoked. In that case, the call_initiator would have
        // already been removed from the stream_map and hence the EOF frame
        // cannot be pushed into the call. No need to log such frames.
        if (!frame.end_of_stream) {
          LOG(INFO) << "CHAOTIC_GOOD: Cannot pass frame to stream. Error:"
                    << error.ToString() << " Frame:" << frame.ToString();
        }
        return Immediate(std::move(error));
      });
}

auto ChaoticGoodServerTransport::SendFragment(
    ServerFragmentFrame frame, MpscSender<ServerFrame> outgoing_frames,
    CallInitiator call_initiator) {
  if (GRPC_TRACE_FLAG_ENABLED(chaotic_good)) {
    LOG(INFO) << "CHAOTIC_GOOD: SendFragment: frame=" << frame.ToString();
  }
  // Capture the call_initiator to ensure the underlying call spine is alive
  // until the outgoing_frames.Send promise completes.
  return Map(outgoing_frames.Send(std::move(frame)),
             [call_initiator](bool success) -> absl::Status {
               if (!success) {
                 // Failed to send outgoing frame.
                 return absl::UnavailableError("Transport closed.");
               }
               return absl::OkStatus();
             });
}

auto ChaoticGoodServerTransport::SendCallBody(
    uint32_t stream_id, MpscSender<ServerFrame> outgoing_frames,
    CallInitiator call_initiator) {
  // Continuously send client frame with client to server
  // messages.
  return ForEach(
      OutgoingMessages(call_initiator),
      // Capture the call_initator to ensure the underlying call
      // spine is alive until the SendFragment promise completes.
      [stream_id, outgoing_frames, call_initiator,
       aligned_bytes = aligned_bytes_](MessageHandle message) mutable {
        ServerFragmentFrame frame;
        // Construct frame header (flags, header_length
        // and trailer_length will be added in
        // serialization).
        const uint32_t message_length = message->payload()->Length();
        const uint32_t padding =
            message_length % aligned_bytes == 0
                ? 0
                : aligned_bytes - message_length % aligned_bytes;
        CHECK_EQ((message_length + padding) % aligned_bytes, 0u);
        frame.message =
            FragmentMessage(std::move(message), padding, message_length);
        frame.stream_id = stream_id;
        return SendFragment(std::move(frame), outgoing_frames, call_initiator);
      });
}

auto ChaoticGoodServerTransport::SendCallInitialMetadataAndBody(
    uint32_t stream_id, MpscSender<ServerFrame> outgoing_frames,
    CallInitiator call_initiator) {
  return TrySeq(
      // Wait for initial metadata then send it out.
      call_initiator.PullServerInitialMetadata(),
      [stream_id, outgoing_frames, call_initiator,
       this](absl::optional<ServerMetadataHandle> md) mutable {
        if (GRPC_TRACE_FLAG_ENABLED(chaotic_good)) {
          LOG(INFO) << "CHAOTIC_GOOD: SendCallInitialMetadataAndBody: md="
                    << (md.has_value() ? (*md)->DebugString() : "null");
        }
        return If(
            md.has_value(),
            [&md, stream_id, &outgoing_frames, &call_initiator, this]() {
              ServerFragmentFrame frame;
              frame.headers = std::move(*md);
              frame.stream_id = stream_id;
              return TrySeq(
                  SendFragment(std::move(frame), outgoing_frames,
                               call_initiator),
                  SendCallBody(stream_id, outgoing_frames, call_initiator));
            },
            []() { return absl::OkStatus(); });
      });
}

auto ChaoticGoodServerTransport::CallOutboundLoop(
    uint32_t stream_id, CallInitiator call_initiator) {
  auto outgoing_frames = outgoing_frames_.MakeSender();
  return Seq(
      Map(SendCallInitialMetadataAndBody(stream_id, outgoing_frames,
                                         call_initiator),
          [stream_id](absl::Status main_body_result) {
            if (GRPC_TRACE_FLAG_ENABLED(chaotic_good)) {
              VLOG(2) << "CHAOTIC_GOOD: CallOutboundLoop: stream_id="
                      << stream_id << " main_body_result=" << main_body_result;
            }
            return Empty{};
          }),
      call_initiator.PullServerTrailingMetadata(),
      // Capture the call_initator to ensure the underlying call_spine
      // is alive until the SendFragment promise completes.
      [stream_id, outgoing_frames,
       call_initiator](ServerMetadataHandle md) mutable {
        ServerFragmentFrame frame;
        frame.trailers = std::move(md);
        frame.stream_id = stream_id;
        return SendFragment(std::move(frame), outgoing_frames, call_initiator);
      });
}

auto ChaoticGoodServerTransport::DeserializeAndPushFragmentToNewCall(
    FrameHeader frame_header, BufferPair buffers,
    ChaoticGoodTransport& transport) {
  ClientFragmentFrame fragment_frame;
  RefCountedPtr<Arena> arena(call_arena_allocator_->MakeArena());
  arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      event_engine_.get());
  absl::Status status = transport.DeserializeFrame(
      frame_header, std::move(buffers), arena.get(), fragment_frame,
      FrameLimits{1024 * 1024 * 1024, aligned_bytes_ - 1});
  absl::optional<CallInitiator> call_initiator;
  if (status.ok()) {
    auto call =
        MakeCallPair(std::move(fragment_frame.headers), std::move(arena));
    call_initiator.emplace(std::move(call.initiator));
    auto add_result = NewStream(frame_header.stream_id, *call_initiator);
    if (add_result.ok()) {
      call_destination_->StartCall(std::move(call.handler));
      call_initiator->SpawnGuarded(
          "server-write", [this, stream_id = frame_header.stream_id,
                           call_initiator = *call_initiator]() {
            return CallOutboundLoop(stream_id, call_initiator);
          });
    } else {
      call_initiator.reset();
      status = add_result;
    }
  }
  return MaybePushFragmentIntoCall(std::move(call_initiator), std::move(status),
                                   std::move(fragment_frame),
                                   frame_header.stream_id);
}

auto ChaoticGoodServerTransport::DeserializeAndPushFragmentToExistingCall(
    FrameHeader frame_header, BufferPair buffers,
    ChaoticGoodTransport& transport) {
  absl::optional<CallInitiator> call_initiator =
      LookupStream(frame_header.stream_id);
  Arena* arena = nullptr;
  if (call_initiator.has_value()) arena = call_initiator->arena();
  ClientFragmentFrame fragment_frame;
  absl::Status status = transport.DeserializeFrame(
      frame_header, std::move(buffers), arena, fragment_frame,
      FrameLimits{1024 * 1024 * 1024, aligned_bytes_ - 1});
  return MaybePushFragmentIntoCall(std::move(call_initiator), std::move(status),
                                   std::move(fragment_frame),
                                   frame_header.stream_id);
}

auto ChaoticGoodServerTransport::ReadOneFrame(ChaoticGoodTransport& transport) {
  return TrySeq(
      transport.ReadFrameBytes(),
      [this, transport =
                 &transport](std::tuple<FrameHeader, BufferPair> frame_bytes) {
        const auto& frame_header = std::get<0>(frame_bytes);
        auto& buffers = std::get<1>(frame_bytes);
        return Switch(
            frame_header.type,
            Case(FrameType::kSettings,
                 []() -> absl::Status {
                   return absl::InternalError("Unexpected settings frame");
                 }),
            Case(FrameType::kFragment,
                 [this, &frame_header, &buffers, transport]() {
                   return If(
                       frame_header.flags.is_set(0),
                       [this, &frame_header, &buffers, transport]() {
                         return DeserializeAndPushFragmentToNewCall(
                             frame_header, std::move(buffers), *transport);
                       },
                       [this, &frame_header, &buffers, transport]() {
                         return DeserializeAndPushFragmentToExistingCall(
                             frame_header, std::move(buffers), *transport);
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
                       []() -> absl::Status { return absl::OkStatus(); });
                 }),
            Default([frame_header]() {
              return absl::InternalError(
                  absl::StrCat("Unexpected frame type: ",
                               static_cast<uint8_t>(frame_header.type)));
            }));
      },
      []() -> LoopCtl<absl::Status> { return Continue{}; });
}

auto ChaoticGoodServerTransport::TransportReadLoop(
    RefCountedPtr<ChaoticGoodTransport> transport) {
  return Seq(got_acceptor_.Wait(),
             Loop([this, transport = std::move(transport)] {
               return ReadOneFrame(*transport);
             }));
}

auto ChaoticGoodServerTransport::OnTransportActivityDone(
    absl::string_view activity) {
  return [self = RefAsSubclass<ChaoticGoodServerTransport>(),
          activity](absl::Status status) {
    if (GRPC_TRACE_FLAG_ENABLED(chaotic_good)) {
      LOG(INFO) << "CHAOTIC_GOOD: OnTransportActivityDone: activity="
                << activity << " status=" << status;
    }
    self->AbortWithError();
  };
}

ChaoticGoodServerTransport::ChaoticGoodServerTransport(
    const ChannelArgs& args, PromiseEndpoint control_endpoint,
    PromiseEndpoint data_endpoint,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    HPackParser hpack_parser, HPackCompressor hpack_encoder)
    : call_arena_allocator_(MakeRefCounted<CallArenaAllocator>(
          args.GetObject<ResourceQuota>()
              ->memory_quota()
              ->CreateMemoryAllocator("chaotic-good"),
          1024)),
      event_engine_(event_engine),
      outgoing_frames_(4) {
  auto transport = MakeRefCounted<ChaoticGoodTransport>(
      std::move(control_endpoint), std::move(data_endpoint),
      std::move(hpack_parser), std::move(hpack_encoder));
  writer_ = MakeActivity(TransportWriteLoop(transport),
                         EventEngineWakeupScheduler(event_engine),
                         OnTransportActivityDone("writer"));
  reader_ = MakeActivity(TransportReadLoop(std::move(transport)),
                         EventEngineWakeupScheduler(event_engine),
                         OnTransportActivityDone("reader"));
}

void ChaoticGoodServerTransport::SetCallDestination(
    RefCountedPtr<UnstartedCallDestination> call_destination) {
  CHECK(call_destination_ == nullptr);
  CHECK(call_destination != nullptr);
  call_destination_ = call_destination;
  got_acceptor_.Set();
}

void ChaoticGoodServerTransport::Orphan() {
  ActivityPtr writer;
  ActivityPtr reader;
  {
    MutexLock lock(&mu_);
    writer = std::move(writer_);
    reader = std::move(reader_);
  }
  writer.reset();
  reader.reset();
  Unref();
}

void ChaoticGoodServerTransport::AbortWithError() {
  // Mark transport as unavailable when the endpoint write/read failed.
  // Close all the available pipes.
  outgoing_frames_.MarkClosed();
  ReleasableMutexLock lock(&mu_);
  StreamMap stream_map = std::move(stream_map_);
  stream_map_.clear();
  state_tracker_.SetState(GRPC_CHANNEL_SHUTDOWN,
                          absl::UnavailableError("transport closed"),
                          "transport closed");
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

absl::Status ChaoticGoodServerTransport::NewStream(
    uint32_t stream_id, CallInitiator call_initiator) {
  MutexLock lock(&mu_);
  auto it = stream_map_.find(stream_id);
  if (it != stream_map_.end()) {
    return absl::InternalError("Stream already exists");
  }
  if (stream_id <= last_seen_new_stream_id_) {
    return absl::InternalError("Stream id is not increasing");
  }
  stream_map_.emplace(stream_id, call_initiator);
  call_initiator.OnDone([this, stream_id]() {
    MutexLock lock(&mu_);
    stream_map_.erase(stream_id);
  });
  return absl::OkStatus();
}

void ChaoticGoodServerTransport::PerformOp(grpc_transport_op* op) {
  std::vector<ActivityPtr> cancelled;
  MutexLock lock(&mu_);
  bool did_stuff = false;
  if (op->start_connectivity_watch != nullptr) {
    state_tracker_.AddWatcher(op->start_connectivity_watch_state,
                              std::move(op->start_connectivity_watch));
    did_stuff = true;
  }
  if (op->stop_connectivity_watch != nullptr) {
    state_tracker_.RemoveWatcher(op->stop_connectivity_watch);
    did_stuff = true;
  }
  if (op->set_accept_stream) {
    if (op->set_accept_stream_fn != nullptr) {
      Crash(absl::StrCat(
          "set_accept_stream not supported on chaotic good transports: ",
          grpc_transport_op_string(op)));
    }
    did_stuff = true;
  }
  if (!op->goaway_error.ok() || !op->disconnect_with_error.ok()) {
    cancelled.push_back(std::move(writer_));
    cancelled.push_back(std::move(reader_));
    did_stuff = true;
  }
  if (!did_stuff) {
    Crash(absl::StrCat("unimplemented transport perform op: ",
                       grpc_transport_op_string(op)));
  }
  ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
}

}  // namespace chaotic_good
}  // namespace grpc_core
