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

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <string>
#include <tuple>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/core/ext/transport/chaotic_good/chaotic_good_transport.h"
#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/lib/event_engine/event_engine_context.h"
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
#include "src/core/util/ref_counted_ptr.h"

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

auto ChaoticGoodServerTransport::PushFrameIntoCall(CallInitiator call_initiator,
                                                   MessageFrame frame) {
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: PushFrameIntoCall: frame=" << frame.ToString();
  return call_initiator.PushMessage(std::move(frame.message));
}

auto ChaoticGoodServerTransport::PushFrameIntoCall(CallInitiator call_initiator,
                                                   ClientEndOfStream) {
  call_initiator.FinishSends();
  // Note that we cannot remove from the stream map yet, as we
  // may yet receive a cancellation.
  return Immediate(Success{});
}

template <typename T>
auto ChaoticGoodServerTransport::DispatchFrame(ChaoticGoodTransport& transport,
                                               const FrameHeader& header,
                                               SliceBuffer payload) {
  return TrySeq(
      [&transport, header, payload = std::move(payload)]() mutable {
        return transport.DeserializeFrame<T>(header, std::move(payload));
      },
      [this](T frame) {
        absl::optional<CallInitiator> call_initiator =
            LookupStream(frame.stream_id);
        return If(
            call_initiator.has_value(),
            [this, &call_initiator, &frame]() {
              return call_initiator->SpawnWaitable(
                  "push-frame", [this, call_initiator = *call_initiator,
                                 frame = std::move(frame)]() mutable {
                    return Map(call_initiator.CancelIfFails(PushFrameIntoCall(
                                   call_initiator, std::move(frame))),
                               [](StatusFlag) { return absl::OkStatus(); });
                  });
            },
            []() { return absl::OkStatus(); });
      });
}

namespace {
auto BooleanSuccessToTransportErrorCapturingInitiator(CallInitiator initiator) {
  return [initiator = std::move(initiator)](bool success) {
    return success ? absl::OkStatus()
                   : absl::UnavailableError("Transport closed.");
  };
}
}  // namespace

auto ChaoticGoodServerTransport::SendFrame(
    ServerFrame frame, MpscSender<ServerFrame> outgoing_frames,
    CallInitiator call_initiator) {
  // Capture the call_initiator to ensure the underlying call spine is alive
  // until the outgoing_frames.Send promise completes.
  return Map(outgoing_frames.Send(std::move(frame)),
             BooleanSuccessToTransportErrorCapturingInitiator(
                 std::move(call_initiator)));
}

auto ChaoticGoodServerTransport::SendFrameAcked(
    ServerFrame frame, MpscSender<ServerFrame> outgoing_frames,
    CallInitiator call_initiator) {
  // Capture the call_initiator to ensure the underlying call spine is alive
  // until the outgoing_frames.Send promise completes.
  return Map(outgoing_frames.SendAcked(std::move(frame)),
             BooleanSuccessToTransportErrorCapturingInitiator(
                 std::move(call_initiator)));
}

auto ChaoticGoodServerTransport::SendCallBody(
    uint32_t stream_id, MpscSender<ServerFrame> outgoing_frames,
    CallInitiator call_initiator) {
  // Continuously send client frame with client to server messages.
  return ForEach(OutgoingMessages(call_initiator),
                 [this, stream_id, outgoing_frames = std::move(outgoing_frames),
                  call_initiator](MessageHandle message) mutable {
                   MessageFrame frame;
                   frame.message = std::move(message);
                   frame.stream_id = stream_id;
                   return SendFrameAcked(std::move(frame), outgoing_frames,
                                         call_initiator);
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
        GRPC_TRACE_LOG(chaotic_good, INFO)
            << "CHAOTIC_GOOD: SendCallInitialMetadataAndBody: md="
            << (md.has_value() ? (*md)->DebugString() : "null");
        return If(
            md.has_value(),
            [&md, stream_id, &outgoing_frames, &call_initiator, this]() {
              ServerInitialMetadataFrame frame;
              frame.headers = ServerMetadataProtoFromGrpc(**md);
              frame.stream_id = stream_id;
              return TrySeq(
                  SendFrame(std::move(frame), outgoing_frames, call_initiator),
                  SendCallBody(stream_id, outgoing_frames, call_initiator));
            },
            []() { return absl::OkStatus(); });
      });
}

