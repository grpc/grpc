// Copyright 2021 The gRPC Authors
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
#ifndef GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_ENDPOINT_H
#define GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_ENDPOINT_H

#include <grpc/support/port_platform.h>

#ifdef GRPC_USE_EVENT_ENGINE
#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resource_quota.h"

struct grpc_event_engine_endpoint {
  grpc_endpoint base;
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
      endpoint;
  std::string peer_address;
  std::string local_address;
  std::aligned_storage<
      sizeof(grpc_event_engine::experimental::SliceBuffer),
      alignof(grpc_event_engine::experimental::SliceBuffer)>::type read_buffer;
  std::aligned_storage<
      sizeof(grpc_event_engine::experimental::SliceBuffer),
      alignof(grpc_event_engine::experimental::SliceBuffer)>::type write_buffer;
};

/// Creates an internal grpc_endpoint struct from an EventEngine Endpoint.
/// Server code needs to create grpc_endpoints after the EventEngine has made
/// connections.
grpc_event_engine_endpoint* grpc_tcp_server_endpoint_create(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint> ee);

/// Creates a new internal grpc_endpoint struct, when no EventEngine Endpoint
/// has yet been created. This is used in client code before connections are
/// established. Takes ownership of the slice_allocator.
grpc_endpoint* grpc_tcp_create(const grpc_channel_args* channel_args,
                               absl::string_view peer_address);

#endif
#endif  // GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_ENDPOINT_H
