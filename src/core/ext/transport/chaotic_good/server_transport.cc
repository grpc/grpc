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
#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chaotic_good/frame_transport.h"
#include "src/core/ext/transport/chaotic_good/message_chunker.h"
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

auto ChaoticGoodServerTransport::StreamDispatch::PushFrameIntoCall(
    RefCountedPtr<Stream> stream, MessageFrame frame) {
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: PushFrameIntoCall: frame=" << frame.ToString();
  return stream->message_reassembly.PushFrameInto(std::move(frame),
                                                  stream->call);
}

auto ChaoticGoodServerTransport::StreamDispatch::PushFrameIntoCall(
    RefCountedPtr<Stream> stream, BeginMessageFrame frame) {
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: PushFrameIntoCall: frame=" << frame.ToString();
  return stream->message_reassembly.PushFrameInto(std::move(frame),
                                                  stream->call);
}

auto ChaoticGoodServerTransport::StreamDispatch::PushFrameIntoCall(
    RefCountedPtr<Stream> stream, MessageChunkFrame frame) {
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD: PushFrameIntoCall: frame=" << frame.ToString();
  return stream->message_reassembly.PushFrameInto(std::move(frame),
                                                  stream->call);
}

auto ChaoticGoodServerTransport::StreamDispatch::PushFrameIntoCall(
    RefCountedPtr<Stream> stream, ClientEndOfStream) {
  if (stream->message_reassembly.in_message_boundary()) {
    stream->call.FinishSends();
    // Note that we cannot remove from the stream map yet, as we
    // may yet receive a cancellation.
    return Immediate(StatusFlag{Success{}});
  } else {
    stream->message_reassembly.FailCall(
        stream->call, "Received end of stream before end of chunked message");
    return Immediate(StatusFlag{Failure{}});
  }
}

template <typename T>
void ChaoticGoodServerTransport::StreamDispatch::DispatchFrame(
    IncomingFrame frame) {
  auto stream = LookupStream(frame.header().stream_id);
  if (stream == nullptr) return;
  stream->spawn_serializer->Spawn(
      [this, stream, frame = std::move(frame)]() mutable {
        DCHECK_NE(stream.get(), nullptr);
        auto& call = stream->call;
        return call.CancelIfFails(call.UntilCallCompletes(TrySeq(
            frame.Payload(),
            [stream = std::move(stream), this](Frame frame) mutable {
              return PushFrameIntoCall(std::move(stream),
                                       std::move(std::get<T>(frame)));
            },
            []() { return absl::OkStatus(); })));
      });
}

auto ChaoticGoodServerTransport::StreamDispatch::SendCallBody(
    uint32_t stream_id, CallInitiator call_initiator) {
  // Continuously send client frame with client to server messages.
  return ForEach(MessagesFrom(call_initiator),
                 [this, stream_id](MessageHandle message) mutable {
                   return message_chunker_.Send(std::move(message), stream_id,
                                                outgoing_frames_);
                 });
}

auto ChaoticGoodServerTransport::StreamDispatch::SendCallInitialMetadataAndBody(
    uint32_t stream_id, CallInitiator call_initiator) {
  return TrySeq(
      // Wait for initial metadata then send it out.
      call_initiator.PullServerInitialMetadata(),
      [stream_id, call_initiator,
       this](std::optional<ServerMetadataHandle> md) mutable {
        GRPC_TRACE_LOG(chaotic_good, INFO)
            << "CHAOTIC_GOOD: SendCallInitialMetadataAndBody: md="
            << (md.has_value() ? (*md)->DebugString() : "null");
        return If(
            md.has_value(),
            [&md, stream_id, &call_initiator, this]() {
              ServerInitialMetadataFrame frame;
              frame.body = ServerMetadataProtoFromGrpc(**md);
              frame.stream_id = stream_id;
              return TrySeq(outgoing_frames_.Send(std::move(frame)),
                            SendCallBody(stream_id, call_initiator));
            },
            []() { return StatusFlag(true); });
      });
}

auto ChaoticGoodServerTransport::StreamDispatch::CallOutboundLoop(
    uint32_t stream_id, CallInitiator call_initiator) {
  return GRPC_LATENT_SEE_PROMISE(
      "CallOutboundLoop",
      Seq(Map(SendCallInitialMetadataAndBody(stream_id, call_initiator),
              [stream_id](StatusFlag main_body_result) {
                GRPC_TRACE_VLOG(chaotic_good, 2)
                    << "CHAOTIC_GOOD: CallOutboundLoop: stream_id=" << stream_id
                    << " main_body_result=" << main_body_result;
                return Empty{};
              }),
          call_initiator.PullServerTrailingMetadata(),
          [outgoing_frames = outgoing_frames_,
           stream_id](ServerMetadataHandle md) mutable {
            ServerTrailingMetadataFrame frame;
            frame.body = ServerMetadataProtoFromGrpc(*md);
            frame.stream_id = stream_id;
            return outgoing_frames.Send(std::move(frame));
          }));
}

