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

#include <grpc/grpc.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/channel/http_server_filter.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

typedef struct server_connect_state {
  grpc_server *server;
  grpc_pollset *accepting_pollset;
  grpc_tcp_server_acceptor *acceptor;
  grpc_handshake_manager *handshake_mgr;
} server_connect_state;

static void on_handshake_done(grpc_exec_ctx *exec_ctx, grpc_endpoint *endpoint,
                              grpc_channel_args *args,
                              gpr_slice_buffer *read_buffer, void *user_data,
                              grpc_error *error) {
  server_connect_state *state = user_data;
  if (error != GRPC_ERROR_NONE) {
    const char *error_str = grpc_error_string(error);
    gpr_log(GPR_ERROR, "Handshaking failed: %s", error_str);
    grpc_error_free_string(error_str);
    GRPC_ERROR_UNREF(error);
    grpc_handshake_manager_shutdown(exec_ctx, state->handshake_mgr);
    gpr_free(read_buffer);
  } else {
    // Beware that the call to grpc_create_chttp2_transport() has to happen
    // before grpc_tcp_server_destroy(). This is fine here, but similar code
    // asynchronously doing a handshake instead of calling
    // grpc_tcp_server_start() (as in server_secure_chttp2.c) needs to add
    // synchronization to avoid this case.
    grpc_transport *transport =
        grpc_create_chttp2_transport(exec_ctx, args, endpoint, 0);
    grpc_server_setup_transport(exec_ctx, state->server, transport,
                                state->accepting_pollset,
                                grpc_server_get_channel_args(state->server));
    grpc_chttp2_transport_start_reading(exec_ctx, transport, read_buffer);
  }
  // Clean up.
  grpc_channel_args_destroy(args);
  grpc_handshake_manager_destroy(exec_ctx, state->handshake_mgr);
  gpr_free(state);
}

static void on_accept(grpc_exec_ctx *exec_ctx, void *server, grpc_endpoint *tcp,
                      grpc_pollset *accepting_pollset,
                      grpc_tcp_server_acceptor *acceptor) {
  server_connect_state *state = gpr_malloc(sizeof(server_connect_state));
  state->server = server;
  state->accepting_pollset = accepting_pollset;
  state->acceptor = acceptor;
  state->handshake_mgr = grpc_handshake_manager_create();
  // TODO(roth): We should really get this timeout value from channel
  // args instead of hard-coding it.
  const gpr_timespec deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_seconds(120, GPR_TIMESPAN));
  grpc_handshake_manager_do_handshake(
      exec_ctx, state->handshake_mgr, tcp, grpc_server_get_channel_args(server),
      deadline, acceptor, on_handshake_done, state);
}

/* Server callback: start listening on our ports */
static void start(grpc_exec_ctx *exec_ctx, grpc_server *server, void *tcpp,
                  grpc_pollset **pollsets, size_t pollset_count) {
  grpc_tcp_server *tcp = tcpp;
  grpc_tcp_server_start(exec_ctx, tcp, pollsets, pollset_count, on_accept,
                        server);
}

/* Server callback: destroy the tcp listener (so we don't generate further
   callbacks) */
static void destroy(grpc_exec_ctx *exec_ctx, grpc_server *server, void *tcpp,
                    grpc_closure *destroy_done) {
  grpc_tcp_server *tcp = tcpp;
  grpc_tcp_server_shutdown_listeners(exec_ctx, tcp);
  grpc_tcp_server_unref(exec_ctx, tcp);
  grpc_exec_ctx_sched(exec_ctx, destroy_done, GRPC_ERROR_NONE, NULL);
}

int grpc_server_add_insecure_http2_port(grpc_server *server, const char *addr) {
  grpc_resolved_addresses *resolved = NULL;
  grpc_tcp_server *tcp = NULL;
  size_t i;
  size_t count = 0;
  int port_num = -1;
  int port_temp;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_error *err = GRPC_ERROR_NONE;

  GRPC_API_TRACE("grpc_server_add_insecure_http2_port(server=%p, addr=%s)", 2,
                 (server, addr));

  grpc_error **errors = NULL;
  err = grpc_blocking_resolve_address(addr, "https", &resolved);
  if (err != GRPC_ERROR_NONE) {
    goto error;
  }

  err =
      grpc_tcp_server_create(NULL, grpc_server_get_channel_args(server), &tcp);
  if (err != GRPC_ERROR_NONE) {
    goto error;
  }

  const size_t naddrs = resolved->naddrs;
  errors = gpr_malloc(sizeof(*errors) * naddrs);
  for (i = 0; i < naddrs; i++) {
    errors[i] = grpc_tcp_server_add_port(
        tcp, (struct sockaddr *)&resolved->addrs[i].addr,
        resolved->addrs[i].len, &port_temp);
    if (errors[i] == GRPC_ERROR_NONE) {
      if (port_num == -1) {
        port_num = port_temp;
      } else {
        GPR_ASSERT(port_num == port_temp);
      }
      count++;
    }
  }
  if (count == 0) {
    char *msg;
    gpr_asprintf(&msg, "No address added out of total %" PRIuPTR " resolved",
                 naddrs);
    err = GRPC_ERROR_CREATE_REFERENCING(msg, errors, naddrs);
    gpr_free(msg);
    goto error;
  } else if (count != naddrs) {
    char *msg;
    gpr_asprintf(&msg, "Only %" PRIuPTR
                       " addresses added out of total %" PRIuPTR " resolved",
                 count, naddrs);
    err = GRPC_ERROR_CREATE_REFERENCING(msg, errors, naddrs);
    gpr_free(msg);

    const char *warning_message = grpc_error_string(err);
    gpr_log(GPR_INFO, "WARNING: %s", warning_message);
    grpc_error_free_string(warning_message);
    /* we managed to bind some addresses: continue */
  }
  grpc_resolved_addresses_destroy(resolved);

  /* Register with the server only upon success */
  grpc_server_add_listener(&exec_ctx, server, tcp, start, destroy);
  goto done;

/* Error path: cleanup and return */
error:
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  if (resolved) {
    grpc_resolved_addresses_destroy(resolved);
  }
  if (tcp) {
    grpc_tcp_server_unref(&exec_ctx, tcp);
  }
  port_num = 0;

  const char *msg = grpc_error_string(err);
  gpr_log(GPR_ERROR, "%s", msg);
  grpc_error_free_string(msg);
  GRPC_ERROR_UNREF(err);

done:
  grpc_exec_ctx_finish(&exec_ctx);
  if (errors != NULL) {
    for (i = 0; i < naddrs; i++) {
      GRPC_ERROR_UNREF(errors[i]);
    }
  }
  gpr_free(errors);
  return port_num;
}
