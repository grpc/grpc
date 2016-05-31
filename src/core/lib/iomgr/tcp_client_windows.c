/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/support/port_platform.h>

#ifdef GPR_WINSOCK_SOCKET

#include "src/core/lib/iomgr/sockaddr_windows.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_windows.h"
#include "src/core/lib/iomgr/timer.h"

typedef struct {
  grpc_closure *on_done;
  gpr_mu mu;
  grpc_winsocket *socket;
  gpr_timespec deadline;
  grpc_timer alarm;
  char *addr_name;
  int refs;
  grpc_closure on_connect;
  grpc_endpoint **endpoint;
} async_connect;

static void async_connect_unlock_and_cleanup(async_connect *ac,
                                             grpc_winsocket *socket) {
  int done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (done) {
    gpr_mu_destroy(&ac->mu);
    gpr_free(ac->addr_name);
    gpr_free(ac);
  }
  if (socket != NULL) grpc_winsocket_destroy(socket);
}

static void on_alarm(grpc_exec_ctx *exec_ctx, void *acp, bool occured) {
  async_connect *ac = acp;
  gpr_mu_lock(&ac->mu);
  if (ac->socket != NULL) {
    grpc_winsocket_shutdown(ac->socket);
  }
  async_connect_unlock_and_cleanup(ac, ac->socket);
}

static void on_connect(grpc_exec_ctx *exec_ctx, void *acp, bool from_iocp) {
  async_connect *ac = acp;
  SOCKET sock = ac->socket->socket;
  grpc_endpoint **ep = ac->endpoint;
  GPR_ASSERT(*ep == NULL);
  grpc_winsocket_callback_info *info = &ac->socket->write_info;
  grpc_closure *on_done = ac->on_done;

  gpr_mu_lock(&ac->mu);
  grpc_winsocket *socket = ac->socket;
  ac->socket = NULL;
  gpr_mu_unlock(&ac->mu);

  grpc_timer_cancel(exec_ctx, &ac->alarm);

  gpr_mu_lock(&ac->mu);

  if (from_iocp && socket != NULL) {
    DWORD transfered_bytes = 0;
    DWORD flags;
    BOOL wsa_success = WSAGetOverlappedResult(sock, &info->overlapped,
                                              &transfered_bytes, FALSE, &flags);
    GPR_ASSERT(transfered_bytes == 0);
    if (!wsa_success) {
      char *utf8_message = gpr_format_message(WSAGetLastError());
      gpr_log(GPR_ERROR, "on_connect error connecting to '%s': %s",
              ac->addr_name, utf8_message);
      gpr_free(utf8_message);
    } else {
      *ep = grpc_tcp_create(socket, ac->addr_name);
      socket = NULL;
    }
  }

  async_connect_unlock_and_cleanup(ac, socket);
  /* If the connection was aborted, the callback was already called when
     the deadline was met. */
  on_done->cb(exec_ctx, on_done->cb_arg, *ep != NULL);
}

/* Tries to issue one async connection, then schedules both an IOCP
   notification request for the connection, and one timeout alert. */
void grpc_tcp_client_connect(grpc_exec_ctx *exec_ctx, grpc_closure *on_done,
                             grpc_endpoint **endpoint,
                             grpc_pollset_set *interested_parties,
                             const struct sockaddr *addr, size_t addr_len,
                             gpr_timespec deadline) {
  SOCKET sock = INVALID_SOCKET;
  BOOL success;
  int status;
  struct sockaddr_in6 addr6_v4mapped;
  struct sockaddr_in6 local_address;
  async_connect *ac;
  grpc_winsocket *socket = NULL;
  LPFN_CONNECTEX ConnectEx;
  GUID guid = WSAID_CONNECTEX;
  DWORD ioctl_num_bytes;
  const char *message = NULL;
  char *utf8_message;
  grpc_winsocket_callback_info *info;
  int last_error;

  *endpoint = NULL;

  /* Use dualstack sockets where available. */
  if (grpc_sockaddr_to_v4mapped(addr, &addr6_v4mapped)) {
    addr = (const struct sockaddr *)&addr6_v4mapped;
    addr_len = sizeof(addr6_v4mapped);
  }

  sock = WSASocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                   WSA_FLAG_OVERLAPPED);
  if (sock == INVALID_SOCKET) {
    message = "Unable to create socket: %s";
    goto failure;
  }

  if (!grpc_tcp_prepare_socket(sock)) {
    message = "Unable to set socket options: %s";
    goto failure;
  }

  /* Grab the function pointer for ConnectEx for that specific socket.
     It may change depending on the interface. */
  status =
      WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
               &ConnectEx, sizeof(ConnectEx), &ioctl_num_bytes, NULL, NULL);

  if (status != 0) {
    message = "Unable to retrieve ConnectEx pointer: %s";
    goto failure;
  }

  grpc_sockaddr_make_wildcard6(0, &local_address);

  status = bind(sock, (struct sockaddr *)&local_address, sizeof(local_address));
  if (status != 0) {
    message = "Unable to bind socket: %s";
    goto failure;
  }

  socket = grpc_winsocket_create(sock, "client");
  info = &socket->write_info;
  success =
      ConnectEx(sock, addr, (int)addr_len, NULL, 0, NULL, &info->overlapped);

  /* It wouldn't be unusual to get a success immediately. But we'll still get
     an IOCP notification, so let's ignore it. */
  if (!success) {
    int error = WSAGetLastError();
    if (error != ERROR_IO_PENDING) {
      message = "ConnectEx failed: %s";
      goto failure;
    }
  }

  ac = gpr_malloc(sizeof(async_connect));
  ac->on_done = on_done;
  ac->socket = socket;
  gpr_mu_init(&ac->mu);
  ac->refs = 2;
  ac->addr_name = grpc_sockaddr_to_uri(addr);
  ac->endpoint = endpoint;
  grpc_closure_init(&ac->on_connect, on_connect, ac);

  grpc_timer_init(exec_ctx, &ac->alarm, deadline, on_alarm, ac,
                  gpr_now(GPR_CLOCK_MONOTONIC));
  grpc_socket_notify_on_write(exec_ctx, socket, &ac->on_connect);
  return;

failure:
  last_error = WSAGetLastError();
  utf8_message = gpr_format_message(last_error);
  gpr_log(GPR_ERROR, message, utf8_message);
  gpr_log(GPR_ERROR, "last error = %d", last_error);
  gpr_free(utf8_message);
  if (socket != NULL) {
    grpc_winsocket_destroy(socket);
  } else if (sock != INVALID_SOCKET) {
    closesocket(sock);
  }
  grpc_exec_ctx_enqueue(exec_ctx, on_done, false, NULL);
}

#endif /* GPR_WINSOCK_SOCKET */
