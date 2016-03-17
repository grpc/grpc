/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include <string.h>

#include "src/core/channel/channel_args.h"
#include "src/core/channel/http_server_filter.h"
#include "src/core/iomgr/endpoint.h"
#include "src/core/iomgr/resolve_address.h"
#include "src/core/iomgr/tcp_server.h"
#include "src/core/security/auth_filters.h"
#include "src/core/security/credentials.h"
#include "src/core/security/security_connector.h"
#include "src/core/security/security_context.h"
#include "src/core/surface/api_trace.h"
#include "src/core/surface/server.h"
#include "src/core/transport/chttp2_transport.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

typedef struct grpc_server_secure_state {
  grpc_server *server;
  grpc_tcp_server *tcp;
  grpc_server_security_connector *sc;
  grpc_server_credentials *creds;
  int is_shutdown;
  gpr_mu mu;
  gpr_refcount refcount;
  grpc_closure destroy_closure;
  grpc_closure *destroy_callback;
} grpc_server_secure_state;

static void state_ref(grpc_server_secure_state *state) {
  gpr_ref(&state->refcount);
}

static void state_unref(grpc_server_secure_state *state) {
  if (gpr_unref(&state->refcount)) {
    /* ensure all threads have unlocked */
    gpr_mu_lock(&state->mu);
    gpr_mu_unlock(&state->mu);
    /* clean up */
    GRPC_SECURITY_CONNECTOR_UNREF(&state->sc->base, "server");
    grpc_server_credentials_unref(state->creds);
    gpr_free(state);
  }
}

static void setup_transport(grpc_exec_ctx *exec_ctx, void *statep,
                            grpc_transport *transport,
                            grpc_auth_context *auth_context) {
  static grpc_channel_filter const *extra_filters[] = {
      &grpc_server_auth_filter, &grpc_http_server_filter};
  grpc_server_secure_state *state = statep;
  grpc_channel_args *args_copy;
  grpc_arg args_to_add[2];
  args_to_add[0] = grpc_server_credentials_to_arg(state->creds);
  args_to_add[1] = grpc_auth_context_to_arg(auth_context);
  args_copy = grpc_channel_args_copy_and_add(
      grpc_server_get_channel_args(state->server), args_to_add,
      GPR_ARRAY_SIZE(args_to_add));
  grpc_server_setup_transport(exec_ctx, state->server, transport, extra_filters,
                              GPR_ARRAY_SIZE(extra_filters), args_copy);
  grpc_channel_args_destroy(args_copy);
}

static void on_secure_handshake_done(grpc_exec_ctx *exec_ctx, void *statep,
                                     grpc_security_status status,
                                     grpc_endpoint *secure_endpoint,
                                     grpc_auth_context *auth_context) {
  grpc_server_secure_state *state = statep;
  grpc_transport *transport;
  if (status == GRPC_SECURITY_OK) {
    if (secure_endpoint) {
      gpr_mu_lock(&state->mu);
      if (!state->is_shutdown) {
        transport = grpc_create_chttp2_transport(
            exec_ctx, grpc_server_get_channel_args(state->server),
            secure_endpoint, 0);
        setup_transport(exec_ctx, state, transport, auth_context);
        grpc_chttp2_transport_start_reading(exec_ctx, transport, NULL, 0);
      } else {
        /* We need to consume this here, because the server may already have
         * gone away. */
        grpc_endpoint_destroy(exec_ctx, secure_endpoint);
      }
      gpr_mu_unlock(&state->mu);
    }
  } else {
    gpr_log(GPR_ERROR, "Secure transport failed with error %d", status);
  }
  state_unref(state);
}

static void on_accept(grpc_exec_ctx *exec_ctx, void *statep, grpc_endpoint *tcp,
                      grpc_tcp_server_acceptor *acceptor) {
  grpc_server_secure_state *state = statep;
  state_ref(state);
  grpc_server_security_connector_do_handshake(
      exec_ctx, state->sc, acceptor, tcp, on_secure_handshake_done, state);
}

