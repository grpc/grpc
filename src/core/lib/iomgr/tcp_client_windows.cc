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

#include <inttypes.h>

#ifdef GRPC_WINSOCK_SOCKET

#include "src/core/lib/iomgr/sockaddr_windows.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_windows.h"
#include "src/core/lib/iomgr/timer.h"

typedef struct {
  grpc_closure* on_done;
  gpr_mu mu;
  grpc_winsocket* socket;
  grpc_timer alarm;
  grpc_closure on_alarm;
  char* addr_name;
  int refs;
  grpc_closure on_connect;
  grpc_endpoint** endpoint;
  grpc_channel_args* channel_args;
} async_connect;

static void async_connect_unlock_and_cleanup(async_connect* ac,
                                             grpc_winsocket* socket) {
  int done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (done) {
    grpc_channel_args_destroy(ac->channel_args);
    gpr_mu_destroy(&ac->mu);
    gpr_free(ac->addr_name);
    gpr_free(ac);
  }
  if (socket != NULL) grpc_winsocket_destroy(socket);
}

static void on_alarm(void* acp, grpc_error* error) {
  async_connect* ac = (async_connect*)acp;
  gpr_mu_lock(&ac->mu);
  grpc_winsocket* socket = ac->socket;
  ac->socket = NULL;
  if (socket != NULL) {
    grpc_winsocket_shutdown(socket);
  }
  async_connect_unlock_and_cleanup(ac, socket);
}

static void on_connect(void* acp, grpc_error* error) {
  async_connect* ac = (async_connect*)acp;
  grpc_endpoint** ep = ac->endpoint;
  GPR_ASSERT(*ep == NULL);
  grpc_closure* on_done = ac->on_done;

  GRPC_ERROR_REF(error);

  gpr_mu_lock(&ac->mu);
  grpc_winsocket* socket = ac->socket;
  ac->socket = NULL;
  gpr_mu_unlock(&ac->mu);

  grpc_timer_cancel(&ac->alarm);

  gpr_mu_lock(&ac->mu);

  if (error == GRPC_ERROR_NONE) {
    if (socket != NULL) {
      DWORD transfered_bytes = 0;
      DWORD flags;
      BOOL wsa_success =
          WSAGetOverlappedResult(socket->socket, &socket->write_info.overlapped,
                                 &transfered_bytes, FALSE, &flags);
      GPR_ASSERT(transfered_bytes == 0);
      if (!wsa_success) {
        error = GRPC_WSA_ERROR(WSAGetLastError(), "ConnectEx");
        closesocket(socket->socket);
      } else {
        *ep = grpc_tcp_create(socket, ac->channel_args, ac->addr_name);
        socket = NULL;
      }
    } else {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("socket is null");
    }
  }

  async_connect_unlock_and_cleanup(ac, socket);
  /* If the connection was aborted, the callback was already called when
     the deadline was met. */
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, error);
}

/* Tries to issue one async connection, then schedules both an IOCP
   notification request for the connection, and one timeout alert. */
static void tcp_connect(grpc_closure* on_done, grpc_endpoint** endpoint,
                        grpc_pollset_set* interested_parties,
                        const grpc_channel_args* channel_args,
                        const grpc_resolved_address* addr,
                        grpc_millis deadline) {
  SOCKET sock = INVALID_SOCKET;
  BOOL success;
  int status;
  grpc_resolved_address addr6_v4mapped;
  grpc_resolved_address local_address;
  async_connect* ac;
  grpc_winsocket* socket = NULL;
  LPFN_CONNECTEX ConnectEx;
  GUID guid = WSAID_CONNECTEX;
  DWORD ioctl_num_bytes;
  grpc_winsocket_callback_info* info;
  grpc_error* error = GRPC_ERROR_NONE;

  *endpoint = NULL;

  /* Use dualstack sockets where available. */
  if (grpc_sockaddr_to_v4mapped(addr, &addr6_v4mapped)) {
    addr = &addr6_v4mapped;
  }

  sock = WSASocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                   grpc_get_default_wsa_socket_flags());
  if (sock == INVALID_SOCKET) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "WSASocket");
    goto failure;
  }

  error = grpc_tcp_prepare_socket(sock);
  if (error != GRPC_ERROR_NONE) {
    goto failure;
  }

  /* Grab the function pointer for ConnectEx for that specific socket.
     It may change depending on the interface. */
  status =
      WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
               &ConnectEx, sizeof(ConnectEx), &ioctl_num_bytes, NULL, NULL);

  if (status != 0) {
    error = GRPC_WSA_ERROR(WSAGetLastError(),
                           "WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)");
    goto failure;
  }

  grpc_sockaddr_make_wildcard6(0, &local_address);

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

  /* It wouldn't be unusual to get a success immediately. But we'll still get
     an IOCP notification, so let's ignore it. */
  if (!success) {
    int last_error = WSAGetLastError();
    if (last_error != ERROR_IO_PENDING) {
      error = GRPC_WSA_ERROR(last_error, "ConnectEx");
      goto failure;
    }
  }

  ac = (async_connect*)gpr_malloc(sizeof(async_connect));
  ac->on_done = on_done;
  ac->socket = socket;
  gpr_mu_init(&ac->mu);
  ac->refs = 2;
  ac->addr_name = grpc_sockaddr_to_uri(addr);
  ac->endpoint = endpoint;
  ac->channel_args = grpc_channel_args_copy(channel_args);
  GRPC_CLOSURE_INIT(&ac->on_connect, on_connect, ac, grpc_schedule_on_exec_ctx);

  GRPC_CLOSURE_INIT(&ac->on_alarm, on_alarm, ac, grpc_schedule_on_exec_ctx);
  grpc_timer_init(&ac->alarm, deadline, &ac->on_alarm);
  grpc_socket_notify_on_write(socket, &ac->on_connect);
  return;

failure:
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  char* target_uri = grpc_sockaddr_to_uri(addr);
  grpc_error* final_error =
      grpc_error_set_str(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Failed to connect", &error, 1),
                         GRPC_ERROR_STR_TARGET_ADDRESS,
                         grpc_slice_from_copied_string(
                             target_uri == nullptr ? "NULL" : target_uri));
  GRPC_ERROR_UNREF(error);
  if (socket != NULL) {
    grpc_winsocket_destroy(socket);
  } else if (sock != INVALID_SOCKET) {
    closesocket(sock);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, final_error);
}

grpc_tcp_client_vtable grpc_windows_tcp_client_vtable = {tcp_connect};

#endif /* GRPC_WINSOCK_SOCKET */
