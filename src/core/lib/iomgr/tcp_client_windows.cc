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

#include "absl/log/check.h"
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

using ::grpc_event_engine::experimental::EndpointConfig;

struct async_connect {
  grpc_closure* on_done;
  gpr_mu mu;
  grpc_winsocket* socket;
  grpc_timer alarm;
  grpc_closure on_alarm;
  std::string addr_name;
  int refs;
  grpc_closure on_connect;
  grpc_endpoint** endpoint;
};

static void async_connect_unlock_and_cleanup(async_connect* ac,
                                             grpc_winsocket* socket) {
  int done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (done) {
    gpr_mu_destroy(&ac->mu);
    delete ac;
  }
  if (socket != NULL) grpc_winsocket_destroy(socket);
}

static void on_alarm(void* acp, grpc_error_handle /* error */) {
  async_connect* ac = (async_connect*)acp;
  gpr_mu_lock(&ac->mu);
  grpc_winsocket* socket = ac->socket;
  ac->socket = NULL;
  if (socket != NULL) {
    grpc_winsocket_shutdown(socket);
  }
  async_connect_unlock_and_cleanup(ac, socket);
}

static void on_connect(void* acp, grpc_error_handle error) {
  async_connect* ac = (async_connect*)acp;
  grpc_endpoint** ep = ac->endpoint;
  CHECK(*ep == NULL);
  grpc_closure* on_done = ac->on_done;

  gpr_mu_lock(&ac->mu);
  grpc_winsocket* socket = ac->socket;
  ac->socket = NULL;
  gpr_mu_unlock(&ac->mu);

  grpc_timer_cancel(&ac->alarm);

  gpr_mu_lock(&ac->mu);

  if (error.ok()) {
    if (socket != NULL) {
      DWORD transferred_bytes = 0;
      DWORD flags;
      BOOL wsa_success =
          WSAGetOverlappedResult(socket->socket, &socket->write_info.overlapped,
                                 &transferred_bytes, FALSE, &flags);
      CHECK_EQ(transferred_bytes, 0);
      if (!wsa_success) {
        error = GRPC_WSA_ERROR(WSAGetLastError(), "ConnectEx");
        closesocket(socket->socket);
      } else {
        *ep = grpc_tcp_create(socket, ac->addr_name);
        socket = nullptr;
      }
    } else {
      error = GRPC_ERROR_CREATE("socket is null");
    }
  }

  async_connect_unlock_and_cleanup(ac, socket);
  // If the connection was aborted, the callback was already called when
  // the deadline was met.
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, error);
}

