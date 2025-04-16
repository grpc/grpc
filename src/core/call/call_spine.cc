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

#include "src/core/call/call_spine.h"

#include <grpc/support/port_platform.h>

#include "absl/functional/any_invocable.h"
#include "src/core/call/metadata.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/try_seq.h"

namespace grpc_core {

void CallHandler::ForwardTo(CallInitiator call_initiator,
                            absl::AnyInvocable<void(ServerMetadata&)>
                                on_server_trailing_metadata_from_initiator) {
  spine_->AddChildCall(call_initiator.spine_);
  // Read messages from handler into initiator.
  SpawnInfallible(
      "read_messages", [call_handler = *this, call_initiator]() mutable {
        return Seq(
            ForEach(MessagesFrom(call_handler),
                    [call_initiator](MessageHandle msg) mutable {
                      // Need to spawn a job into the initiator's activity to
                      // push the message in.
                      call_initiator.SpawnPushMessage(std::move(msg));
                      return Success{};
                    }),
            [call_initiator]() mutable { call_initiator.SpawnFinishSends(); });
      });
  call_initiator.SpawnInfallible(
      "read_the_things",
      [call_initiator, call_handler = *this,
       on_server_trailing_metadata_from_initiator =
           std::move(on_server_trailing_metadata_from_initiator)]() mutable {
        return Seq(
            call_initiator.CancelIfFails(TrySeq(
                call_initiator.PullServerInitialMetadata(),
                [call_handler, call_initiator](
                    std::optional<ServerMetadataHandle> md) mutable {
                  const bool has_md = md.has_value();
                  return If(
                      has_md,
                      [&call_handler, &call_initiator,
                       md = std::move(md)]() mutable {
                        call_handler.SpawnPushServerInitialMetadata(
                            std::move(*md));
                        return ForEach(
                            MessagesFrom(call_initiator),
                            [call_handler](MessageHandle msg) mutable {
                              call_handler.SpawnPushMessage(std::move(msg));
                              return Success{};
                            });
                      },
                      []() -> StatusFlag { return Success{}; });
                })),
            call_initiator.PullServerTrailingMetadata(),
            [call_handler,
             on_server_trailing_metadata_from_initiator =
                 std::move(on_server_trailing_metadata_from_initiator)](
                ServerMetadataHandle md) mutable {
              on_server_trailing_metadata_from_initiator(*md);
              call_handler.SpawnPushServerTrailingMetadata(std::move(md));
            });
      });
}

CallInitiatorAndHandler MakeCallPair(
    ClientMetadataHandle client_initial_metadata,
    CallArenaSource arena_source) {
  auto arena = arena_source.Take();
  DCHECK_NE(arena.get(), nullptr);
  DCHECK_NE(arena->GetContext<grpc_event_engine::experimental::EventEngine>(),
            nullptr);
  auto spine =
      CallSpine::Create(std::move(client_initial_metadata), std::move(arena));
  return {CallInitiator(spine), UnstartedCallHandler(spine)};
}

CallInitiatorAndHandler CallHandler::MakeChildCall(
    ClientMetadataHandle client_initial_metadata,
    CallArenaSource arena_source) {
  auto result =
      MakeCallPair(std::move(client_initial_metadata), std::move(arena_source));
  result.handler.arena()->ForwardPropagateContextFrom(arena());
  spine_->AddChildCall(result.handler.spine_);
  return result;
}

}  // namespace grpc_core
