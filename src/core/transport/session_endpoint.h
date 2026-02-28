//
//
// Copyright 2026 gRPC authors.
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
//
//

#ifndef GRPC_SRC_CORE_TRANSPORT_SESSION_ENDPOINT_H
#define GRPC_SRC_CORE_TRANSPORT_SESSION_ENDPOINT_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/grpc_types.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace grpc_core {

struct SessionEndpointTag {
  grpc_closure closure;
  absl::AnyInvocable<void(bool)> callback;
};

class SessionEndpoint
    : public grpc_event_engine::experimental::EventEngine::Endpoint {
 public:
  static grpc_endpoint* Create(grpc_call* call, bool is_client);

  SessionEndpoint(grpc_call* call, bool is_client);
  ~SessionEndpoint() override;

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
            grpc_event_engine::experimental::SliceBuffer* buffer,
            ReadArgs args) override;

  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             grpc_event_engine::experimental::SliceBuffer* data,
             WriteArgs args) override;

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetPeerAddress() const {
    // TODO(snohria): Implement this.
    return peer_address_;
  }

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddress() const {
    // TODO(snohria): Implement this.
    return local_address_;
  }

  std::shared_ptr<TelemetryInfo> GetTelemetryInfo() const override {
    // TODO(snohria): Implement this.
    return nullptr;
  }

 private:
  struct State {
    State(grpc_call* call, bool is_client);
    ~State();
    grpc_call* const call;
    std::atomic<bool> shutdown{false};
    const bool is_client;
    SessionEndpointTag read_tag;
    grpc_byte_buffer* read_buffer = nullptr;
    std::atomic<bool> read_in_progress{false};
    SessionEndpointTag write_tag;
    std::atomic<bool> write_in_progress{false};
  };

  std::shared_ptr<State> state_;
  bool is_client_;
  grpc_event_engine::experimental::EventEngine::ResolvedAddress local_address_;
  grpc_event_engine::experimental::EventEngine::ResolvedAddress peer_address_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TRANSPORT_SESSION_ENDPOINT_H
