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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/call_spine.h"

namespace grpc_core {

void ForwardCall(CallHandler call_handler, CallInitiator call_initiator,
                 ClientMetadataHandle client_initial_metadata) {
  // Send initial metadata.
  call_initiator.SpawnGuarded(
      "send_initial_metadata",
      [client_initial_metadata = std::move(client_initial_metadata),
       call_initiator]() mutable {
        return call_initiator.PushClientInitialMetadata(
            std::move(client_initial_metadata));
      });
  // Read messages from handler into initiator.
  call_handler.SpawnGuarded("read_messages", [call_handler,
                                              call_initiator]() mutable {
    return Seq(ForEach(OutgoingMessages(call_handler),
                       [call_initiator](MessageHandle msg) mutable {
                         // Need to spawn a job into the initiator's activity to
                         // push the message in.
                         return call_initiator.SpawnWaitable(
                             "send_message",
                             [msg = std::move(msg), call_initiator]() mutable {
                               return call_initiator.CancelIfFails(
                                   call_initiator.PushMessage(std::move(msg)));
                             });
                       }),
               [call_initiator](StatusFlag result) mutable {
                 if (result.ok()) {
                   call_initiator.SpawnInfallible(
                       "finish-downstream-ok", [call_initiator]() mutable {
                         call_initiator.FinishSends();
                         return Empty{};
                       });
                 } else {
                   call_initiator.SpawnInfallible("finish-downstream-fail",
                                                  [call_initiator]() mutable {
                                                    call_initiator.Cancel();
                                                    return Empty{};
                                                  });
                 }
                 return result;
               });
  });
  call_initiator.SpawnInfallible("read_the_things", [call_initiator,
                                                     call_handler]() mutable {
    return Seq(
        call_initiator.CancelIfFails(TrySeq(
            call_initiator.PullServerInitialMetadata(),
            [call_handler,
             call_initiator](absl::optional<ServerMetadataHandle> md) mutable {
              const bool has_md = md.has_value();
              call_handler.SpawnGuarded(
                  "recv_initial_metadata",
                  [md = std::move(md), call_handler]() mutable {
                    return call_handler.PushServerInitialMetadata(
                        std::move(md));
                  });
              return If(
                  has_md,
                  ForEach(OutgoingMessages(call_initiator),
                          [call_handler](MessageHandle msg) mutable {
                            return call_handler.SpawnWaitable(
                                "recv_message",
                                [msg = std::move(msg), call_handler]() mutable {
                                  return call_handler.CancelIfFails(
                                      call_handler.PushMessage(std::move(msg)));
                                });
                          }),
                  []() -> StatusFlag { return Success{}; });
            })),
        call_initiator.PullServerTrailingMetadata(),
        [call_handler](ServerMetadataHandle md) mutable {
          call_handler.SpawnGuarded(
              "recv_trailing_metadata",
              [md = std::move(md), call_handler]() mutable {
                return call_handler.PushServerTrailingMetadata(std::move(md));
              });
          return Empty{};
        });
  });
}

CallInitiatorAndHandler MakeCall(
    grpc_event_engine::experimental::EventEngine* event_engine, Arena* arena) {
  auto spine = CallSpine::Create(event_engine, arena);
  return {CallInitiator(spine), CallHandler(spine)};
}

}  // namespace grpc_core
