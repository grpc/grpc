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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/channel/http_server_filter.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/security/transport/security_connector.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

typedef struct server_secure_state {
  grpc_server *server;
  grpc_tcp_server *tcp;
  grpc_server_security_connector *sc;
  grpc_server_credentials *creds;
  bool is_shutdown;
  gpr_mu mu;
  grpc_closure tcp_server_shutdown_complete;
  grpc_closure *server_destroy_listener_done;
} server_secure_state;

typedef struct server_secure_connect {
  server_secure_state *server_state;
  grpc_pollset *accepting_pollset;
  grpc_tcp_server_acceptor *acceptor;
  grpc_handshake_manager *handshake_mgr;
  // TODO(roth): Remove the following two fields when we eliminate
  // grpc_server_security_connector_do_handshake().
  gpr_timespec deadline;
  grpc_channel_args *args;
} server_secure_connect;

static void on_secure_handshake_done(grpc_exec_ctx *exec_ctx, void *statep,
                                     grpc_security_status status,
                                     grpc_endpoint *secure_endpoint,
                                     grpc_auth_context *auth_context) {
  server_secure_connect *connection_state = statep;
  if (status == GRPC_SECURITY_OK) {
    if (secure_endpoint) {
      gpr_mu_lock(&connection_state->server_state->mu);
      if (!connection_state->server_state->is_shutdown) {
        grpc_transport *transport = grpc_create_chttp2_transport(
            exec_ctx, grpc_server_get_channel_args(
                          connection_state->server_state->server),
            secure_endpoint, 0);
        grpc_arg args_to_add[2];
        args_to_add[0] = grpc_server_credentials_to_arg(
            connection_state->server_state->creds);
        args_to_add[1] = grpc_auth_context_to_arg(auth_context);
        grpc_channel_args *args_copy = grpc_channel_args_copy_and_add(
            connection_state->args, args_to_add, GPR_ARRAY_SIZE(args_to_add));
        grpc_server_setup_transport(
            exec_ctx, connection_state->server_state->server, transport,
            connection_state->accepting_pollset, args_copy);
        grpc_channel_args_destroy(args_copy);
        grpc_chttp2_transport_start_reading(exec_ctx, transport, NULL);
      } else {
        /* We need to consume this here, because the server may already have
         * gone away. */
        grpc_endpoint_destroy(exec_ctx, secure_endpoint);
      }
      gpr_mu_unlock(&connection_state->server_state->mu);
    }
  } else {
    gpr_log(GPR_ERROR, "Secure transport failed with error %d", status);
  }
  grpc_channel_args_destroy(connection_state->args);
  grpc_tcp_server_unref(exec_ctx, connection_state->server_state->tcp);
  gpr_free(connection_state);
}

static void on_handshake_done(grpc_exec_ctx *exec_ctx, grpc_endpoint *endpoint,
                              grpc_channel_args *args,
                              gpr_slice_buffer *read_buffer, void *user_data,
                              grpc_error *error) {
  server_secure_connect *connection_state = user_data;
  if (error != GRPC_ERROR_NONE) {
    const char *error_str = grpc_error_string(error);
    gpr_log(GPR_ERROR, "Handshaking failed: %s", error_str);
    grpc_error_free_string(error_str);
    GRPC_ERROR_UNREF(error);
    grpc_channel_args_destroy(args);
    gpr_free(read_buffer);
    grpc_handshake_manager_shutdown(exec_ctx, connection_state->handshake_mgr);
    grpc_handshake_manager_destroy(exec_ctx, connection_state->handshake_mgr);
    grpc_tcp_server_unref(exec_ctx, connection_state->server_state->tcp);
    gpr_free(connection_state);
    return;
  }
  grpc_handshake_manager_destroy(exec_ctx, connection_state->handshake_mgr);
  connection_state->handshake_mgr = NULL;
  // TODO(roth, jboeuf): Convert security connector handshaking to use new
  // handshake API, and then move the code from on_secure_handshake_done()
  // into this function.
  connection_state->args = args;
  grpc_server_security_connector_do_handshake(
      exec_ctx, connection_state->server_state->sc, connection_state->acceptor,
      endpoint, read_buffer, connection_state->deadline,
      on_secure_handshake_done, connection_state);
}

