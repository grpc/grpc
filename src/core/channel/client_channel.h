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

#ifndef GRPC_INTERNAL_CORE_CHANNEL_CLIENT_CHANNEL_H
#define GRPC_INTERNAL_CORE_CHANNEL_CLIENT_CHANNEL_H

#include "src/core/channel/channel_stack.h"
#include "src/core/client_config/resolver.h"

/* A client channel is a channel that begins disconnected, and can connect
   to some endpoint on demand. If that endpoint disconnects, it will be
   connected to again later.

   Calls on a disconnected client channel are queued until a connection is
   established. */

extern const grpc_channel_filter grpc_client_channel_filter;

/* post-construction initializer to let the client channel know which
   transport setup it should cancel upon destruction, or initiate when it needs
   a connection */
void grpc_client_channel_set_resolver(grpc_exec_ctx *exec_ctx,
                                      grpc_channel_stack *channel_stack,
                                      grpc_resolver *resolver);

grpc_connectivity_state grpc_client_channel_check_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem, int try_to_connect);

void grpc_client_channel_watch_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem, grpc_pollset *pollset,
    grpc_connectivity_state *state, grpc_closure *on_complete);

#endif /* GRPC_INTERNAL_CORE_CHANNEL_CLIENT_CHANNEL_H */