absl::Status ChaoticGoodServerTransport::StreamDispatch::NewStream(
    uint32_t stream_id,
    ClientInitialMetadataFrame client_initial_metadata_frame) {
  auto md = ClientMetadataGrpcFromProto(client_initial_metadata_frame.body);
  if (!md.ok()) {
    return md.status();
  }
  RefCountedPtr<Arena> arena(call_arena_allocator_->MakeArena());
  arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      ctx_->event_engine.get());
  std::optional<CallInitiator> call_initiator;
  auto call = MakeCallPair(std::move(*md), std::move(arena));
  call_initiator.emplace(std::move(call.initiator));
  auto add_result = AddStream(stream_id, *call_initiator);
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

auto ChaoticGoodServerTransport::StreamDispatch::ProcessNextFrame(
    IncomingFrame incoming_frame) {
  return Switch(
      incoming_frame.header().type,
      Case<FrameType::kClientInitialMetadata>([&, this]() {
        return Map(
            TrySeq(
                incoming_frame.Payload(),
                [this, header = incoming_frame.header()](Frame frame) mutable {
                  return NewStream(
                      header.stream_id,
                      std::move(std::get<ClientInitialMetadataFrame>(frame)));
                }),
            [](absl::Status status) {
              if (!status.ok()) {
                LOG(ERROR) << "Failed to process client initial metadata: "
                           << status;
              }
            });
      }),
      Case<FrameType::kMessage>([&, this]() mutable {
        DispatchFrame<MessageFrame>(std::move(incoming_frame));
      }),
      Case<FrameType::kBeginMessage>([&, this]() mutable {
        DispatchFrame<BeginMessageFrame>(std::move(incoming_frame));
      }),
      Case<FrameType::kMessageChunk>([&, this]() mutable {
        DispatchFrame<MessageChunkFrame>(std::move(incoming_frame));
      }),
      Case<FrameType::kClientEndOfStream>([&, this]() mutable {
        DispatchFrame<ClientEndOfStream>(std::move(incoming_frame));
      }),
      Case<FrameType::kCancel>([&, this]() {
        auto stream = ExtractStream(incoming_frame.header().stream_id);
        GRPC_TRACE_LOG(chaotic_good, INFO)
            << "Cancel stream " << incoming_frame.header().stream_id
            << (stream != nullptr ? " (active)" : " (not found)");
        if (stream == nullptr) return;
        auto& c = stream->call;
        c.SpawnInfallible("cancel", [c]() mutable { c.Cancel(); });
      }),
      Default([&]() {
        LOG_EVERY_N_SEC(INFO, 10)
            << "Bad frame type: " << incoming_frame.header().ToString();
      }));
}

void ChaoticGoodServerTransport::StreamDispatch::OnIncomingFrame(
    IncomingFrame incoming_frame) {
  incoming_frame_spawner_->Spawn(
      [self = RefAsSubclass<StreamDispatch>(),
       incoming_frame = std::move(incoming_frame)]() mutable {
        return self->ProcessNextFrame(std::move(incoming_frame));
      });
}

ChaoticGoodServerTransport::ChaoticGoodServerTransport(
    const ChannelArgs& args, OrphanablePtr<FrameTransport> frame_transport,
    MessageChunker message_chunker)
    : state_{std::make_unique<ConstructionParameters>(args, message_chunker)},
      frame_transport_(std::move(frame_transport)) {}

ChaoticGoodServerTransport::StreamDispatch::StreamDispatch(
    const ChannelArgs& args, FrameTransport* frame_transport,
    MessageChunker message_chunker,
    RefCountedPtr<UnstartedCallDestination> call_destination)
    : ctx_(frame_transport->ctx()),
      call_arena_allocator_(MakeRefCounted<CallArenaAllocator>(
          args.GetObject<ResourceQuota>()
              ->memory_quota()
              ->CreateMemoryAllocator("chaotic-good"),
          1024)),
      call_destination_(std::move(call_destination)),
      message_chunker_(message_chunker) {
  CHECK(ctx_ != nullptr);
  auto party_arena = SimpleArenaAllocator(0)->MakeArena();
  party_arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      ctx_->event_engine.get());
  party_ = Party::Make(std::move(party_arena));
  incoming_frame_spawner_ = party_->MakeSpawnSerializer();
  MpscReceiver<Frame> outgoing_pipe(8);
  outgoing_frames_ = outgoing_pipe.MakeSender();
  frame_transport->Start(party_.get(), std::move(outgoing_pipe), Ref());
}

void ChaoticGoodServerTransport::SetCallDestination(
    RefCountedPtr<UnstartedCallDestination> call_destination) {
  auto construction_parameters =
      std::move(std::get<std::unique_ptr<ConstructionParameters>>(state_));
  state_ = MakeRefCounted<StreamDispatch>(
      construction_parameters->args, frame_transport_.get(),
      construction_parameters->message_chunker, std::move(call_destination));
}