static void on_accept(grpc_exec_ctx *exec_ctx, void *statep, grpc_endpoint *tcp,
                      grpc_pollset *accepting_pollset,
                      grpc_tcp_server_acceptor *acceptor) {
  server_secure_state *server_state = statep;
  server_secure_connect *connection_state = NULL;
  gpr_mu_lock(&server_state->mu);
  if (server_state->is_shutdown) {
    gpr_mu_unlock(&server_state->mu);
    grpc_endpoint_destroy(exec_ctx, tcp);
    return;
  }
  gpr_mu_unlock(&server_state->mu);
  grpc_tcp_server_ref(server_state->tcp);
  connection_state = gpr_malloc(sizeof(*connection_state));
  connection_state->server_state = server_state;
  connection_state->accepting_pollset = accepting_pollset;
  connection_state->acceptor = acceptor;
  connection_state->handshake_mgr = grpc_handshake_manager_create();
  // TODO(roth): We should really get this timeout value from channel
  // args instead of hard-coding it.
  connection_state->deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_seconds(120, GPR_TIMESPAN));
  grpc_handshake_manager_do_handshake(
      exec_ctx, connection_state->handshake_mgr, tcp,
      grpc_server_get_channel_args(connection_state->server_state->server),
      connection_state->deadline, acceptor, on_handshake_done,
      connection_state);
}

/* Server callback: start listening on our ports */
static void server_start_listener(grpc_exec_ctx *exec_ctx, grpc_server *server,
                                  void *statep, grpc_pollset **pollsets,
                                  size_t pollset_count) {
  server_secure_state *server_state = statep;
  gpr_mu_lock(&server_state->mu);
  server_state->is_shutdown = false;
  gpr_mu_unlock(&server_state->mu);
  grpc_tcp_server_start(exec_ctx, server_state->tcp, pollsets, pollset_count,
                        on_accept, server_state);
}

static void tcp_server_shutdown_complete(grpc_exec_ctx *exec_ctx, void *statep,
                                         grpc_error *error) {
  server_secure_state *server_state = statep;
  /* ensure all threads have unlocked */
  gpr_mu_lock(&server_state->mu);
  grpc_closure *destroy_done = server_state->server_destroy_listener_done;
  GPR_ASSERT(server_state->is_shutdown);
  gpr_mu_unlock(&server_state->mu);
  /* clean up */
  grpc_server_security_connector_shutdown(exec_ctx, server_state->sc);

  /* Flush queued work before a synchronous unref. */
  grpc_exec_ctx_flush(exec_ctx);
  GRPC_SECURITY_CONNECTOR_UNREF(&server_state->sc->base, "server");
  grpc_server_credentials_unref(server_state->creds);

  if (destroy_done != NULL) {
    destroy_done->cb(exec_ctx, destroy_done->cb_arg, GRPC_ERROR_REF(error));
    grpc_exec_ctx_flush(exec_ctx);
  }
  gpr_free(server_state);
}

static void server_destroy_listener(grpc_exec_ctx *exec_ctx,
                                    grpc_server *server, void *statep,
                                    grpc_closure *callback) {
  server_secure_state *server_state = statep;
  grpc_tcp_server *tcp;
  gpr_mu_lock(&server_state->mu);
  server_state->is_shutdown = true;
  server_state->server_destroy_listener_done = callback;
  tcp = server_state->tcp;
  gpr_mu_unlock(&server_state->mu);
  grpc_tcp_server_shutdown_listeners(exec_ctx, tcp);
  grpc_tcp_server_unref(exec_ctx, server_state->tcp);
}