/* Server callback: start listening on our ports */
static void start(grpc_exec_ctx *exec_ctx, grpc_server *server, void *statep,
                  grpc_pollset **pollsets, size_t pollset_count) {
  grpc_server_secure_state *state = statep;
  grpc_tcp_server_start(exec_ctx, state->tcp, pollsets, pollset_count,
                        on_accept, state);
}

static void destroy_done(grpc_exec_ctx *exec_ctx, void *statep, bool success) {
  grpc_server_secure_state *state = statep;
  if (state->destroy_callback != NULL) {
    state->destroy_callback->cb(exec_ctx, state->destroy_callback->cb_arg,
                                success);
  }
  grpc_server_security_connector_shutdown(exec_ctx, state->sc);
  state_unref(state);
}

/* Server callback: destroy the tcp listener (so we don't generate further
   callbacks) */
static void destroy(grpc_exec_ctx *exec_ctx, grpc_server *server, void *statep,
                    grpc_closure *callback) {
  grpc_server_secure_state *state = statep;
  grpc_tcp_server *tcp;
  gpr_mu_lock(&state->mu);
  state->is_shutdown = 1;
  state->destroy_callback = callback;
  tcp = state->tcp;
  gpr_mu_unlock(&state->mu);
  grpc_tcp_server_unref(exec_ctx, tcp);
}

int grpc_server_add_secure_http2_port(grpc_server *server, const char *addr,
                                      grpc_server_credentials *creds) {
  grpc_resolved_addresses *resolved = NULL;
  grpc_tcp_server *tcp = NULL;
  grpc_server_secure_state *state = NULL;
  size_t i;
  unsigned count = 0;
  int port_num = -1;
  int port_temp;
  grpc_security_status status = GRPC_SECURITY_ERROR;
  grpc_server_security_connector *sc = NULL;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GRPC_API_TRACE(
      "grpc_server_add_secure_http2_port("
      "server=%p, addr=%s, creds=%p)",
      3, (server, addr, creds));

  /* create security context */
  if (creds == NULL) goto error;
  status = grpc_server_credentials_create_security_connector(creds, &sc);
  if (status != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR,
            "Unable to create secure server with credentials of type %s.",
            creds->type);
    goto error;
  }
  sc->channel_args = grpc_server_get_channel_args(server);

  /* resolve address */
  resolved = grpc_blocking_resolve_address(addr, "https");
  if (!resolved) {
    goto error;
  }
  state = gpr_malloc(sizeof(*state));
  memset(state, 0, sizeof(*state));
  grpc_closure_init(&state->destroy_closure, destroy_done, state);
  tcp = grpc_tcp_server_create(&state->destroy_closure);
  if (!tcp) {
    goto error;
  }

  state->server = server;
  state->tcp = tcp;
  state->sc = sc;
  state->creds = grpc_server_credentials_ref(creds);
  state->is_shutdown = 0;
  gpr_mu_init(&state->mu);
  gpr_ref_init(&state->refcount, 1);

  for (i = 0; i < resolved->naddrs; i++) {
    port_temp = grpc_tcp_server_add_port(
        tcp, (struct sockaddr *)&resolved->addrs[i].addr,
        resolved->addrs[i].len);
    if (port_temp > 0) {
      if (port_num == -1) {
        port_num = port_temp;
      } else {
        GPR_ASSERT(port_num == port_temp);
      }
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
    /* if it's an error, don't we want to goto error; here ? */
  }
  grpc_resolved_addresses_destroy(resolved);

  /* Register with the server only upon success */
  grpc_server_add_listener(&exec_ctx, server, state, start, destroy);

  grpc_exec_ctx_finish(&exec_ctx);
  return port_num;

/* Error path: cleanup and return */
error:
  if (resolved) {
    grpc_resolved_addresses_destroy(resolved);
  }
  if (tcp) {
    grpc_tcp_server_unref(&exec_ctx, tcp);
  } else {
    if (sc) {
      GRPC_SECURITY_CONNECTOR_UNREF(&sc->base, "server");
    }
    if (state) {
      gpr_free(state);
    }
  }
  grpc_exec_ctx_finish(&exec_ctx);
  return 0;
}
