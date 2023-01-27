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
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/event_engine_shims/tcp_client.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/time.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/resolved_address_internal.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_event_engine {
namespace experimental {

int64_t event_engine_tcp_client_connect(
    std::shared_ptr<EventEngine> event_engine, bool fd_support_exists,
    grpc_closure* on_connect, grpc_endpoint** endpoint,
    const grpc_event_engine::experimental::EndpointConfig& config,
    const grpc_resolved_address* addr, grpc_core::Timestamp deadline) {
  GPR_DEBUG_ASSERT(event_engine);
  auto resource_quota = reinterpret_cast<grpc_core::ResourceQuota*>(
      config.GetVoidPointer(GRPC_ARG_RESOURCE_QUOTA));
  auto addr_uri = grpc_sockaddr_to_uri(addr);
  EventEngine::ConnectionHandle handle = event_engine->Connect(
      [on_connect, endpoint, fd_support_exists](
          absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep) {
        grpc_core::ApplicationCallbackExecCtx app_ctx;
        grpc_core::ExecCtx exec_ctx;
        absl::Status conn_status = ep.ok() ? absl::OkStatus() : ep.status();
        if (ep.ok()) {
          *endpoint = grpc_event_engine_endpoint_create(std::move(*ep),
                                                        fd_support_exists);
        } else {
          *endpoint = nullptr;
        }
        GRPC_EVENT_ENGINE_TRACE("EventEngine::Connect Status: %s",
                                ep.status().ToString().c_str());
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_connect,
                                absl_status_to_grpc_error(conn_status));
      },
      CreateResolvedAddress(*addr), config,
      resource_quota != nullptr
          ? resource_quota->memory_quota()->CreateMemoryOwner(
                absl::StrCat("tcp-client:", addr_uri.value()))
          : grpc_event_engine::experimental::MemoryAllocator(),
      std::max(grpc_core::Duration::Milliseconds(1),
               deadline - grpc_core::Timestamp::Now()));
  GRPC_EVENT_ENGINE_TRACE("EventEngine::Connect Peer: %s, handle: %" PRId64,
                          (*addr_uri).c_str(),
                          static_cast<int64_t>(handle.keys[0]));
  return handle.keys[0];
}

bool event_engine_tcp_client_cancel_connect(
    std::shared_ptr<EventEngine> event_engine, int64_t connection_handle) {
  GPR_DEBUG_ASSERT(event_engine);
  GRPC_EVENT_ENGINE_TRACE("EventEngine::CancelConnect handle: %" PRId64,
                          connection_handle);
  return event_engine->CancelConnect(
      {static_cast<intptr_t>(connection_handle), 0});
}
}  // namespace experimental
}  // namespace grpc_event_engine
