/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef SRC_CORE_LIB_TUNNEL_TUNNEL_H_
#define SRC_CORE_LIB_TUNNEL_TUNNEL_H_

#include <grpc/impl/codegen/status.h>
#include <grpc/support/time.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/tunnel/tunnel_server_listener.h"

typedef struct grpc_tunnel_vtable grpc_tunnel_vtable;
typedef struct grpc_tunnel grpc_tunnel;

typedef struct grpc_tunnel {
  const grpc_tunnel_vtable *vtable;
  gpr_atm next_tag;
  grpc_channel_args *tunnel_args;
} grpc_tunnel;

typedef struct grpc_tunnel_vtable {
  grpc_channel* (*tunnel_channel_create)(
      const char* target, const grpc_channel_args *args, void *reserved,
      grpc_tunnel *tunnel);
  void (*tunnel_channel_endpoint_create)(grpc_tunnel *tunnelp,
      grpc_exec_ctx *exec_ctx, grpc_closure *closure, grpc_endpoint **ep,
      grpc_pollset_set *interested_parties,
      const struct sockaddr *addr, size_t addr_len, gpr_timespec deadline);
  int (*server_add_tunnel)(grpc_server *server, const char *addr,
      grpc_tunnel *tunnel);
  void (*on_tunnel_server_listener_start)(tunnel_server_listener *listener);
  void  (*start)(grpc_tunnel *tunnel);
  void  (*shutdown)(grpc_tunnel *tunnel);
  void  (*destroy)(grpc_tunnel *tunnel);
} grpc_tunnel_vtable;

#define TUNNEL_DEFAULT_SHUTDOWN_TIMEOUT_MS 100

/** Internal functions for use only by authoritative and non-authoritative
    tunnel implementation. tunnel and tunnel_args are owned by the
    implementation this point onwards until destroy_tunnel_internal() */
void tunnel_internal_init(grpc_tunnel* tunnel,
                          grpc_channel_args *tunnel_args,
                          const grpc_tunnel_vtable *vtable);

/** Internal function for use only by authoritative and non-authoritative
    tunnel implementation to get exclusive tags for use with completion
    queues. */
void *tunnel_get_next_tag(grpc_tunnel* tunnel);

/** Internal function for use only by authoritative and non-authoritative
    tunnel implementation to drain the associated completion queue. */
void completion_queue_drain(grpc_tunnel* tunnel,
                            grpc_completion_queue* cq);

/** Internal function for use only by authoritative and non-authoritative
    tunnel implementation to get the shutdown timeout settings from
    tunnel_args. */
gpr_timespec tunnel_get_shutdown_timeout(grpc_tunnel* tunnel);

#endif /* SRC_CORE_LIB_TUNNEL_TUNNEL_H_ */
