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

#include "src/core/call/request_buffer.h"

#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/try_seq.h"

namespace grpc_core {

auto RequestBuffer::PushClientToServerMessage(MessageHandle message) {
  return MatchMutable(
      &state_,
      [&message, this](Buffering* buffering) -> Poll<Empty> {
        buffering->client_to_server_messages.push_back(std::move(message));
        next_client_to_server_event_wait_set_.TakeWakeupSet().Wakeup();
        return Empty{};
      },
      [&message, this](Streaming* streaming) -> Poll<Empty> {
        if (streaming->pending_message != nullptr) {
          return Pending{};
        }
        streaming->pending_message = std::move(message);
        next_client_to_server_event_wait_set_.TakeWakeupSet().Wakeup();
        return Empty{};
      });
}

void RequestBuffer::Start(CallHandler handler) {
  handler.SpawnGuarded("read", [this, handler = std::move(handler)]() mutable {
    return TrySeq(
        handler.PullClientInitialMetadata(),
        [this, handler](ClientMetadataHandle client_initial_metadata) {
          auto& buffering = absl::get<Buffering>(state_);
          buffering.client_initial_metadata =
              std::move(client_initial_metadata);
          next_client_to_server_event_wait_set_.TakeWakeupSet().Wakeup();
          return ForEach(OutgoingMessages(handler),
                         [this](MessageHandle message) {
                           return PushClientToServerMessage(std::move(message));
                         });
        });
  });
}

CallHandler RequestBuffer::Proxy(uintptr_t key) {
  return MatchMutable(
      &state_, [key](Buffering* buffering) {},
      [key](Streaming*) {
        return MakeFailedCall(absl::CancelledError("Call already committed"));
      });
}

}  // namespace grpc_core