// Tries to issue one async connection, then schedules both an IOCP
// notification request for the connection, and one timeout alert.
static int64_t tcp_connect(grpc_closure* on_done, grpc_endpoint** endpoint,
                           grpc_pollset_set* /* interested_parties */,
                           const EndpointConfig& config,
                           const grpc_resolved_address* addr,
                           grpc_core::Timestamp deadline) {
  if (grpc_event_engine::experimental::UseEventEngineClient()) {
    return grpc_event_engine::experimental::event_engine_tcp_client_connect(
        on_done, endpoint, config, addr, deadline);
  }
  SOCKET sock = INVALID_SOCKET;
  BOOL success;
  int status;
  grpc_resolved_address addr6_v4mapped;
  grpc_resolved_address local_address;
  grpc_winsocket* socket = NULL;
  LPFN_CONNECTEX ConnectEx;
  GUID guid = WSAID_CONNECTEX;
  DWORD ioctl_num_bytes;
  grpc_winsocket_callback_info* info;
  grpc_error_handle error;
  async_connect* ac = NULL;
  absl::StatusOr<std::string> addr_uri;
  int addr_family;
  int protocol;

  addr_uri = grpc_sockaddr_to_uri(addr);
  if (!addr_uri.ok()) {
    error = GRPC_ERROR_CREATE(addr_uri.status().ToString());
    goto failure;
  }

  *endpoint = NULL;

  // Use dualstack sockets where available.
  if (grpc_sockaddr_to_v4mapped(addr, &addr6_v4mapped)) {
    addr = &addr6_v4mapped;
  }

  // extract family
  addr_family =
      (grpc_sockaddr_get_family(addr) == AF_UNIX) ? AF_UNIX : AF_INET6;
  protocol = addr_family == AF_UNIX ? 0 : IPPROTO_TCP;

  sock = WSASocket(addr_family, SOCK_STREAM, protocol, NULL, 0,
                   grpc_get_default_wsa_socket_flags());
  if (sock == INVALID_SOCKET) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "WSASocket");
    goto failure;
  }

  if (addr_family == AF_UNIX) {
    // tcp settings for af_unix are skipped.
    error = grpc_tcp_set_non_block(sock);
  } else {
    error = grpc_tcp_prepare_socket(sock);
  }

  if (!error.ok()) {
    goto failure;
  }

  // Grab the function pointer for ConnectEx for that specific socket.
  // It may change depending on the interface.
  status =
      WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
               &ConnectEx, sizeof(ConnectEx), &ioctl_num_bytes, NULL, NULL);

  if (status != 0) {
    error = GRPC_WSA_ERROR(WSAGetLastError(),
                           "WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)");
    goto failure;
  }

  if (addr_family == AF_UNIX) {
    // For ConnectEx() to work for AF_UNIX, the sock needs to be bound to
    // the local address of an unnamed socket.
    local_address = {};
    ((grpc_sockaddr*)local_address.addr)->sa_family = AF_UNIX;
    local_address.len = sizeof(grpc_sockaddr);
  } else {
    grpc_sockaddr_make_wildcard6(0, &local_address);
  }

  status =
      bind(sock, (grpc_sockaddr*)&local_address.addr, (int)local_address.len);
  if (status != 0) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "bind");
    goto failure;
  }

  socket = grpc_winsocket_create(sock, "client");
  info = &socket->write_info;
  success = ConnectEx(sock, (grpc_sockaddr*)&addr->addr, (int)addr->len, NULL,
                      0, NULL, &info->overlapped);
  // It wouldn't be unusual to get a success immediately. But we'll still get
  // an IOCP notification, so let's ignore it.
  if (!success) {
    int last_error = WSAGetLastError();
    if (last_error != ERROR_IO_PENDING) {
      error = GRPC_WSA_ERROR(last_error, "ConnectEx");
      goto failure;
    }
  }
  ac = new async_connect();
  ac->on_done = on_done;
  ac->socket = socket;
  gpr_mu_init(&ac->mu);
  ac->refs = 2;
  ac->addr_name = addr_uri.value();
  ac->endpoint = endpoint;
  GRPC_CLOSURE_INIT(&ac->on_connect, on_connect, ac, grpc_schedule_on_exec_ctx);

  GRPC_CLOSURE_INIT(&ac->on_alarm, on_alarm, ac, grpc_schedule_on_exec_ctx);
  gpr_mu_lock(&ac->mu);
  grpc_timer_init(&ac->alarm, deadline, &ac->on_alarm);
  grpc_socket_notify_on_write(socket, &ac->on_connect);
  gpr_mu_unlock(&ac->mu);
  return 0;

failure:
  CHECK(!error.ok());
  grpc_error_handle final_error =
      GRPC_ERROR_CREATE_REFERENCING("Failed to connect", &error, 1);
  if (socket != NULL) {
    grpc_winsocket_destroy(socket);
  } else if (sock != INVALID_SOCKET) {
    closesocket(sock);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, final_error);
  return 0;
}

static bool tcp_cancel_connect(int64_t connection_handle) {
  if (grpc_event_engine::experimental::UseEventEngineClient()) {
    return grpc_event_engine::experimental::
        event_engine_tcp_client_cancel_connect(connection_handle);
  }
  return false;
}

grpc_tcp_client_vtable grpc_windows_tcp_client_vtable = {tcp_connect,
                                                         tcp_cancel_connect};

#endif  // GRPC_WINSOCK_SOCKET
