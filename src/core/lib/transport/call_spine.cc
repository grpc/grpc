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

#include "src/core/lib/transport/call_spine.h"

#include <grpc/support/port_platform.h>

#include "absl/functional/any_invocable.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/try_seq.h"

namespace grpc_core {

void ForwardCall(CallHandler call_handler, CallInitiator call_initiator,
                 absl::AnyInvocable<void(ServerMetadata&)>
                     on_server_trailing_metadata_from_initiator) {
  // Read messages from handler into initiator.
  call_handler.SpawnInfallible(
      "read_messages", [call_handler, call_initiator]() mutable {
        return Seq(
            ForEach(OutgoingMessages(call_handler),
                    [call_initiator](MessageHandle msg) mutable {
                      // Need to spawn a job into the initiator's activity to
                      // push the message in.
                      return call_initiator.SpawnWaitable(
                          "send_message",
                          [msg = std::move(msg), call_initiator]() mutable {
                            return call_initiator.PushMessage(std::move(msg));
                          });
                    }),
            [call_initiator](StatusFlag result) mutable {
              if (result.ok()) {
                call_initiator.SpawnInfallible("finish-downstream-ok",
                                               [call_initiator]() mutable {
                                                 call_initiator.FinishSends();
                                               });
              }
            });
      });
  call_handler.SpawnInfallible(
      "check_cancellation", [call_handler, call_initiator]() mutable {
        return Map(call_handler.WasCancelled(), [call_initiator =
                                                     std::move(call_initiator)](
                                                    bool cancelled) mutable {
          if (cancelled) {
            call_initiator.SpawnInfallible("propagate_handler_cancel",
                                           [call_initiator]() mutable {
                                             call_initiator.Cancel();
                                             return Empty();
                                           });
          }
          return Empty();
        });
      });
  call_initiator.SpawnInfallible(
      "read_the_things",
      [call_initiator, call_handler,
       on_server_trailing_metadata_from_initiator =
           std::move(on_server_trailing_metadata_from_initiator)]() mutable {
        return Seq(
            call_initiator.CancelIfFails(TrySeq(
                call_initiator.PullServerInitialMetadata(),
                [call_handler, call_initiator](
                    absl::optional<ServerMetadataHandle> md) mutable {
                  const bool has_md = md.has_value();
                  return If(
                      has_md,
                      [&call_handler, &call_initiator,
                       md = std::move(md)]() mutable {
                        call_handler.SpawnGuarded(
                            "recv_initial_metadata",
                            [md = std::move(*md), call_handler]() mutable {
                              return call_handler.PushServerInitialMetadata(
                                  std::move(md));
                            });
                        return ForEach(
                            OutgoingMessages(call_initiator),
                            [call_handler](MessageHandle msg) mutable {
                              return call_handler.SpawnWaitable(
                                  "recv_message", [msg = std::move(msg),
                                                   call_handler]() mutable {
                                    return call_handler.CancelIfFails(
                                        call_handler.PushMessage(
                                            std::move(msg)));
                                  });
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
              call_handler.SpawnInfallible(
                  "recv_trailing",
                  [call_handler, md = std::move(md)]() mutable {
                    call_handler.PushServerTrailingMetadata(std::move(md));
                  });
            });
      });
}

CallInitiatorAndHandler MakeCallPair(
    ClientMetadataHandle client_initial_metadata, RefCountedPtr<Arena> arena) {
  DCHECK_NE(arena.get(), nullptr);
  DCHECK_NE(arena->GetContext<grpc_event_engine::experimental::EventEngine>(),
            nullptr);
  auto spine =
      CallSpine::Create(std::move(client_initial_metadata), std::move(arena));
  return {CallInitiator(spine), UnstartedCallHandler(spine)};
}

}  // namespace grpc_core
