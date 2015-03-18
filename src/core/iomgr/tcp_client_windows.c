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
  void(*cb)(void *arg, grpc_endpoint *tcp);
  void *cb_arg;
  gpr_mu mu;
  grpc_winsocket *socket;
  gpr_timespec deadline;
  grpc_alarm alarm;
  int refs;
} async_connect;

static void async_connect_cleanup(async_connect *ac) {
  int done = (--ac->refs == 0);
  gpr_mu_unlock(&ac->mu);
  if (done) {
    gpr_mu_destroy(&ac->mu);
    gpr_free(ac);
  }
}

static void on_alarm(void *acp, int success) {
  async_connect *ac = acp;
  gpr_mu_lock(&ac->mu);
  if (ac->socket != NULL && success) {
    grpc_winsocket_shutdown(ac->socket);
  }
  async_connect_cleanup(ac);
}

static void on_connect(void *acp, int success) {
  async_connect *ac = acp;
  SOCKET sock = ac->socket->socket;
  grpc_endpoint *ep = NULL;
  grpc_winsocket_callback_info *info = &ac->socket->write_info;
  void(*cb)(void *arg, grpc_endpoint *tcp) = ac->cb;
  void *cb_arg = ac->cb_arg;

  grpc_alarm_cancel(&ac->alarm);

  if (success) {
    DWORD transfered_bytes = 0;
    DWORD flags;
    BOOL wsa_success = WSAGetOverlappedResult(sock, &info->overlapped,
                                              &transfered_bytes, FALSE,
                                              &flags);
    GPR_ASSERT(transfered_bytes == 0);
    if (!wsa_success) {
      char *utf8_message = gpr_format_message(WSAGetLastError());
      gpr_log(GPR_ERROR, "on_connect error: %s", utf8_message);
      gpr_free(utf8_message);
      goto finish;
    } else {
      gpr_log(GPR_DEBUG, "on_connect: connection established");
      ep = grpc_tcp_create(ac->socket);
      goto finish;
    }
  } else {
    gpr_log(GPR_ERROR, "on_connect is shutting down");
    goto finish;
  }

  abort();

finish:
  gpr_mu_lock(&ac->mu);
  if (!ep) {
    grpc_winsocket_orphan(ac->socket);
  }
  async_connect_cleanup(ac);
  cb(cb_arg, ep);
}

void grpc_tcp_client_connect(void(*cb)(void *arg, grpc_endpoint *tcp),
                             void *arg, const struct sockaddr *addr,
                             int addr_len, gpr_timespec deadline) {
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

  status = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &guid, sizeof(guid), &ConnectEx, sizeof(ConnectEx),
                    &ioctl_num_bytes, NULL, NULL);

  if (status != 0) {
    message = "Unable to retreive ConnectEx pointer: %s";
    goto failure;
  }

  grpc_sockaddr_make_wildcard6(0, &local_address);

  status = bind(sock, (struct sockaddr *) &local_address,
                sizeof(local_address));
  if (status != 0) {
    message = "Unable to bind socket: %s";
    goto failure;
  }

  socket = grpc_winsocket_create(sock);
  info = &socket->write_info;
  success = ConnectEx(sock, addr, addr_len, NULL, 0, NULL, &info->overlapped);

  if (success) {
    gpr_log(GPR_DEBUG, "connected immediately - but we still go to sleep");
  } else {
    int error = WSAGetLastError();
    if (error != ERROR_IO_PENDING) {
      message = "ConnectEx failed: %s";
      goto failure;
    }
  }

  gpr_log(GPR_DEBUG, "grpc_tcp_client_connect: connection pending");
  ac = gpr_malloc(sizeof(async_connect));
  ac->cb = cb;
  ac->cb_arg = arg;
  ac->socket = socket;
  gpr_mu_init(&ac->mu);
  ac->refs = 2;

  grpc_alarm_init(&ac->alarm, deadline, on_alarm, ac, gpr_now());
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

#endif  /* GPR_WINSOCK_SOCKET */
