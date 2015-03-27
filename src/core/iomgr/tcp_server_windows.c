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

#define _GNU_SOURCE
#include "src/core/iomgr/sockaddr_utils.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_win32.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/iomgr/iocp_windows.h"
#include "src/core/iomgr/pollset_windows.h"
#include "src/core/iomgr/socket_windows.h"
#include "src/core/iomgr/tcp_server.h"
#include "src/core/iomgr/tcp_windows.h"

#define INIT_PORT_CAP 2
#define MIN_SAFE_ACCEPT_QUEUE_SIZE 100

/* one listening port */
typedef struct server_port {
  gpr_uint8 addresses[sizeof(struct sockaddr_in6) * 2 + 32];
  SOCKET new_socket;
  grpc_winsocket *socket;
  grpc_tcp_server *server;
  LPFN_ACCEPTEX AcceptEx;
} server_port;

/* the overall server */
struct grpc_tcp_server {
  grpc_tcp_server_cb cb;
  void *cb_arg;

  gpr_mu mu;
  gpr_cv cv;

  /* active port count: how many ports are actually still listening */
  int active_ports;

  /* all listening ports */
  server_port *ports;
  size_t nports;
  size_t port_capacity;
};

grpc_tcp_server *grpc_tcp_server_create(void) {
  grpc_tcp_server *s = gpr_malloc(sizeof(grpc_tcp_server));
  gpr_mu_init(&s->mu);
  gpr_cv_init(&s->cv);
  s->active_ports = 0;
  s->cb = NULL;
  s->cb_arg = NULL;
  s->ports = gpr_malloc(sizeof(server_port) * INIT_PORT_CAP);
  s->nports = 0;
  s->port_capacity = INIT_PORT_CAP;
  return s;
}

void grpc_tcp_server_destroy(grpc_tcp_server *s) {
  size_t i;
  gpr_mu_lock(&s->mu);
  /* shutdown all fd's */
  for (i = 0; i < s->nports; i++) {
    grpc_winsocket_shutdown(s->ports[i].socket);
  }
  /* wait while that happens */
  while (s->active_ports) {
    gpr_cv_wait(&s->cv, &s->mu, gpr_inf_future);
  }
  gpr_mu_unlock(&s->mu);

  /* delete ALL the things */
  for (i = 0; i < s->nports; i++) {
    server_port *sp = &s->ports[i];
    grpc_winsocket_orphan(sp->socket);
  }
  gpr_free(s->ports);
  gpr_free(s);
}

/* Prepare a recently-created socket for listening. */
static int prepare_socket(SOCKET sock,
                          const struct sockaddr *addr, int addr_len) {
  struct sockaddr_storage sockname_temp;
  socklen_t sockname_len;

  if (sock == INVALID_SOCKET) goto error;

  if (!grpc_tcp_prepare_socket(sock)) {
    char *utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_ERROR, "Unable to prepare socket: %s", utf8_message);
    gpr_free(utf8_message);
    goto error;
  }

  if (bind(sock, addr, addr_len) == SOCKET_ERROR) {
    char *addr_str;
    char *utf8_message = gpr_format_message(WSAGetLastError());
    grpc_sockaddr_to_string(&addr_str, addr, 0);
    gpr_log(GPR_ERROR, "bind addr=%s: %s", addr_str, utf8_message);
    gpr_free(utf8_message);
    gpr_free(addr_str);
    goto error;
  }

  if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
    char *utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_ERROR, "listen: %s", utf8_message);
    gpr_free(utf8_message);
    goto error;
  }

  sockname_len = sizeof(sockname_temp);
  if (getsockname(sock, (struct sockaddr *) &sockname_temp, &sockname_len)
        == SOCKET_ERROR) {
    char *utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_ERROR, "getsockname: %s", utf8_message);
    gpr_free(utf8_message);
    goto error;
  }

  return grpc_sockaddr_get_port((struct sockaddr *) &sockname_temp);

error:
  if (sock != INVALID_SOCKET) closesocket(sock);
  return -1;
}

static void on_accept(void *arg, int success);

static void start_accept(server_port *port) {
  SOCKET sock = INVALID_SOCKET;
  char *message;
  char *utf8_message;
  BOOL success;
  DWORD addrlen = sizeof(struct sockaddr_in6) + 16;
  DWORD bytes_received = 0;

  sock = WSASocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                   WSA_FLAG_OVERLAPPED);

  if (sock == INVALID_SOCKET) {
    message = "Unable to create socket: %s";
    goto failure;
  }

  if (!grpc_tcp_prepare_socket(sock)) {
    message = "Unable to prepare socket: %s";
    goto failure;
  }

  success = port->AcceptEx(port->socket->socket, sock, port->addresses, 0,
                           addrlen, addrlen, &bytes_received,
                           &port->socket->read_info.overlapped);

  if (success) {
    gpr_log(GPR_DEBUG, "accepted immediately - but we still go to sleep");
  } else {
    int error = WSAGetLastError();
    if (error != ERROR_IO_PENDING) {
      message = "AcceptEx failed: %s";
      goto failure;
    }
  }

  port->new_socket = sock;
  grpc_socket_notify_on_read(port->socket, on_accept, port);
  return;

failure:
  utf8_message = gpr_format_message(WSAGetLastError());
  gpr_log(GPR_ERROR, message, utf8_message);
  gpr_free(utf8_message);
  if (sock != INVALID_SOCKET) closesocket(sock);
}