int grpc_server_add_secure_http2_port(grpc_server *server, const char *addr,
                                      grpc_server_credentials *creds) {
  grpc_resolved_addresses *resolved = NULL;
  grpc_tcp_server *tcp = NULL;
  server_secure_state *server_state = NULL;
  size_t i;
  size_t count = 0;
  int port_num = -1;
  int port_temp;
  grpc_security_status status = GRPC_SECURITY_ERROR;
  grpc_server_security_connector *sc = NULL;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_error *err = GRPC_ERROR_NONE;
  grpc_error **errors = NULL;

  GRPC_API_TRACE(
      "grpc_server_add_secure_http2_port("
      "server=%p, addr=%s, creds=%p)",
      3, (server, addr, creds));

  /* create security context */
  if (creds == NULL) {
    err = GRPC_ERROR_CREATE(
        "No credentials specified for secure server port (creds==NULL)");
    goto error;
  }
  status = grpc_server_credentials_create_security_connector(creds, &sc);
  if (status != GRPC_SECURITY_OK) {
    char *msg;
    gpr_asprintf(&msg,
                 "Unable to create secure server with credentials of type %s.",
                 creds->type);
    err = grpc_error_set_int(GRPC_ERROR_CREATE(msg),
                             GRPC_ERROR_INT_SECURITY_STATUS, status);
    gpr_free(msg);
    goto error;
  }
  sc->channel_args = grpc_server_get_channel_args(server);

  /* resolve address */
  err = grpc_blocking_resolve_address(addr, "https", &resolved);
  if (err != GRPC_ERROR_NONE) {
    goto error;
  }
  server_state = gpr_malloc(sizeof(*server_state));
  memset(server_state, 0, sizeof(*server_state));
  grpc_closure_init(&server_state->tcp_server_shutdown_complete,
                    tcp_server_shutdown_complete, server_state);
  err = grpc_tcp_server_create(&server_state->tcp_server_shutdown_complete,
                               grpc_server_get_channel_args(server), &tcp);
  if (err != GRPC_ERROR_NONE) {
    goto error;
  }

  server_state->server = server;
  server_state->tcp = tcp;
  server_state->sc = sc;
  server_state->creds = grpc_server_credentials_ref(creds);
  server_state->is_shutdown = true;
  gpr_mu_init(&server_state->mu);

  errors = gpr_malloc(sizeof(*errors) * resolved->naddrs);
  for (i = 0; i < resolved->naddrs; i++) {
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
                 resolved->naddrs);
    err = GRPC_ERROR_CREATE_REFERENCING(msg, errors, resolved->naddrs);
    gpr_free(msg);
    goto error;
  } else if (count != resolved->naddrs) {
    char *msg;
    gpr_asprintf(&msg, "Only %" PRIuPTR
                       " addresses added out of total %" PRIuPTR " resolved",
                 count, resolved->naddrs);
    err = GRPC_ERROR_CREATE_REFERENCING(msg, errors, resolved->naddrs);
    gpr_free(msg);

    const char *warning_message = grpc_error_string(err);
    gpr_log(GPR_INFO, "WARNING: %s", warning_message);
    grpc_error_free_string(warning_message);
    /* we managed to bind some addresses: continue */
  } else {
    for (i = 0; i < resolved->naddrs; i++) {
      GRPC_ERROR_UNREF(errors[i]);
    }
  }
  gpr_free(errors);
  errors = NULL;
  grpc_resolved_addresses_destroy(resolved);

  /* Register with the server only upon success */
  grpc_server_add_listener(&exec_ctx, server, server_state,
                           server_start_listener, server_destroy_listener);

  grpc_exec_ctx_finish(&exec_ctx);
  return port_num;

/* Error path: cleanup and return */
error:
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  if (errors != NULL) {
    for (i = 0; i < resolved->naddrs; i++) {
      GRPC_ERROR_UNREF(errors[i]);
    }
    gpr_free(errors);
  }
  if (resolved) {
    grpc_resolved_addresses_destroy(resolved);
  }
  if (tcp) {
    grpc_tcp_server_unref(&exec_ctx, tcp);
  } else {
    if (sc) {
      grpc_exec_ctx_flush(&exec_ctx);
      GRPC_SECURITY_CONNECTOR_UNREF(&sc->base, "server");
    }
    if (server_state) {
      gpr_free(server_state);
    }
  }
  grpc_exec_ctx_finish(&exec_ctx);
  const char *msg = grpc_error_string(err);
  GRPC_ERROR_UNREF(err);
  gpr_log(GPR_ERROR, "%s", msg);
  grpc_error_free_string(msg);
  return 0;
}
