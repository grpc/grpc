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

#include <io.h>

#include "src/core/lib/iomgr/sockaddr_utils.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/pollset_windows.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/tcp_windows.h"

#define MIN_SAFE_ACCEPT_QUEUE_SIZE 100

/* one listening port */
typedef struct grpc_tcp_listener grpc_tcp_listener;
struct grpc_tcp_listener {
  /* This seemingly magic number comes from AcceptEx's documentation. each
     address buffer needs to have at least 16 more bytes at their end. */
  uint8_t addresses[(sizeof(struct sockaddr_in6) + 16) * 2];
  /* This will hold the socket for the next accept. */
  SOCKET new_socket;
  /* The listener winsocket. */
  grpc_winsocket *socket;
  /* The actual TCP port number. */
  int port;
  unsigned port_index;
  grpc_tcp_server *server;
  /* The cached AcceptEx for that port. */
  LPFN_ACCEPTEX AcceptEx;
  int shutting_down;
  /* closure for socket notification of accept being ready */
  grpc_closure on_accept;
  /* linked list */
  struct grpc_tcp_listener *next;
};

/* the overall server */
struct grpc_tcp_server {
  gpr_refcount refs;
  /* Called whenever accept() succeeds on a server port. */
  grpc_tcp_server_cb on_accept_cb;
  void *on_accept_cb_arg;

  gpr_mu mu;

  /* active port count: how many ports are actually still listening */
  int active_ports;

  /* linked list of server ports */
  grpc_tcp_listener *head;
  grpc_tcp_listener *tail;

  /* List of closures passed to shutdown_starting_add(). */
  grpc_closure_list shutdown_starting;

  /* shutdown callback */
  grpc_closure *shutdown_complete;
};

/* Public function. Allocates the proper data structures to hold a
   grpc_tcp_server. */
grpc_tcp_server *grpc_tcp_server_create(grpc_closure *shutdown_complete) {
  grpc_tcp_server *s = gpr_malloc(sizeof(grpc_tcp_server));
  gpr_ref_init(&s->refs, 1);
  gpr_mu_init(&s->mu);
  s->active_ports = 0;
  s->on_accept_cb = NULL;
  s->on_accept_cb_arg = NULL;
  s->head = NULL;
  s->tail = NULL;
  s->shutdown_starting.head = NULL;
  s->shutdown_starting.tail = NULL;
  s->shutdown_complete = shutdown_complete;
  return s;
}

static void finish_shutdown(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s) {
  if (s->shutdown_complete != NULL) {
    grpc_exec_ctx_enqueue(exec_ctx, s->shutdown_complete, true, NULL);
  }

  /* Now that the accepts have been aborted, we can destroy the sockets.
     The IOCP won't get notified on these, so we can flag them as already
     closed by the system. */
  while (s->head) {
    grpc_tcp_listener *sp = s->head;
    s->head = sp->next;
    sp->next = NULL;
    grpc_winsocket_destroy(sp->socket);
    gpr_free(sp);
  }
  gpr_free(s);
}

grpc_tcp_server *grpc_tcp_server_ref(grpc_tcp_server *s) {
  gpr_ref(&s->refs);
  return s;
}

void grpc_tcp_server_shutdown_starting_add(grpc_tcp_server *s,
                                           grpc_closure *shutdown_starting) {
  gpr_mu_lock(&s->mu);
  grpc_closure_list_add(&s->shutdown_starting, shutdown_starting, 1);
  gpr_mu_unlock(&s->mu);
}

static void tcp_server_destroy(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s) {
  int immediately_done = 0;
  grpc_tcp_listener *sp;
  gpr_mu_lock(&s->mu);

  /* First, shutdown all fd's. This will queue abortion calls for all
     of the pending accepts due to the normal operation mechanism. */
  if (s->active_ports == 0) {
    immediately_done = 1;
  }
  for (sp = s->head; sp; sp = sp->next) {
    sp->shutting_down = 1;
    grpc_winsocket_shutdown(sp->socket);
  }
  gpr_mu_unlock(&s->mu);

  if (immediately_done) {
    finish_shutdown(exec_ctx, s);
  }
}

void grpc_tcp_server_unref(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s) {
  if (gpr_unref(&s->refs)) {
    /* Complete shutdown_starting work before destroying. */
    grpc_exec_ctx local_exec_ctx = GRPC_EXEC_CTX_INIT;
    gpr_mu_lock(&s->mu);
    grpc_exec_ctx_enqueue_list(&local_exec_ctx, &s->shutdown_starting, NULL);
    gpr_mu_unlock(&s->mu);
    if (exec_ctx == NULL) {
      grpc_exec_ctx_flush(&local_exec_ctx);
      tcp_server_destroy(&local_exec_ctx, s);
      grpc_exec_ctx_finish(&local_exec_ctx);
    } else {
      grpc_exec_ctx_finish(&local_exec_ctx);
      tcp_server_destroy(exec_ctx, s);
    }
  }
}

