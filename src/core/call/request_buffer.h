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

#ifndef REQUEST_BUFFER_H
#define REQUEST_BUFFER_H

#include "src/core/lib/promise/wait_set.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/message.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

class RequestBuffer {
 public:
  void Start(CallHandler handler);
  CallHandler Proxy(uintptr_t key);
  void Commit(uintptr_t key);

 private:
  struct Buffering {
    ClientMetadataHandle client_initial_metadata;
    absl::InlinedVector<MessageHandle, 1> client_to_server_messages;
    WaitSet become_streaming;
  };
  struct Streaming {
    uintptr_t winner;
    MessageHandle pending_message;
    Waker pending_message_sent;
  };

  auto PushClientToServerMessage(MessageHandle message);
  virtual void OnTrailersOnlyResponse(uintptr_t key,
                                      ServerMetadata& trailers) = 0;

  using State = absl::variant<Buffering, Streaming>;
  State state_{Buffering{}};
  WaitSet next_client_to_server_event_wait_set_;
};

}  // namespace grpc_core

#endif
