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

#include "src/core/ext/transport/chaotic_good/client_transport.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/port_platform.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chaotic_good/frame_transport.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/switch.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {
namespace chaotic_good {

ChaoticGoodClientTransport::StreamDispatch::StreamDispatch(
    MpscSender<Frame> outgoing_frames)
    : outgoing_frames_(std::move(outgoing_frames)) {}

RefCountedPtr<ChaoticGoodClientTransport::Stream>
ChaoticGoodClientTransport::StreamDispatch::LookupStream(uint32_t stream_id) {
  MutexLock lock(&mu_);
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    return nullptr;
  }
  return it->second;
}

auto ChaoticGoodClientTransport::StreamDispatch::PushFrameIntoCall(
    ServerInitialMetadataFrame frame, RefCountedPtr<Stream> stream) {
  DCHECK(stream->message_reassembly.in_message_boundary());
  auto headers = ServerMetadataGrpcFromProto(frame.body);
  if (!headers.ok()) {
    LOG_EVERY_N_SEC(INFO, 10) << "Encode headers failed: " << headers.status();
    return Immediate(StatusFlag(Failure{}));
  }
  return Immediate(stream->call.PushServerInitialMetadata(std::move(*headers)));
}

auto ChaoticGoodClientTransport::StreamDispatch::PushFrameIntoCall(
    MessageFrame frame, RefCountedPtr<Stream> stream) {
  return stream->message_reassembly.PushFrameInto(std::move(frame),
                                                  stream->call);
}

auto ChaoticGoodClientTransport::StreamDispatch::PushFrameIntoCall(
    BeginMessageFrame frame, RefCountedPtr<Stream> stream) {
  return stream->message_reassembly.PushFrameInto(std::move(frame),
                                                  stream->call);
}

auto ChaoticGoodClientTransport::StreamDispatch::PushFrameIntoCall(
    MessageChunkFrame frame, RefCountedPtr<Stream> stream) {
  return stream->message_reassembly.PushFrameInto(std::move(frame),
                                                  stream->call);
}

auto ChaoticGoodClientTransport::StreamDispatch::PushFrameIntoCall(
    ServerTrailingMetadataFrame frame, RefCountedPtr<Stream> stream) {
  auto trailers = ServerMetadataGrpcFromProto(frame.body);
  if (!trailers.ok()) {
    stream->call.PushServerTrailingMetadata(
        CancelledServerMetadataFromStatus(trailers.status()));
  } else if (!stream->message_reassembly.in_message_boundary() &&
             (*trailers)
                     ->get(GrpcStatusMetadata())
                     .value_or(GRPC_STATUS_UNKNOWN) == GRPC_STATUS_OK) {
    stream->call.PushServerTrailingMetadata(CancelledServerMetadataFromStatus(
        GRPC_STATUS_INTERNAL,
        "End of call received while still receiving last message - this is a "
        "protocol error"));
  } else {
    stream->call.PushServerTrailingMetadata(std::move(*trailers));
  }
  return Immediate(Success{});
}

template <typename T>
void ChaoticGoodClientTransport::StreamDispatch::DispatchFrame(
    IncomingFrame incoming_frame) {
  auto stream = LookupStream(incoming_frame.header().stream_id);
  if (stream == nullptr) return;
  stream->frame_dispatch_serializer->Spawn(
      [stream = std::move(stream),
       incoming_frame = std::move(incoming_frame)]() mutable {
        return Map(stream->call.CancelIfFails(TrySeq(
                       incoming_frame.Payload(),
                       [stream = std::move(stream)](Frame frame) mutable {
                         auto& call = stream->call;
                         return Map(call.CancelIfFails(PushFrameIntoCall(
                                        std::move(std::get<T>(frame)),
                                        std::move(stream))),
                                    [](auto) { return absl::OkStatus(); });
                       })),
                   [](auto) {});
      });
}