auto ChaoticGoodServerTransport::CallOutboundLoop(
    uint32_t stream_id, CallInitiator call_initiator) {
  auto outgoing_frames = outgoing_frames_.MakeSender();
  return GRPC_LATENT_SEE_PROMISE(
      "CallOutboundLoop",
      Seq(Map(SendCallInitialMetadataAndBody(stream_id, outgoing_frames,
                                             call_initiator),
              [stream_id](absl::Status main_body_result) {
                GRPC_TRACE_VLOG(chaotic_good, 2)
                    << "CHAOTIC_GOOD: CallOutboundLoop: stream_id=" << stream_id
                    << " main_body_result=" << main_body_result;
                return Empty{};
              }),
          call_initiator.PullServerTrailingMetadata(),
          // Capture the call_initiator to ensure the underlying call_spine
          // is alive until the SendFragment promise completes.
          [this, stream_id, outgoing_frames,
           call_initiator](ServerMetadataHandle md) mutable {
            ServerTrailingMetadataFrame frame;
            frame.trailers = ServerMetadataProtoFromGrpc(*md);
            frame.stream_id = stream_id;
            return SendFrame(std::move(frame), outgoing_frames, call_initiator);
          }));
}

absl::Status ChaoticGoodServerTransport::NewStream(
    ChaoticGoodTransport& transport, const FrameHeader& header,
    SliceBuffer payload) {
  CHECK_EQ(header.payload_length, payload.Length());
  auto client_initial_metadata_frame =
      transport.DeserializeFrame<ClientInitialMetadataFrame>(
          header, std::move(payload));
  if (!client_initial_metadata_frame.ok()) {
    return client_initial_metadata_frame.status();
  }
  auto md = ClientMetadataGrpcFromProto(client_initial_metadata_frame->headers);
  if (!md.ok()) {
    return md.status();
  }
  RefCountedPtr<Arena> arena(call_arena_allocator_->MakeArena());
  arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      event_engine_.get());
  absl::optional<CallInitiator> call_initiator;
  auto call = MakeCallPair(std::move(*md), std::move(arena));
  call_initiator.emplace(std::move(call.initiator));
  const auto stream_id = client_initial_metadata_frame->stream_id;
  auto add_result = NewStream(stream_id, *call_initiator);
  if (!add_result.ok()) {
    call_initiator.reset();
    return add_result;
  }
  call_initiator->SpawnGuarded(
      "server-write", [this, stream_id, call_initiator = *call_initiator,
                       call_handler = std::move(call.handler)]() mutable {
        call_destination_->StartCall(std::move(call_handler));
        return CallOutboundLoop(stream_id, call_initiator);
      });
  return absl::OkStatus();
}