/* Prepare (bind) a recently-created socket for listening. */
static int prepare_socket(SOCKET sock, const struct sockaddr *addr,
                          size_t addr_len) {
  struct sockaddr_storage sockname_temp;
  socklen_t sockname_len;

  if (sock == INVALID_SOCKET) goto error;

  if (!grpc_tcp_prepare_socket(sock)) {
    char *utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_ERROR, "Unable to prepare socket: %s", utf8_message);
    gpr_free(utf8_message);
    goto error;
  }

  if (bind(sock, addr, (int)addr_len) == SOCKET_ERROR) {
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

static void decrement_active_ports_and_notify(grpc_exec_ctx *exec_ctx,
                                              grpc_tcp_listener *sp) {
  int notify = 0;
  sp->shutting_down = 0;
  gpr_mu_lock(&sp->server->mu);
  GPR_ASSERT(sp->server->active_ports > 0);
  if (0 == --sp->server->active_ports) {
    notify = 1;
  }
  gpr_mu_unlock(&sp->server->mu);
  if (notify) {
    finish_shutdown(exec_ctx, sp->server);
  }
}

/* In order to do an async accept, we need to create a socket first which
   will be the one assigned to the new incoming connection. */
static void start_accept(grpc_exec_ctx *exec_ctx, grpc_tcp_listener *port) {
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
  grpc_socket_notify_on_read(exec_ctx, port->socket, &port->on_accept);
  return;

failure:
  if (port->shutting_down) {
    /* We are abandoning the listener port, take that into account to prevent
       occasional hangs on shutdown. The hang happens when sp->shutting_down
       change is not seen by on_accept and we proceed to trying new accept,
       but we fail there because the listening port has been closed in the
       meantime. */
    decrement_active_ports_and_notify(exec_ctx, port);
    return;
  }
  utf8_message = gpr_format_message(WSAGetLastError());
  gpr_log(GPR_ERROR, message, utf8_message);
  gpr_free(utf8_message);
  if (sock != INVALID_SOCKET) closesocket(sock);
}

/* Event manager callback when reads are ready. */
static void on_accept(grpc_exec_ctx *exec_ctx, void *arg, bool from_iocp) {
  grpc_tcp_listener *sp = arg;
  grpc_tcp_server_acceptor acceptor = {sp->server, sp->port_index, 0};
  SOCKET sock = sp->new_socket;
  grpc_winsocket_callback_info *info = &sp->socket->read_info;
  grpc_endpoint *ep = NULL;
  struct sockaddr_storage peer_name;
  char *peer_name_string;
  char *fd_name;
  int peer_name_len = sizeof(peer_name);
  DWORD transfered_bytes;
  DWORD flags;
  BOOL wsa_success;
  int err;

  /* The general mechanism for shutting down is to queue abortion calls. While
     this is necessary in the read/write case, it's useless for the accept
     case. We only need to adjust the pending callback count */
  if (!from_iocp) {
    return;
  }

  /* The IOCP notified us of a completed operation. Let's grab the results,
     and act accordingly. */
  transfered_bytes = 0;
  wsa_success = WSAGetOverlappedResult(sock, &info->overlapped,
                                       &transfered_bytes, FALSE, &flags);
  if (!wsa_success) {
    if (sp->shutting_down) {
      /* During the shutdown case, we ARE expecting an error. So that's well,
         and we can wake up the shutdown thread. */
      decrement_active_ports_and_notify(exec_ctx, sp);
      return;
    } else {
      char *utf8_message = gpr_format_message(WSAGetLastError());
      gpr_log(GPR_ERROR, "on_accept error: %s", utf8_message);
      gpr_free(utf8_message);
      closesocket(sock);
    }
  } else {
    if (!sp->shutting_down) {
      peer_name_string = NULL;
      err = setsockopt(sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                       (char *)&sp->socket->socket, sizeof(sp->socket->socket));
      if (err) {
        char *utf8_message = gpr_format_message(WSAGetLastError());
        gpr_log(GPR_ERROR, "setsockopt error: %s", utf8_message);
        gpr_free(utf8_message);
      }
      err = getpeername(sock, (struct sockaddr *)&peer_name, &peer_name_len);
      if (!err) {
        peer_name_string = grpc_sockaddr_to_uri((struct sockaddr *)&peer_name);
      } else {
        char *utf8_message = gpr_format_message(WSAGetLastError());
        gpr_log(GPR_ERROR, "getpeername error: %s", utf8_message);
        gpr_free(utf8_message);
      }
      gpr_asprintf(&fd_name, "tcp_server:%s", peer_name_string);
      ep = grpc_tcp_create(grpc_winsocket_create(sock, fd_name),
                           peer_name_string);
      gpr_free(fd_name);
      gpr_free(peer_name_string);
    } else {
      closesocket(sock);
    }
  }

  /* The only time we should call our callback, is where we successfully
     managed to accept a connection, and created an endpoint. */
  if (ep) {
    sp->server->on_accept_cb(exec_ctx, sp->server->on_accept_cb_arg, ep, NULL,
                             &acceptor);
  }
  /* As we were notified from the IOCP of one and exactly one accept,
     the former socked we created has now either been destroy or assigned
     to the new connection. We need to create a new one for the next
     connection. */
  start_accept(exec_ctx, sp);
}

static grpc_tcp_listener *add_socket_to_server(grpc_tcp_server *s, SOCKET sock,
                                               const struct sockaddr *addr,
                                               size_t addr_len,
                                               unsigned port_index) {
  grpc_tcp_listener *sp = NULL;
  int port;
  int status;
  GUID guid = WSAID_ACCEPTEX;
  DWORD ioctl_num_bytes;
  LPFN_ACCEPTEX AcceptEx;

  if (sock == INVALID_SOCKET) return NULL;

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
    return NULL;
  }

  port = prepare_socket(sock, addr, addr_len);
  if (port >= 0) {
    gpr_mu_lock(&s->mu);
    GPR_ASSERT(!s->on_accept_cb && "must add ports before starting server");
    sp = gpr_malloc(sizeof(grpc_tcp_listener));
    sp->next = NULL;
    if (s->head == NULL) {
      s->head = sp;
    } else {
      s->tail->next = sp;
    }
    s->tail = sp;
    sp->server = s;
    sp->socket = grpc_winsocket_create(sock, "listener");
    sp->shutting_down = 0;
    sp->AcceptEx = AcceptEx;
    sp->new_socket = INVALID_SOCKET;
    sp->port = port;
    sp->port_index = port_index;
    grpc_closure_init(&sp->on_accept, on_accept, sp);
    GPR_ASSERT(sp->socket);
    gpr_mu_unlock(&s->mu);
  }

  return sp;
}