/* event manager callback when reads are ready */
static void on_accept(void *arg, int success) {
  server_port *sp = arg;
  SOCKET sock = sp->new_socket;
  grpc_winsocket_callback_info *info = &sp->socket->read_info;
  grpc_endpoint *ep = NULL;

  if (success) {
    DWORD transfered_bytes = 0;
    DWORD flags;
    BOOL wsa_success = WSAGetOverlappedResult(sock, &info->overlapped,
                                              &transfered_bytes, FALSE,
                                              &flags);
    if (!wsa_success) {
      char *utf8_message = gpr_format_message(WSAGetLastError());
      gpr_log(GPR_ERROR, "on_accept error: %s", utf8_message);
      gpr_free(utf8_message);
      closesocket(sock);
    } else {
      gpr_log(GPR_DEBUG, "on_accept: accepted connection");
      ep = grpc_tcp_create(grpc_winsocket_create(sock));
    }
  } else {
    gpr_log(GPR_DEBUG, "on_accept: shutting down");
    closesocket(sock);
    gpr_mu_lock(&sp->server->mu);
    if (0 == --sp->server->active_ports) {
      gpr_cv_broadcast(&sp->server->cv);
    }
    gpr_mu_unlock(&sp->server->mu);
  }

  if (ep) sp->server->cb(sp->server->cb_arg, ep);
  start_accept(sp);
}

static int add_socket_to_server(grpc_tcp_server *s, SOCKET sock,
                                const struct sockaddr *addr, int addr_len) {
  server_port *sp;
  int port;
  int status;
  GUID guid = WSAID_ACCEPTEX;
  DWORD ioctl_num_bytes;
  LPFN_ACCEPTEX AcceptEx;

  if (sock == INVALID_SOCKET) return -1;

  status = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &guid, sizeof(guid), &AcceptEx, sizeof(AcceptEx),
                    &ioctl_num_bytes, NULL, NULL);

  if (status != 0) {
    char *utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_ERROR, "on_connect error: %s", utf8_message);
    gpr_free(utf8_message);
    closesocket(sock);
    return -1;
  }

  port = prepare_socket(sock, addr, addr_len);
  if (port >= 0) {
    gpr_mu_lock(&s->mu);
    GPR_ASSERT(!s->cb && "must add ports before starting server");
    /* append it to the list under a lock */
    if (s->nports == s->port_capacity) {
      s->port_capacity *= 2;
      s->ports = gpr_realloc(s->ports, sizeof(server_port) * s->port_capacity);
    }
    sp = &s->ports[s->nports++];
    sp->server = s;
    sp->socket = grpc_winsocket_create(sock);
    sp->AcceptEx = AcceptEx;
    GPR_ASSERT(sp->socket);
    gpr_mu_unlock(&s->mu);
  }

  return port;
}

int grpc_tcp_server_add_port(grpc_tcp_server *s, const void *addr,
                             int addr_len) {
  int allocated_port = -1;
  unsigned i;
  SOCKET sock;
  struct sockaddr_in6 addr6_v4mapped;
  struct sockaddr_in6 wildcard;
  struct sockaddr *allocated_addr = NULL;
  struct sockaddr_storage sockname_temp;
  socklen_t sockname_len;
  int port;

  /* Check if this is a wildcard port, and if so, try to keep the port the same
     as some previously created listener. */
  if (grpc_sockaddr_get_port(addr) == 0) {
    for (i = 0; i < s->nports; i++) {
      sockname_len = sizeof(sockname_temp);
      if (0 == getsockname(s->ports[i].socket->socket,
                           (struct sockaddr *) &sockname_temp,
                           &sockname_len)) {
        port = grpc_sockaddr_get_port((struct sockaddr *) &sockname_temp);
        if (port > 0) {
          allocated_addr = malloc(addr_len);
          memcpy(allocated_addr, addr, addr_len);
          grpc_sockaddr_set_port(allocated_addr, port);
          addr = allocated_addr;
          break;
        }
      }
    }
  }

  if (grpc_sockaddr_to_v4mapped(addr, &addr6_v4mapped)) {
    addr = (const struct sockaddr *)&addr6_v4mapped;
    addr_len = sizeof(addr6_v4mapped);
  }

  /* Treat :: or 0.0.0.0 as a family-agnostic wildcard. */
  if (grpc_sockaddr_is_wildcard(addr, &port)) {
    grpc_sockaddr_make_wildcard6(port, &wildcard);

    addr = (struct sockaddr *) &wildcard;
    addr_len = sizeof(wildcard);
  }

  sock = WSASocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                   WSA_FLAG_OVERLAPPED);
  if (sock == INVALID_SOCKET) {
    char *utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_ERROR, "unable to create socket: %s", utf8_message);
    gpr_free(utf8_message);
  }

  allocated_port = add_socket_to_server(s, sock, addr, addr_len);
  gpr_free(allocated_addr);

  return allocated_port;
}

SOCKET grpc_tcp_server_get_socket(grpc_tcp_server *s, unsigned index) {
  return (index < s->nports) ? s->ports[index].socket->socket : INVALID_SOCKET;
}

void grpc_tcp_server_start(grpc_tcp_server *s, grpc_pollset **pollset,
                           size_t pollset_count, grpc_tcp_server_cb cb,
                           void *cb_arg) {
  size_t i;
  GPR_ASSERT(cb);
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(!s->cb);
  GPR_ASSERT(s->active_ports == 0);
  s->cb = cb;
  s->cb_arg = cb_arg;
  for (i = 0; i < s->nports; i++) {
    start_accept(s->ports + i);
    s->active_ports++;
  }
  gpr_mu_unlock(&s->mu);
}

#endif  /* GPR_WINSOCK_SOCKET */
