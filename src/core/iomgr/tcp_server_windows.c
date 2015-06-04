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
#include <grpc/support/string_util.h>
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
  /* This seemingly magic number comes from AcceptEx's documentation. each
     address buffer needs to have at least 16 more bytes at their end. */
  gpr_uint8 addresses[(sizeof(struct sockaddr_in6) + 16) * 2];
  /* This will hold the socket for the next accept. */
  SOCKET new_socket;
  /* The listener winsocked. */
  grpc_winsocket *socket;
  grpc_tcp_server *server;
  /* The cached AcceptEx for that port. */
  LPFN_ACCEPTEX AcceptEx;
  int shutting_down;
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

/* Public function. Allocates the proper data structures to hold a
   grpc_tcp_server. */
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

/* Public function. Stops and destroys a grpc_tcp_server. */
void grpc_tcp_server_destroy(grpc_tcp_server *s,
                             void (*shutdown_done)(void *shutdown_done_arg),
                             void *shutdown_done_arg) {
  size_t i;
  gpr_mu_lock(&s->mu);
  /* First, shutdown all fd's. This will queue abortion calls for all
     of the pending accepts. */
  for (i = 0; i < s->nports; i++) {
    server_port *sp = &s->ports[i];
    grpc_winsocket_shutdown(sp->socket);
  }
  /* This happens asynchronously. Wait while that happens. */
  while (s->active_ports) {
    gpr_cv_wait(&s->cv, &s->mu, gpr_inf_future);
  }
  gpr_mu_unlock(&s->mu);

  /* Now that the accepts have been aborted, we can destroy the sockets.
     The IOCP won't get notified on these, so we can flag them as already
     closed by the system. */
  for (i = 0; i < s->nports; i++) {
    server_port *sp = &s->ports[i];
    grpc_winsocket_orphan(sp->socket);
  }
  gpr_free(s->ports);
  gpr_free(s);

  if (shutdown_done) {
    shutdown_done(shutdown_done_arg);
  }
}

/* Prepare (bind) a recently-created socket for listening. */
static int prepare_socket(SOCKET sock, const struct sockaddr *addr,
                          int addr_len) {
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
  if (getsockname(sock, (struct sockaddr *)&sockname_temp, &sockname_len) ==
      SOCKET_ERROR) {
    char *utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_ERROR, "getsockname: %s", utf8_message);
    gpr_free(utf8_message);
    goto error;
  }

  return grpc_sockaddr_get_port((struct sockaddr *)&sockname_temp);

error:
  if (sock != INVALID_SOCKET) closesocket(sock);
  return -1;
}

/* start_accept will reference that for the IOCP notification request. */
static void on_accept(void *arg, int from_iocp);

/* In order to do an async accept, we need to create a socket first which
   will be the one assigned to the new incoming connection. */
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

  /* Start the "accept" asynchronously. */
  success = port->AcceptEx(port->socket->socket, sock, port->addresses, 0,
                           addrlen, addrlen, &bytes_received,
                           &port->socket->read_info.overlapped);

  /* It is possible to get an accept immediately without delay. However, we
     will still get an IOCP notification for it. So let's just ignore it. */
  if (!success) {
    int error = WSAGetLastError();
    if (error != ERROR_IO_PENDING) {
      message = "AcceptEx failed: %s";
      goto failure;
    }
  }

  /* We're ready to do the accept. Calling grpc_socket_notify_on_read may
     immediately process an accept that happened in the meantime. */
  port->new_socket = sock;
  grpc_socket_notify_on_read(port->socket, on_accept, port);
  return;

failure:
  utf8_message = gpr_format_message(WSAGetLastError());
  gpr_log(GPR_ERROR, message, utf8_message);
  gpr_free(utf8_message);
  if (sock != INVALID_SOCKET) closesocket(sock);
}

/* Event manager callback when reads are ready. */
static void on_accept(void *arg, int from_iocp) {
  server_port *sp = arg;
  SOCKET sock = sp->new_socket;
  grpc_winsocket_callback_info *info = &sp->socket->read_info;
  grpc_endpoint *ep = NULL;

  /* The shutdown sequence is done in two parts. This is the second
     part here, acknowledging the IOCP notification, and doing nothing
     else, especially not queuing a new accept. */
  if (sp->shutting_down) {
    GPR_ASSERT(from_iocp);
    sp->shutting_down = 0;
    sp->socket->read_info.outstanding = 0;
    gpr_mu_lock(&sp->server->mu);
    if (0 == --sp->server->active_ports) {
      gpr_cv_broadcast(&sp->server->cv);
    }
    gpr_mu_unlock(&sp->server->mu);
    return;
  }

  if (from_iocp) {
    /* The IOCP notified us of a completed operation. Let's grab the results,
       and act accordingly. */
    DWORD transfered_bytes = 0;
    DWORD flags;
    BOOL wsa_success = WSAGetOverlappedResult(sock, &info->overlapped,
                                              &transfered_bytes, FALSE, &flags);
    if (!wsa_success) {
      char *utf8_message = gpr_format_message(WSAGetLastError());
      gpr_log(GPR_ERROR, "on_accept error: %s", utf8_message);
      gpr_free(utf8_message);
      closesocket(sock);
    } else {
	  /* TODO(ctiller): add sockaddr address to label */
      ep = grpc_tcp_create(grpc_winsocket_create(sock, "server"));
    }
  } else {
    /* If we're not notified from the IOCP, it means we are asked to shutdown.
       This will initiate that shutdown. Calling closesocket will trigger an
       IOCP notification, that will call this function a second time, from
       the IOCP thread. Of course, this only works if the socket was, in fact,
       listening. If that's not the case, we'd wait indefinitely. That's a bit
       of a degenerate case, but it can happen if you create a server, but
       don't start it. So let's support that by recursing once. */
    sp->shutting_down = 1;
    sp->new_socket = INVALID_SOCKET;
    if (sock != INVALID_SOCKET) {
      closesocket(sock);
    } else {
      on_accept(sp, 1);
    }
    return;
  }

  /* The only time we should call our callback, is where we successfully
     managed to accept a connection, and created an endpoint. */
  if (ep) sp->server->cb(sp->server->cb_arg, ep);
  /* As we were notified from the IOCP of one and exactly one accept,
      the former socked we created has now either been destroy or assigned
      to the new connection. We need to create a new one for the next
      connection. */
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

  /* We need to grab the AcceptEx pointer for that port, as it may be
     interface-dependent. We'll cache it to avoid doing that again. */
  status =
      WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
               &AcceptEx, sizeof(AcceptEx), &ioctl_num_bytes, NULL, NULL);

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
    sp->socket = grpc_winsocket_create(sock, "listener");
    sp->shutting_down = 0;
    sp->AcceptEx = AcceptEx;
    sp->new_socket = INVALID_SOCKET;
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
                           (struct sockaddr *)&sockname_temp, &sockname_len)) {
        port = grpc_sockaddr_get_port((struct sockaddr *)&sockname_temp);
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

    addr = (struct sockaddr *)&wildcard;
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
    s->ports[i].socket->read_info.outstanding = 1;
    start_accept(s->ports + i);
    s->active_ports++;
  }
  gpr_mu_unlock(&s->mu);
}

#endif /* GPR_WINSOCK_SOCKET */