int grpc_tcp_server_add_port(grpc_tcp_server *s, const void *addr,
                             size_t addr_len) {
  grpc_tcp_listener *sp;
  SOCKET sock;
  struct sockaddr_in6 addr6_v4mapped;
  struct sockaddr_in6 wildcard;
  struct sockaddr *allocated_addr = NULL;
  struct sockaddr_storage sockname_temp;
  socklen_t sockname_len;
  int port;
  unsigned port_index = 0;
  if (s->tail != NULL) {
    port_index = s->tail->port_index + 1;
  }

  /* Check if this is a wildcard port, and if so, try to keep the port the same
     as some previously created listener. */
  if (grpc_sockaddr_get_port(addr) == 0) {
    for (sp = s->head; sp; sp = sp->next) {
      sockname_len = sizeof(sockname_temp);
      if (0 == getsockname(sp->socket->socket,
                           (struct sockaddr *)&sockname_temp, &sockname_len)) {
        port = grpc_sockaddr_get_port((struct sockaddr *)&sockname_temp);
        if (port > 0) {
          allocated_addr = gpr_malloc(addr_len);
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

  sp = add_socket_to_server(s, sock, addr, addr_len, port_index);
  gpr_free(allocated_addr);

  if (sp) {
    return sp->port;
  } else {
    return -1;
  }
}

void grpc_tcp_server_start(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s,
                           grpc_pollset **pollset, size_t pollset_count,
                           grpc_tcp_server_cb on_accept_cb,
                           void *on_accept_cb_arg) {
  grpc_tcp_listener *sp;
  GPR_ASSERT(on_accept_cb);
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(!s->on_accept_cb);
  GPR_ASSERT(s->active_ports == 0);
  s->on_accept_cb = on_accept_cb;
  s->on_accept_cb_arg = on_accept_cb_arg;
  for (sp = s->head; sp; sp = sp->next) {
    start_accept(exec_ctx, sp);
    s->active_ports++;
  }
  gpr_mu_unlock(&s->mu);
}

#endif /* GPR_WINSOCK_SOCKET */
