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

#ifndef GRPC_CORE_LIB_SURFACE_SERVER_H
#define GRPC_CORE_LIB_SURFACE_SERVER_H

#include <grpc/grpc.h>
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/transport/transport.h"

extern const grpc_channel_filter grpc_server_top_filter;

/* Add a listener to the server: when the server starts, it will call start,
   and when it shuts down, it will call destroy */
void grpc_server_add_listener(
    grpc_exec_ctx *exec_ctx, grpc_server *server, void *listener,
    void (*start)(grpc_exec_ctx *exec_ctx, grpc_server *server, void *arg,
                  grpc_pollset **pollsets, size_t npollsets),
    void (*destroy)(grpc_exec_ctx *exec_ctx, grpc_server *server, void *arg,
                    grpc_closure *on_done));

/* Setup a transport - creates a channel stack, binds the transport to the
   server */
void grpc_server_setup_transport(grpc_exec_ctx *exec_ctx, grpc_server *server,
                                 grpc_transport *transport,
                                 grpc_pollset *accepting_pollset,
                                 const grpc_channel_args *args);

const grpc_channel_args *grpc_server_get_channel_args(grpc_server *server);

int grpc_server_has_open_connections(grpc_server *server);

#endif /* GRPC_CORE_LIB_SURFACE_SERVER_H */