void ChaoticGoodClientTransport::StreamDispatch::OnIncomingFrame(
    IncomingFrame incoming_frame) {
  switch (incoming_frame.header().type) {
    case FrameType::kServerInitialMetadata:
      DispatchFrame<ServerInitialMetadataFrame>(std::move(incoming_frame));
      break;
    case FrameType::kServerTrailingMetadata:
      DispatchFrame<ServerTrailingMetadataFrame>(std::move(incoming_frame));
      break;
    case FrameType::kMessage:
      DispatchFrame<MessageFrame>(std::move(incoming_frame));
      break;
    case FrameType::kBeginMessage:
      DispatchFrame<BeginMessageFrame>(std::move(incoming_frame));
      break;
    case FrameType::kMessageChunk:
      DispatchFrame<MessageChunkFrame>(std::move(incoming_frame));
      break;
    default:
      LOG_EVERY_N_SEC(INFO, 10)
          << "Unhandled frame of type: " << incoming_frame.header().type;
  }
}

void ChaoticGoodClientTransport::StreamDispatch::OnFrameTransportClosed(
    absl::Status) {
  // Mark transport as unavailable when the endpoint write/read failed.
  ReleasableMutexLock lock(&mu_);
  StreamMap stream_map = std::move(stream_map_);
  stream_map_.clear();
  next_stream_id_ = kClosedTransportStreamId;
  state_tracker_.SetState(GRPC_CHANNEL_SHUTDOWN,
                          absl::UnavailableError("transport closed"),
                          "transport closed");
  lock.Release();
  for (auto& pair : stream_map) {
    auto stream = std::move(pair.second);
    auto& call = stream->call;
    call.SpawnInfallible("cancel", [stream = std::move(stream)]() mutable {
      stream->call.PushServerTrailingMetadata(ServerMetadataFromStatus(
          absl::UnavailableError("Transport closed.")));
    });
  }
}

uint32_t ChaoticGoodClientTransport::StreamDispatch::MakeStream(
    CallHandler call_handler) {
  MutexLock lock(&mu_);
  if (next_stream_id_ == kClosedTransportStreamId) return 0;
  const uint32_t stream_id = next_stream_id_++;
  const bool on_done_added = call_handler.OnDone(
      [self = RefAsSubclass<StreamDispatch>(), stream_id](bool cancelled) {
        GRPC_TRACE_LOG(chaotic_good, INFO)
            << "CHAOTIC_GOOD: Client call " << self.get() << " id=" << stream_id
            << " done: cancelled=" << cancelled;
        if (cancelled) {
          self->outgoing_frames_.UnbufferedImmediateSend(
              CancelFrame{stream_id});
        }
        MutexLock lock(&self->mu_);
        self->stream_map_.erase(stream_id);
      });
  if (!on_done_added) return 0;
  stream_map_.emplace(stream_id,
                      MakeRefCounted<Stream>(std::move(call_handler)));
  return stream_id;
}

void ChaoticGoodClientTransport::StreamDispatch::StartConnectivityWatch(
    grpc_connectivity_state state,
    OrphanablePtr<ConnectivityStateWatcherInterface> watcher) {
  MutexLock lock(&mu_);
  state_tracker_.AddWatcher(state, std::move(watcher));
}

void ChaoticGoodClientTransport::StreamDispatch::StopConnectivityWatch(
    ConnectivityStateWatcherInterface* watcher) {
  MutexLock lock(&mu_);
  state_tracker_.RemoveWatcher(watcher);
}

ChaoticGoodClientTransport::ChaoticGoodClientTransport(
    const ChannelArgs& args, OrphanablePtr<FrameTransport> frame_transport,
    MessageChunker message_chunker)
    : ctx_(frame_transport->ctx()),
      allocator_(args.GetObject<ResourceQuota>()
                     ->memory_quota()
                     ->CreateMemoryAllocator("chaotic-good")),
      message_chunker_(message_chunker),
      frame_transport_(std::move(frame_transport)) {
  CHECK(ctx_ != nullptr);
  auto party_arena = SimpleArenaAllocator(0)->MakeArena();
  party_arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      ctx_->event_engine.get());
  party_ = Party::Make(std::move(party_arena));
  MpscReceiver<Frame> outgoing_frames{8};
  outgoing_frames_ = outgoing_frames.MakeSender();
  stream_dispatch_ =
      MakeRefCounted<StreamDispatch>(outgoing_frames.MakeSender());
  frame_transport_->Start(party_.get(), std::move(outgoing_frames),
                          stream_dispatch_);
}

ChaoticGoodClientTransport::~ChaoticGoodClientTransport() { party_.reset(); }

