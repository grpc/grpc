// Copyright 2022 The gRPC Authors
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
#ifndef GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_SHIMS_ENDPOINT_H
#define GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_SHIMS_ENDPOINT_H
#include <grpc/support/port_platform.h>

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP
typedef struct grpc_fd grpc_fd;
#endif

namespace grpc_event_engine {
namespace experimental {

struct grpc_event_engine_endpoint {
  grpc_endpoint base;
  std::unique_ptr<EventEngine::Endpoint> endpoint;
  std::string peer_address;
  std::string local_address;
  std::aligned_storage<sizeof(SliceBuffer), alignof(SliceBuffer)>::type
      read_buffer;
  std::aligned_storage<sizeof(SliceBuffer), alignof(SliceBuffer)>::type
      write_buffer;
};

grpc_event_engine_endpoint* grpc_event_engine_tcp_server_endpoint_create(
    std::unique_ptr<EventEngine::Endpoint> ee);

#ifdef GRPC_POSIX_SOCKET_TCP
grpc_endpoint* CreatePosixIomgrEndpiont(
    grpc_fd* wrapped_fd,
    const grpc_event_engine::experimental::EndpointConfig& endpoint_config,
    absl::string_view peer_string);
#endif

}  // namespace experimental
}  // namespace grpc_event_engine

/// Creates an internal grpc_endpoint struct from an EventEngine Endpoint.
/// Server code needs to create grpc_endpoints after the EventEngine has made
/// connections.

#endif  // GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_SHIMS_ENDPOINT_H
