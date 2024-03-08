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
#ifndef GRPC_EVENT_ENGINE_PASSIVE_LISTENER_INJECTION_H
#define GRPC_EVENT_ENGINE_PASSIVE_LISTENER_INJECTION_H

#include <grpc/support/port_platform.h>

#include "absl/status/status.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

/** Add the connected endpoint to the 'server' and server credentials 'creds'.
    The endpoint must be connected already. The server's EventEngine will be
    associated with the Endpoint, and the standard http2 handshake process will
    occur. */
GRPCAPI void grpc_server_add_passive_listener_endpoint(
    grpc_server* server,
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint,
    grpc_server_credentials* creds);

/** Add the connected fd to the 'server' and server credentials 'creds'.
    The fd must be connected already. The server's EventEngine will be
    associated with a new Endpoint created from that fd, and the standard http2
    handshake process will occur. */
GRPCAPI absl::Status grpc_server_add_passive_listener_connected_fd(
    grpc_server* server, int fd, grpc_server_credentials* creds,
    grpc_channel_args* server_args);

#endif /* GRPC_EVENT_ENGINE_PASSIVE_LISTENER_INJECTION_H */
