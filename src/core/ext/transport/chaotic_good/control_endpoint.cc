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

#include "src/core/ext/transport/chaotic_good/control_endpoint.h"

#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/event_engine/extensions/channelz.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/try_seq.h"

namespace grpc_core {
namespace chaotic_good {

auto ControlEndpoint::Buffer::Pull() {
  return [this]() -> Poll<SliceBuffer> {
    Waker waker;
    auto cleanup = absl::MakeCleanup([&waker]() { waker.Wakeup(); });
    MutexLock lock(&mu_);
    if (queued_output_.Length() == 0) {
      flush_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
      return Pending{};
    }
    waker = std::move(write_waker_);
    return std::move(queued_output_);
  };
}

ControlEndpoint::ControlEndpoint(
    PromiseEndpoint endpoint, RefCountedPtr<TransportContext> ctx,
    std::shared_ptr<TcpZTraceCollector> ztrace_collector)
    : endpoint_(std::make_shared<PromiseEndpoint>(std::move(endpoint))),
      ctx_(std::move(ctx)),
      ztrace_collector_(std::move(ztrace_collector)) {
  if (ctx_->socket_node != nullptr) {
    auto* channelz_endpoint = grpc_event_engine::experimental::QueryExtension<
        grpc_event_engine::experimental::ChannelzExtension>(
        endpoint_->GetEventEngineEndpoint().get());
    if (channelz_endpoint != nullptr) {
      channelz_endpoint->SetSocketNode(ctx_->socket_node);
    }
  }
  auto arena = SimpleArenaAllocator(0)->MakeArena();
  arena->SetContext(ctx_->event_engine.get());
  write_party_ = Party::Make(arena);
  CHECK(ctx_->event_engine != nullptr);
  write_party_->arena()->SetContext(ctx_->event_engine.get());
  write_party_->Spawn(
      "flush-control",
      GRPC_LATENT_SEE_PROMISE(
          "FlushLoop", Loop([ztrace_collector = ztrace_collector_,
                             endpoint = endpoint_, buffer = buffer_]() {
            return AddErrorPrefix(
                "CONTROL_CHANNEL: ",
                TrySeq(
                    // Pull one set of buffered writes
                    buffer->Pull(),
                    // And write them
                    [endpoint, ztrace_collector,
                     buffer = buffer.get()](SliceBuffer flushing) {
                      GRPC_TRACE_LOG(chaotic_good, INFO)
                          << "CHAOTIC_GOOD: Flush " << flushing.Length()
                          << " bytes from " << buffer << " to "
                          << ResolvedAddressToString(endpoint->GetPeerAddress())
                                 .value_or("<<unknown peer address>>");
                      ztrace_collector->Append(
                          WriteBytesToControlChannelTrace{flushing.Length()});
                      return Map(
                          GRPC_LATENT_SEE_PROMISE(
                              "CtlEndpointWrite",
                              endpoint->Write(std::move(flushing),
                                              PromiseEndpoint::WriteArgs{})),
                          [ztrace_collector](absl::Status status) {
                            ztrace_collector->Append([&status]() {
                              return FinishWriteBytesToControlChannelTrace{
                                  status};
                            });
                            return status;
                          });
                    },
                    // Then repeat
                    []() -> LoopCtl<absl::Status> { return Continue{}; }));
          })),
      [](absl::Status) {});
}

}  // namespace chaotic_good
}  // namespace grpc_core
