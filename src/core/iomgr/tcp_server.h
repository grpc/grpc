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

#ifndef GRPC_INTERNAL_CORE_IOMGR_TCP_SERVER_H
#define GRPC_INTERNAL_CORE_IOMGR_TCP_SERVER_H

#include "src/core/iomgr/endpoint.h"

/* Forward decl of grpc_tcp_server */
typedef struct grpc_tcp_server grpc_tcp_server;

/* Forward decl of grpc_tcp_listener */
typedef struct grpc_tcp_listener grpc_tcp_listener;

/* Called for newly connected TCP connections. */
typedef void (*grpc_tcp_server_cb)(grpc_exec_ctx *exec_ctx, void *arg,
                                   grpc_endpoint *ep);

/* Create a server, initially not bound to any ports */
grpc_tcp_server *grpc_tcp_server_create(void);

/* Start listening to bound ports */
void grpc_tcp_server_start(grpc_exec_ctx *exec_ctx, grpc_tcp_server *server,
                           grpc_pollset **pollsets, size_t pollset_count,
                           grpc_tcp_server_cb on_accept_cb, void *cb_arg);

/* Add a port to the server, returning the newly created listener on success,
   or a null pointer on failure.

   The :: and 0.0.0.0 wildcard addresses are treated identically, accepting
   both IPv4 and IPv6 connections, but :: is the preferred style.  This usually
   creates one socket, but possibly two on systems which support IPv6,
   but not dualstack sockets. */
/* TODO(ctiller): deprecate this, and make grpc_tcp_server_add_ports to handle
                  all of the multiple socket port matching logic in one place */
grpc_tcp_listener *grpc_tcp_server_add_port(grpc_tcp_server *s,
                                            const void *addr, size_t addr_len);

/* Returns the file descriptor of the Nth listening socket on this server,
   or -1 if the index is out of bounds.

   The file descriptor remains owned by the server, and will be cleaned
   up when grpc_tcp_server_destroy is called. */
int grpc_tcp_server_get_fd(grpc_tcp_server *s, unsigned index);

void grpc_tcp_server_destroy(grpc_exec_ctx *exec_ctx, grpc_tcp_server *server,
                             grpc_closure *closure);

int grpc_tcp_listener_get_port(grpc_tcp_listener *listener);
void grpc_tcp_listener_ref(grpc_tcp_listener *listener);
void grpc_tcp_listener_unref(grpc_tcp_listener *listener);

#endif /* GRPC_INTERNAL_CORE_IOMGR_TCP_SERVER_H */
