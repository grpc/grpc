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

#include "src/core/iomgr/sockaddr_win32.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_win32.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/useful.h>

#include "src/core/iomgr/alarm.h"
#include "src/core/iomgr/iocp_windows.h"
#include "src/core/iomgr/tcp_client.h"
#include "src/core/iomgr/tcp_windows.h"
#include "src/core/iomgr/sockaddr.h"
#include "src/core/iomgr/sockaddr_utils.h"
#include "src/core/iomgr/socket_windows.h"

typedef struct {
  void (*cb)(void *arg, grpc_endpoint *tcp);
  void *cb_arg;
  gpr_mu mu;
  grpc_winsocket *socket;
  gpr_timespec deadline;
  grpc_alarm alarm;
  int refs;
  int aborted;
} async_connect;

static void async_connect_cleanup(async_connect *ac) {
  int done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (done) {
    gpr_mu_destroy(&ac->mu);
    gpr_free(ac);
  }
}

static void on_alarm(void *acp, int occured) {
  async_connect *ac = acp;
  gpr_mu_lock(&ac->mu);
  /* If the alarm didn't occur, it got cancelled. */
  if (ac->socket != NULL && occured) {
    grpc_winsocket_shutdown(ac->socket);
  }
  async_connect_cleanup(ac);
}

static void on_connect(void *acp, int from_iocp) {
  async_connect *ac = acp;
  SOCKET sock = ac->socket->socket;
  grpc_endpoint *ep = NULL;
  grpc_winsocket_callback_info *info = &ac->socket->write_info;
  void (*cb)(void *arg, grpc_endpoint *tcp) = ac->cb;
  void *cb_arg = ac->cb_arg;
  int aborted;

  grpc_alarm_cancel(&ac->alarm);

  gpr_mu_lock(&ac->mu);
  aborted = ac->aborted;

  if (from_iocp) {
    DWORD transfered_bytes = 0;
    DWORD flags;
    BOOL wsa_success = WSAGetOverlappedResult(sock, &info->overlapped,
                                              &transfered_bytes, FALSE, &flags);
    info->outstanding = 0;
    GPR_ASSERT(transfered_bytes == 0);
    if (!wsa_success) {
      char *utf8_message = gpr_format_message(WSAGetLastError());
      gpr_log(GPR_ERROR, "on_connect error: %s", utf8_message);
      gpr_free(utf8_message);
    } else if (!aborted) {
      ep = grpc_tcp_create(ac->socket);
    }
  } else {
    gpr_log(GPR_ERROR, "on_connect is shutting down");
    /* If the connection timeouts, we will still get a notification from
       the IOCP whatever happens. So we're just going to flag that connection
       as being in the process of being aborted, and wait for the IOCP. We
       can't just orphan the socket now, because the IOCP might already have
       gotten a successful connection, which is our worst-case scenario.
       We need to call our callback now to respect the deadline. */
    ac->aborted = 1;
    gpr_mu_unlock(&ac->mu);
    cb(cb_arg, NULL);
    return;
  }

  ac->socket->write_info.outstanding = 0;

  /* If we don't have an endpoint, it means the connection failed,
     so it doesn't matter if it aborted or failed. We need to orphan
     that socket. */
  if (!ep || aborted) grpc_winsocket_orphan(ac->socket);
  async_connect_cleanup(ac);
  /* If the connection was aborted, the callback was already called when
     the deadline was met. */
  if (!aborted) cb(cb_arg, ep);
}

/* Tries to issue one async connection, then schedules both an IOCP
   notification request for the connection, and one timeout alert. */
void grpc_tcp_client_connect(void (*cb)(void *arg, grpc_endpoint *tcp),
                             void *arg, grpc_pollset_set *interested_parties,
                             const struct sockaddr *addr, int addr_len,
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
  info->outstanding = 1;
  success = ConnectEx(sock, addr, addr_len, NULL, 0, NULL, &info->overlapped);

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
  ac->cb = cb;
  ac->cb_arg = arg;
  ac->socket = socket;
  gpr_mu_init(&ac->mu);
  ac->refs = 2;
  ac->aborted = 0;

  grpc_alarm_init(&ac->alarm, deadline, on_alarm, ac,
                  gpr_now(GPR_CLOCK_REALTIME));
  socket->write_info.outstanding = 1;
  grpc_socket_notify_on_write(socket, on_connect, ac);
  return;

failure:
  utf8_message = gpr_format_message(WSAGetLastError());
  gpr_log(GPR_ERROR, message, utf8_message);
  gpr_free(utf8_message);
  if (socket) {
    grpc_winsocket_orphan(socket);
  } else if (sock != INVALID_SOCKET) {
    closesocket(sock);
  }
  cb(arg, NULL);
}

#endif /* GPR_WINSOCK_SOCKET */
