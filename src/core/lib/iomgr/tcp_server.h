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

#ifndef GRPC_CORE_LIB_IOMGR_TCP_SERVER_H
#define GRPC_CORE_LIB_IOMGR_TCP_SERVER_H

#include <grpc/grpc.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"

/* Forward decl of grpc_tcp_server */
typedef struct grpc_tcp_server grpc_tcp_server;

typedef struct grpc_tcp_server_acceptor {
  /* grpc_tcp_server_cb functions share a ref on from_server that is valid
     until the function returns. */
  grpc_tcp_server *from_server;
  /* Indices that may be passed to grpc_tcp_server_port_fd(). */
  unsigned port_index;
  unsigned fd_index;
} grpc_tcp_server_acceptor;

/* Called for newly connected TCP connections. */
typedef void (*grpc_tcp_server_cb)(grpc_exec_ctx *exec_ctx, void *arg,
                                   grpc_endpoint *ep,
                                   grpc_pollset *accepting_pollset,
                                   grpc_tcp_server_acceptor *acceptor);

/* Create a server, initially not bound to any ports. The caller owns one ref.
   If shutdown_complete is not NULL, it will be used by
   grpc_tcp_server_unref() when the ref count reaches zero. */
grpc_error *grpc_tcp_server_create(grpc_closure *shutdown_complete,
                                   const grpc_channel_args *args,
                                   grpc_tcp_server **server);

/* Start listening to bound ports */
void grpc_tcp_server_start(grpc_exec_ctx *exec_ctx, grpc_tcp_server *server,
                           grpc_pollset **pollsets, size_t pollset_count,
                           grpc_tcp_server_cb on_accept_cb, void *cb_arg);

/* Add a port to the server, returning the newly allocated port on success, or
   -1 on failure.

   The :: and 0.0.0.0 wildcard addresses are treated identically, accepting
   both IPv4 and IPv6 connections, but :: is the preferred style.  This usually
   creates one socket, but possibly two on systems which support IPv6,
   but not dualstack sockets. */
/* TODO(ctiller): deprecate this, and make grpc_tcp_server_add_ports to handle
                  all of the multiple socket port matching logic in one place */
grpc_error *grpc_tcp_server_add_port(grpc_tcp_server *s, const void *addr,
                                     size_t addr_len, int *out_port);

/* Number of fds at the given port_index, or 0 if port_index is out of
   bounds. */
unsigned grpc_tcp_server_port_fd_count(grpc_tcp_server *s, unsigned port_index);

/* Returns the file descriptor of the Mth (fd_index) listening socket of the Nth
   (port_index) call to add_port() on this server, or -1 if the indices are out
   of bounds. The file descriptor remains owned by the server, and will be
   cleaned up when the ref count reaches zero. */
int grpc_tcp_server_port_fd(grpc_tcp_server *s, unsigned port_index,
                            unsigned fd_index);

/* Ref s and return s. */
grpc_tcp_server *grpc_tcp_server_ref(grpc_tcp_server *s);

/* shutdown_starting is called when ref count has reached zero and the server is
   about to be destroyed. The server will be deleted after it returns. Calling
   grpc_tcp_server_ref() from it has no effect. */
void grpc_tcp_server_shutdown_starting_add(grpc_tcp_server *s,
                                           grpc_closure *shutdown_starting);

/* If the refcount drops to zero, delete s, and call (exec_ctx==NULL) or enqueue
   a call (exec_ctx!=NULL) to shutdown_complete. */
void grpc_tcp_server_unref(grpc_exec_ctx *exec_ctx, grpc_tcp_server *s);

#endif /* GRPC_CORE_LIB_IOMGR_TCP_SERVER_H */
