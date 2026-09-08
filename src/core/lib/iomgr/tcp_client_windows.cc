//
//
// Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>
#include <inttypes.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log_windows.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/shim.h"
#include "src/core/lib/iomgr/event_engine_shims/tcp_client.h"
#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_windows.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_windows.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/crash.h"
#include "src/core/util/grpc_check.h"

using ::grpc_event_engine::experimental::EndpointConfig;

// Tries to issue one async connection, then schedules both an IOCP
// notification request for the connection, and one timeout alert.
static int64_t tcp_connect(grpc_closure* on_done, grpc_endpoint** endpoint,
                           grpc_pollset_set* /* interested_parties */,
                           const EndpointConfig& config,
                           const grpc_resolved_address* addr,
                           grpc_core::Timestamp deadline) {
  return grpc_event_engine::experimental::event_engine_tcp_client_connect(
      on_done, endpoint, config, addr, deadline);
}

static bool tcp_cancel_connect(int64_t connection_handle) {
  return grpc_event_engine::experimental::
      event_engine_tcp_client_cancel_connect(connection_handle);
}

grpc_tcp_client_vtable grpc_windows_tcp_client_vtable = {tcp_connect,
                                                         tcp_cancel_connect};

#endif  // GRPC_WINSOCK_SOCKET
