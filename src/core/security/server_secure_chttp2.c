/*
 *
 * Copyright 2014, Google Inc.
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

#include <grpc/grpc.h>

#include "src/core/channel/http_filter.h"
#include "src/core/channel/http_server_filter.h"
#include "src/core/iomgr/resolve_address.h"
#include "src/core/iomgr/tcp_server.h"
#include "src/core/security/security_context.h"
#include "src/core/security/secure_transport_setup.h"
#include "src/core/surface/server.h"
#include "src/core/transport/chttp2_transport.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

static grpc_transport_setup_result setup_transport(void *server,
                                                   grpc_transport *transport,
                                                   grpc_mdctx *mdctx) {
  static grpc_channel_filter const *extra_filters[] = {&grpc_http_server_filter,
                                                       &grpc_http_filter};
  return grpc_server_setup_transport(server, transport, extra_filters,
                                     GPR_ARRAY_SIZE(extra_filters), mdctx);
}

static void on_secure_transport_setup_done(void *server,
                                           grpc_security_status status,
                                           grpc_endpoint *secure_endpoint) {
  if (status == GRPC_SECURITY_OK) {
    grpc_create_chttp2_transport(
        setup_transport, server, grpc_server_get_channel_args(server),
        secure_endpoint, NULL, 0, grpc_mdctx_create(), 0);
  } else {
    gpr_log(GPR_ERROR, "Secure transport failed with error %d", status);
  }
}

static void on_accept(void *server, grpc_endpoint *tcp) {
  const grpc_channel_args *args = grpc_server_get_channel_args(server);
  grpc_security_context *ctx = grpc_find_security_context_in_args(args);
  GPR_ASSERT(ctx);
  grpc_setup_secure_transport(ctx, tcp, on_secure_transport_setup_done, server);
}

/* Note: the following code is the same with server_chttp2.c */

/* Server callback: start listening on our ports */
static void start(grpc_server *server, void *tcpp, grpc_pollset *pollset) {
  grpc_tcp_server *tcp = tcpp;
  grpc_tcp_server_start(tcp, pollset, on_accept, server);
}

/* Server callback: destroy the tcp listener (so we don't generate further
   callbacks) */
static void destroy(grpc_server *server, void *tcpp) {
  grpc_tcp_server *tcp = tcpp;
  grpc_tcp_server_destroy(tcp);
}

int grpc_server_add_secure_http2_port(grpc_server *server, const char *addr) {
  grpc_resolved_addresses *resolved = NULL;
  grpc_tcp_server *tcp = NULL;
  size_t i;
  int count = 0;

  resolved = grpc_blocking_resolve_address(addr, "https");
  if (!resolved) {
    goto error;
  }

  tcp = grpc_tcp_server_create();
  if (!tcp) {
    goto error;
  }

  for (i = 0; i < resolved->naddrs; i++) {
    if (grpc_tcp_server_add_port(tcp,
                                 (struct sockaddr *)&resolved->addrs[i].addr,
                                 resolved->addrs[i].len)) {
      count++;
    }
  }
  if (count == 0) {
    gpr_log(GPR_ERROR, "No address added out of total %d resolved",
            resolved->naddrs);
    goto error;
  }
  if (count != resolved->naddrs) {
    gpr_log(GPR_ERROR, "Only %d addresses added out of total %d resolved",
            count, resolved->naddrs);
  }
  grpc_resolved_addresses_destroy(resolved);

  /* Register with the server only upon success */
  grpc_server_add_listener(server, tcp, start, destroy);

  return 1;

/* Error path: cleanup and return */
error:
  if (resolved) {
    grpc_resolved_addresses_destroy(resolved);
  }
  if (tcp) {
    grpc_tcp_server_destroy(tcp);
  }
  return 0;
}