void ChaoticGoodClientTransport::Orphan() {
  stream_dispatch_->OnFrameTransportClosed(
      absl::UnavailableError("Transport closed"));
  party_.reset();
  frame_transport_.reset();
  Unref();
}

auto ChaoticGoodClientTransport::CallOutboundLoop(uint32_t stream_id,
                                                  CallHandler call_handler) {
  auto send_fragment = [this, stream_id](auto frame) mutable {
    frame.stream_id = stream_id;
    return outgoing_frames_.Send(std::move(frame));
  };
  auto send_message = [this, stream_id, message_chunker = message_chunker_](
                          MessageHandle message) mutable {
    return message_chunker.Send(std::move(message), stream_id,
                                outgoing_frames_);
  };
  return GRPC_LATENT_SEE_PROMISE(
      "CallOutboundLoop",
      TrySeq(
          // Wait for initial metadata then send it out.
          call_handler.PullClientInitialMetadata(),
          [send_fragment](ClientMetadataHandle md) mutable {
            GRPC_TRACE_LOG(chaotic_good, INFO)
                << "CHAOTIC_GOOD: Sending initial metadata: "
                << md->DebugString();
            ClientInitialMetadataFrame frame;
            frame.body = ClientMetadataProtoFromGrpc(*md);
            return send_fragment(std::move(frame));
          },
          // Continuously send client frame with client to server messages.
          ForEach(MessagesFrom(call_handler), std::move(send_message)),
          [send_fragment]() mutable {
            ClientEndOfStream frame;
            return send_fragment(std::move(frame));
          },
          [call_handler]() mutable {
            return Map(call_handler.WasCancelled(),
                       [](bool cancelled) { return StatusFlag(!cancelled); });
          }));
}

void ChaoticGoodClientTransport::StartCall(CallHandler call_handler) {
  // At this point, the connection is set up.
  // Start sending data frames.
  call_handler.SpawnGuarded(
      "outbound_loop", [self = RefAsSubclass<ChaoticGoodClientTransport>(),
                        call_handler]() mutable {
        const uint32_t stream_id =
            self->stream_dispatch_->MakeStream(call_handler);
        return If(
            stream_id != 0,
            [stream_id, &call_handler, self = std::move(self)]() mutable {
              return Map(
                  self->CallOutboundLoop(stream_id, std::move(call_handler)),
                  [self, stream_id](StatusFlag result) -> StatusFlag {
                    GRPC_TRACE_LOG(chaotic_good, INFO)
                        << "CHAOTIC_GOOD: Call " << stream_id
                        << " finished with " << result.ToString();
                    if (!result.ok()) {
                      GRPC_TRACE_LOG(chaotic_good, INFO)
                          << "CHAOTIC_GOOD: Send cancel";
                      if (!self->outgoing_frames_
                               .UnbufferedImmediateSend(CancelFrame{stream_id})
                               .ok()) {
                        GRPC_TRACE_LOG(chaotic_good, INFO)
                            << "CHAOTIC_GOOD: Send cancel failed";
                      }
                    }
                    return result;
                  });
            },
            [&call_handler]() {
              call_handler.PushServerTrailingMetadata(
                  CancelledServerMetadataFromStatus(
                      absl::UnavailableError("Transport closed.")));
              return []() -> Poll<StatusFlag> { return Success{}; };
            });
      });
}

void ChaoticGoodClientTransport::PerformOp(grpc_transport_op* op) {
  bool did_stuff = false;
  if (op->start_connectivity_watch != nullptr) {
    stream_dispatch_->StartConnectivityWatch(
        op->start_connectivity_watch_state,
        std::move(op->start_connectivity_watch));
    did_stuff = true;
  }
  if (op->stop_connectivity_watch != nullptr) {
    stream_dispatch_->StopConnectivityWatch(op->stop_connectivity_watch);
    did_stuff = true;
  }
  if (op->set_accept_stream) {
    Crash("set_accept_stream not supported on clients");
  }
  if (!did_stuff) {
    Crash(absl::StrCat("unimplemented transport perform op: ",
                       grpc_transport_op_string(op)));
  }
  ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
}

}  // namespace chaotic_good
}  // namespace grpc_core
