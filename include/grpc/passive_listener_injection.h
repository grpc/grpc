// Copyright 2024 The gRPC Authors
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
#ifndef GRPC_PASSIVE_LISTENER_INJECTION_H
#define GRPC_PASSIVE_LISTENER_INJECTION_H

#include <grpc/support/port_platform.h>

#include "absl/status/status.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

namespace grpc_core {
class ListenerInterface;
class Server;
namespace experimental {
class PassiveListenerImpl;
}  // namespace experimental
}  // namespace grpc_core

absl::Status grpc_server_add_passive_listener(
    grpc_core::Server* server, grpc_server_credentials* credentials,
    grpc_core::experimental::PassiveListenerImpl& passive_listener);

// Called to add an endpoint to passive_listener.
absl::Status grpc_server_accept_connected_endpoint(
    grpc_core::Server* server, grpc_core::ListenerInterface* core_listener,
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint);

#endif /* GRPC_PASSIVE_LISTENER_INJECTION_H */