auto ChaoticGoodServerTransport::ReadOneFrame(ChaoticGoodTransport& transport) {
  return GRPC_LATENT_SEE_PROMISE(
      "ReadOneFrame",
      TrySeq(
          transport.ReadFrameBytes(),
          [this, transport = &transport](
              std::tuple<FrameHeader, SliceBuffer> frame_bytes) {
            const auto& header = std::get<0>(frame_bytes);
            SliceBuffer& payload = std::get<1>(frame_bytes);
            CHECK_EQ(header.payload_length, payload.Length());
            return Switch(
                header.type,
                Case<FrameType, FrameType::kClientInitialMetadata>([&, this]() {
                  return Immediate(
                      NewStream(*transport, header, std::move(payload)));
                }),
                Case<FrameType, FrameType::kMessage>([&, this]() {
                  return DispatchFrame<MessageFrame>(*transport, header,
                                                     std::move(payload));
                }),
                Case<FrameType, FrameType::kClientEndOfStream>([&, this]() {
                  return DispatchFrame<ClientEndOfStream>(*transport, header,
                                                          std::move(payload));
                }),
                Case<FrameType, FrameType::kCancel>([&, this]() {
                  absl::optional<CallInitiator> call_initiator =
                      ExtractStream(header.stream_id);
                  GRPC_TRACE_LOG(chaotic_good, INFO)
                      << "Cancel stream " << header.stream_id
                      << (call_initiator.has_value() ? " (active)"
                                                     : " (not found)");
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
                Default([&]() {
                  return absl::InternalError(
                      absl::StrCat("Unexpected frame type: ",
                                   static_cast<uint8_t>(header.type)));
                }));
          },
          []() -> LoopCtl<absl::Status> { return Continue{}; }));
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
    GRPC_TRACE_LOG(chaotic_good, INFO)
        << "CHAOTIC_GOOD: OnTransportActivityDone: activity=" << activity
        << " status=" << status;
    self->AbortWithError();
  };
}

ChaoticGoodServerTransport::ChaoticGoodServerTransport(
    const ChannelArgs& args, PromiseEndpoint control_endpoint,
    PromiseEndpoint data_endpoint,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : call_arena_allocator_(MakeRefCounted<CallArenaAllocator>(
          args.GetObject<ResourceQuota>()
              ->memory_quota()
              ->CreateMemoryAllocator("chaotic-good"),
          1024)),
      event_engine_(event_engine),
      outgoing_frames_(4) {
  ChaoticGoodTransport::Options options;
  options.inlined_payload_size_threshold =
      args.GetInt("grpc.chaotic_good.inlined_payload_size_threshold")
          .value_or(options.inlined_payload_size_threshold);
  auto transport = MakeRefCounted<ChaoticGoodTransport>(
      std::move(control_endpoint), std::move(data_endpoint), options);
  auto party_arena = SimpleArenaAllocator(0)->MakeArena();
  party_arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      event_engine.get());
  party_ = Party::Make(std::move(party_arena));
  party_->Spawn("server-chaotic-writer",
                GRPC_LATENT_SEE_PROMISE("ServerTransportWriteLoop",
                                        TransportWriteLoop(transport)),
                OnTransportActivityDone("writer"));
  party_->Spawn("server-chaotic-reader",
                GRPC_LATENT_SEE_PROMISE("ServerTransportReadLoop",
                                        TransportReadLoop(transport)),
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
  AbortWithError();
  RefCountedPtr<Party> party;
  {
    MutexLock lock(&mu_);
    party = std::move(party_);
  }
  party.reset();
  Unref();
}

void ChaoticGoodServerTransport::AbortWithError() {
  // Mark transport as unavailable when the endpoint write/read failed.
  // Close all the available pipes.
  outgoing_frames_.MarkClosed();
  ReleasableMutexLock lock(&mu_);
  aborted_with_error_ = true;
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
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD " << this << " LookupStream " << stream_id;
  MutexLock lock(&mu_);
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) return absl::nullopt;
  return it->second;
}

absl::optional<CallInitiator> ChaoticGoodServerTransport::ExtractStream(
    uint32_t stream_id) {
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD " << this << " ExtractStream " << stream_id;
  MutexLock lock(&mu_);
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) return absl::nullopt;
  auto r = std::move(it->second);
  stream_map_.erase(it);
  return std::move(r);
}

absl::Status ChaoticGoodServerTransport::NewStream(
    uint32_t stream_id, CallInitiator call_initiator) {
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD " << this << " NewStream " << stream_id;
  MutexLock lock(&mu_);
  if (aborted_with_error_) {
    return absl::UnavailableError("Transport closed");
  }
  auto it = stream_map_.find(stream_id);
  if (it != stream_map_.end()) {
    return absl::InternalError("Stream already exists");
  }
  if (stream_id <= last_seen_new_stream_id_) {
    return absl::InternalError("Stream id is not increasing");
  }
  const bool on_done_added = call_initiator.OnDone(
      [self = RefAsSubclass<ChaoticGoodServerTransport>(), stream_id](bool) {
        GRPC_TRACE_LOG(chaotic_good, INFO)
            << "CHAOTIC_GOOD " << self.get() << " OnDone " << stream_id;
        absl::optional<CallInitiator> call_initiator =
            self->ExtractStream(stream_id);
        if (call_initiator.has_value()) {
          auto c = std::move(*call_initiator);
          c.SpawnInfallible("cancel", [c]() mutable {
            c.Cancel();
            return Empty{};
          });
        }
      });
  if (!on_done_added) {
    return absl::CancelledError();
  }
  stream_map_.emplace(stream_id, call_initiator);
  return absl::OkStatus();
}

void ChaoticGoodServerTransport::PerformOp(grpc_transport_op* op) {
  RefCountedPtr<Party> cancelled_party;
  bool close_outgoing_frames = false;
  auto cleanup = absl::MakeCleanup([&close_outgoing_frames, this]() {
    if (close_outgoing_frames) {
      outgoing_frames_.MarkClosed();
    }
  });
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
    cancelled_party = std::move(party_);
    close_outgoing_frames = true;
    state_tracker_.SetState(GRPC_CHANNEL_SHUTDOWN,
                            absl::UnavailableError("transport closed"),
                            "transport closed");
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
