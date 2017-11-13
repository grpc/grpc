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

#ifndef GRPC_CORE_LIB_IOMGR_UDP_SERVER_H
#define GRPC_CORE_LIB_IOMGR_UDP_SERVER_H

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/resolve_address.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward decl of struct grpc_server */
/* This is not typedef'ed to avoid a typedef-redefinition error */
struct grpc_server;

/* Forward decl of grpc_udp_server */
typedef struct grpc_udp_server grpc_udp_server;

/* Called when data is available to read from the socket. */
typedef void (*grpc_udp_server_read_cb)(grpc_exec_ctx* exec_ctx, grpc_fd* emfd,
                                        void* user_data);

/* Called when the socket is writeable. */
typedef void (*grpc_udp_server_write_cb)(grpc_exec_ctx* exec_ctx, grpc_fd* emfd,
                                         void* user_data);

/* Called when the grpc_fd is about to be orphaned (and the FD closed). */
typedef void (*grpc_udp_server_orphan_cb)(grpc_exec_ctx* exec_ctx,
                                          grpc_fd* emfd,
                                          grpc_closure* shutdown_fd_callback,
                                          void* user_data);

/* Create a server, initially not bound to any ports */
grpc_udp_server* grpc_udp_server_create(const grpc_channel_args* args);

/* Start listening to bound ports. user_data is passed to callbacks. */
void grpc_udp_server_start(grpc_exec_ctx* exec_ctx, grpc_udp_server* udp_server,
                           grpc_pollset** pollsets, size_t pollset_count,
                           void* user_data);

int grpc_udp_server_get_fd(grpc_udp_server* s, unsigned port_index);

/* Add a port to the server, returning port number on success, or negative
   on failure.

   The :: and 0.0.0.0 wildcard addresses are treated identically, accepting
   both IPv4 and IPv6 connections, but :: is the preferred style.  This usually
   creates one socket, but possibly two on systems which support IPv6,
   but not dualstack sockets. */

/* TODO(ctiller): deprecate this, and make grpc_udp_server_add_ports to handle
                  all of the multiple socket port matching logic in one place */
int grpc_udp_server_add_port(grpc_udp_server* s,
                             const grpc_resolved_address* addr,
                             grpc_udp_server_read_cb read_cb,
                             grpc_udp_server_write_cb write_cb,
                             grpc_udp_server_orphan_cb orphan_cb);

void grpc_udp_server_destroy(grpc_exec_ctx* exec_ctx, grpc_udp_server* server,
                             grpc_closure* on_done);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_IOMGR_UDP_SERVER_H */