void ChaoticGoodServerTransport::Orphan() {
  if (auto* p = std::get_if<RefCountedPtr<StreamDispatch>>(&state_);
      p != nullptr) {
    (*p)->OnFrameTransportClosed(absl::UnavailableError("Transport closed"));
  }
  frame_transport_.reset();
  state_ = Orphaned{};
  Unref();
}

void ChaoticGoodServerTransport::StreamDispatch::OnFrameTransportClosed(
    absl::Status) {
  // Mark transport as unavailable when the endpoint write/read failed.
  // Close all the available pipes.
  ReleasableMutexLock lock(&mu_);
  last_seen_new_stream_id_ = std::numeric_limits<uint32_t>::max();
  StreamMap stream_map = std::move(stream_map_);
  stream_map_.clear();
  state_tracker_.SetState(GRPC_CHANNEL_SHUTDOWN,
                          absl::UnavailableError("transport closed"),
                          "transport closed");
  lock.Release();
  for (auto& pair : stream_map) {
    auto stream = std::move(pair.second);
    auto& call = stream->call;
    call.SpawnInfallible("cancel", [stream = std::move(stream)]() mutable {
      stream->call.Cancel();
    });
  }
}

RefCountedPtr<ChaoticGoodServerTransport::Stream>
ChaoticGoodServerTransport::StreamDispatch::LookupStream(uint32_t stream_id) {
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD " << this << " LookupStream " << stream_id;
  MutexLock lock(&mu_);
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) return nullptr;
  return it->second;
}

RefCountedPtr<ChaoticGoodServerTransport::Stream>
ChaoticGoodServerTransport::StreamDispatch::ExtractStream(uint32_t stream_id) {
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD " << this << " ExtractStream " << stream_id;
  MutexLock lock(&mu_);
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) return nullptr;
  auto r = std::move(it->second);
  stream_map_.erase(it);
  return r;
}

absl::Status ChaoticGoodServerTransport::StreamDispatch::AddStream(
    uint32_t stream_id, CallInitiator call_initiator) {
  MutexLock lock(&mu_);
  GRPC_TRACE_LOG(chaotic_good, INFO)
      << "CHAOTIC_GOOD " << this << " NewStream " << stream_id
      << " last_seen_new_stream_id_=" << last_seen_new_stream_id_;
  auto it = stream_map_.find(stream_id);
  if (stream_id <= last_seen_new_stream_id_) {
    return absl::InternalError("Stream id is not increasing");
  }
  if (it != stream_map_.end()) {
    return absl::InternalError("Stream already exists");
  }
  const bool on_done_added = call_initiator.OnDone(
      [self = RefAsSubclass<StreamDispatch>(), stream_id](bool) {
        GRPC_TRACE_LOG(chaotic_good, INFO)
            << "CHAOTIC_GOOD " << self.get() << " OnDone " << stream_id;
        auto stream = self->ExtractStream(stream_id);
        if (stream != nullptr) {
          auto& call = stream->call;
          call.SpawnInfallible("cancel",
                               [stream = std::move(stream)]() mutable {
                                 stream->call.Cancel();
                               });
        }
      });
  if (!on_done_added) {
    return absl::CancelledError();
  }
  stream_map_.emplace(stream_id,
                      MakeRefCounted<Stream>(std::move(call_initiator)));
  return absl::OkStatus();
}

void ChaoticGoodServerTransport::StreamDispatch::StartConnectivityWatch(
    grpc_connectivity_state state,
    OrphanablePtr<ConnectivityStateWatcherInterface> watcher) {
  MutexLock lock(&mu_);
  state_tracker_.AddWatcher(state, std::move(watcher));
}

void ChaoticGoodServerTransport::StreamDispatch::StopConnectivityWatch(
    ConnectivityStateWatcherInterface* watcher) {
  MutexLock lock(&mu_);
  state_tracker_.RemoveWatcher(watcher);
}

void ChaoticGoodServerTransport::PerformOp(grpc_transport_op* op) {
  RefCountedPtr<Party> cancelled_party;
  bool did_stuff = false;
  auto stream_dispatch = [this]() {
    return std::get<RefCountedPtr<StreamDispatch>>(state_);
  };
  if (op->start_connectivity_watch != nullptr) {
    stream_dispatch()->StartConnectivityWatch(
        op->start_connectivity_watch_state,
        std::move(op->start_connectivity_watch));
    did_stuff = true;
  }
  if (op->stop_connectivity_watch != nullptr) {
    stream_dispatch()->StopConnectivityWatch(op->stop_connectivity_watch);
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
    stream_dispatch()->OnFrameTransportClosed(
        absl::UnavailableError("transport closed"));
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
