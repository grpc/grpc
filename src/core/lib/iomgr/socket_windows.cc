/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET

#include <winsock2.h>

// must be included after winsock2.h
#include <mswsock.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_windows.h"
#include "src/core/lib/iomgr/sockaddr_windows.h"
#include "src/core/lib/iomgr/socket_windows.h"

static DWORD s_wsa_socket_flags;

grpc_winsocket* grpc_winsocket_create(SOCKET socket, const char* name) {
  char* final_name;
  grpc_winsocket* r = (grpc_winsocket*)gpr_malloc(sizeof(grpc_winsocket));
  memset(r, 0, sizeof(grpc_winsocket));
  r->socket = socket;
  gpr_mu_init(&r->state_mu);
  gpr_asprintf(&final_name, "%s:socket=0x%p", name, r);
  grpc_iomgr_register_object(&r->iomgr_object, final_name);
  gpr_free(final_name);
  grpc_iocp_add_socket(r);
  return r;
}

SOCKET grpc_winsocket_wrapped_socket(grpc_winsocket* socket) {
  return socket->socket;
}

/* Schedule a shutdown of the socket operations. Will call the pending
   operations to abort them. We need to do that this way because of the
   various callsites of that function, which happens to be in various
   mutex hold states, and that'd be unsafe to call them directly. */
void grpc_winsocket_shutdown(grpc_winsocket* winsocket) {
  /* Grab the function pointer for DisconnectEx for that specific socket.
     It may change depending on the interface. */
  int status;
  GUID guid = WSAID_DISCONNECTEX;
  LPFN_DISCONNECTEX DisconnectEx;
  DWORD ioctl_num_bytes;

  gpr_mu_lock(&winsocket->state_mu);
  if (winsocket->shutdown_called) {
    gpr_mu_unlock(&winsocket->state_mu);
    return;
  }
  winsocket->shutdown_called = true;
  gpr_mu_unlock(&winsocket->state_mu);

  status = WSAIoctl(winsocket->socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &guid, sizeof(guid), &DisconnectEx, sizeof(DisconnectEx),
                    &ioctl_num_bytes, NULL, NULL);

  if (status == 0) {
    DisconnectEx(winsocket->socket, NULL, 0, 0);
  } else {
    char* utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_INFO, "Unable to retrieve DisconnectEx pointer : %s",
            utf8_message);
    gpr_free(utf8_message);
  }
  closesocket(winsocket->socket);
}

static void destroy(grpc_winsocket* winsocket) {
  grpc_iomgr_unregister_object(&winsocket->iomgr_object);
  gpr_mu_destroy(&winsocket->state_mu);
  gpr_free(winsocket);
}

static bool check_destroyable(grpc_winsocket* winsocket) {
  return winsocket->destroy_called == true &&
         winsocket->write_info.closure == NULL &&
         winsocket->read_info.closure == NULL;
}

void grpc_winsocket_destroy(grpc_winsocket* winsocket) {
  gpr_mu_lock(&winsocket->state_mu);
  GPR_ASSERT(!winsocket->destroy_called);
  winsocket->destroy_called = true;
  bool should_destroy = check_destroyable(winsocket);
  gpr_mu_unlock(&winsocket->state_mu);
  if (should_destroy) destroy(winsocket);
}

/* Calling notify_on_read or write means either of two things:
-) The IOCP already completed in the background, and we need to call
the callback now.
-) The IOCP hasn't completed yet, and we're queuing it for later. */
static void socket_notify_on_iocp(grpc_winsocket* socket, grpc_closure* closure,
                                  grpc_winsocket_callback_info* info) {
  GPR_ASSERT(info->closure == NULL);
  gpr_mu_lock(&socket->state_mu);
  if (info->has_pending_iocp) {
    info->has_pending_iocp = 0;
    GRPC_CLOSURE_SCHED(closure, GRPC_ERROR_NONE);
  } else {
    info->closure = closure;
  }
  gpr_mu_unlock(&socket->state_mu);
}

void grpc_socket_notify_on_write(grpc_winsocket* socket,
                                 grpc_closure* closure) {
  socket_notify_on_iocp(socket, closure, &socket->write_info);
}

void grpc_socket_notify_on_read(grpc_winsocket* socket, grpc_closure* closure) {
  socket_notify_on_iocp(socket, closure, &socket->read_info);
}

void grpc_socket_become_ready(grpc_winsocket* socket,
                              grpc_winsocket_callback_info* info) {
  GPR_ASSERT(!info->has_pending_iocp);
  gpr_mu_lock(&socket->state_mu);
  if (info->closure) {
    GRPC_CLOSURE_SCHED(info->closure, GRPC_ERROR_NONE);
    info->closure = NULL;
  } else {
    info->has_pending_iocp = 1;
  }
  bool should_destroy = check_destroyable(socket);
  gpr_mu_unlock(&socket->state_mu);
  if (should_destroy) destroy(socket);
}

static gpr_once g_probe_ipv6_once = GPR_ONCE_INIT;
static bool g_ipv6_loopback_available = false;

static void probe_ipv6_once(void) {
  SOCKET s = socket(AF_INET6, SOCK_STREAM, 0);
  g_ipv6_loopback_available = 0;
  if (s == INVALID_SOCKET) {
    gpr_log(GPR_INFO, "Disabling AF_INET6 sockets because socket() failed.");
  } else {
    grpc_sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr.s6_addr[15] = 1; /* [::1]:0 */
    if (bind(s, reinterpret_cast<grpc_sockaddr*>(&addr), sizeof(addr)) == 0) {
      g_ipv6_loopback_available = 1;
    } else {
      gpr_log(GPR_INFO,
              "Disabling AF_INET6 sockets because ::1 is not available.");
    }
    closesocket(s);
  }
}

int grpc_ipv6_loopback_available(void) {
  gpr_once_init(&g_probe_ipv6_once, probe_ipv6_once);
  return g_ipv6_loopback_available;
}

DWORD grpc_get_default_wsa_socket_flags() { return s_wsa_socket_flags; }

void grpc_wsa_socket_flags_init() {
  s_wsa_socket_flags = WSA_FLAG_OVERLAPPED;
  /* WSA_FLAG_NO_HANDLE_INHERIT may be not supported on the older Windows
     versions, see
     https://msdn.microsoft.com/en-us/library/windows/desktop/ms742212(v=vs.85).aspx
     for details. */
  SOCKET sock = WSASocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                          s_wsa_socket_flags | WSA_FLAG_NO_HANDLE_INHERIT);
  if (sock != INVALID_SOCKET) {
    /* Windows 7, Windows 2008 R2 with SP1 or later */
    s_wsa_socket_flags |= WSA_FLAG_NO_HANDLE_INHERIT;
    closesocket(sock);
  }
}

#endif /* GRPC_WINSOCK_SOCKET */
