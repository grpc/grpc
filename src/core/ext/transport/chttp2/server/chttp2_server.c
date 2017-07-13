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

#include "src/core/ext/transport/chttp2/server/chttp2_server.h"

#include <grpc/grpc.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

typedef struct {
  grpc_server *server;
  grpc_tcp_server *tcp_server;
  grpc_channel_args *args;
  gpr_mu mu;
  bool shutdown;
  grpc_closure tcp_server_shutdown_complete;
  grpc_closure *server_destroy_listener_done;
  grpc_handshake_manager *pending_handshake_mgrs;
} server_state;

typedef struct {
  server_state *server_state;
  grpc_pollset *accepting_pollset;
  grpc_tcp_server_acceptor *acceptor;
  grpc_handshake_manager *handshake_mgr;
} server_connection_state;

static void on_handshake_done(grpc_exec_ctx *exec_ctx, void *arg,
                              grpc_error *error) {
  grpc_handshaker_args *args = arg;
  server_connection_state *connection_state = args->user_data;
  gpr_mu_lock(&connection_state->server_state->mu);
  if (error != GRPC_ERROR_NONE || connection_state->server_state->shutdown) {
    const char *error_str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "Handshaking failed: %s", error_str);

    if (error == GRPC_ERROR_NONE && args->endpoint != NULL) {
      // We were shut down after handshaking completed successfully, so
      // destroy the endpoint here.
      // TODO(ctiller): It is currently necessary to shutdown endpoints
      // before destroying them, even if we know that there are no
      // pending read/write callbacks.  This should be fixed, at which
      // point this can be removed.
      grpc_endpoint_shutdown(exec_ctx, args->endpoint, GRPC_ERROR_NONE);
      grpc_endpoint_destroy(exec_ctx, args->endpoint);
      grpc_channel_args_destroy(exec_ctx, args->args);
      grpc_slice_buffer_destroy_internal(exec_ctx, args->read_buffer);
      gpr_free(args->read_buffer);
    }
  } else {
    // If the handshaking succeeded but there is no endpoint, then the
    // handshaker may have handed off the connection to some external
    // code, so we can just clean up here without creating a transport.
    if (args->endpoint != NULL) {
      grpc_transport *transport =
          grpc_create_chttp2_transport(exec_ctx, args->args, args->endpoint, 0);
      grpc_server_setup_transport(
          exec_ctx, connection_state->server_state->server, transport,
          connection_state->accepting_pollset, args->args);
      grpc_chttp2_transport_start_reading(exec_ctx, transport,
                                          args->read_buffer);
      grpc_channel_args_destroy(exec_ctx, args->args);
    }
  }
  grpc_handshake_manager_pending_list_remove(
      &connection_state->server_state->pending_handshake_mgrs,
      connection_state->handshake_mgr);
  gpr_mu_unlock(&connection_state->server_state->mu);
  grpc_handshake_manager_destroy(exec_ctx, connection_state->handshake_mgr);
  grpc_tcp_server_unref(exec_ctx, connection_state->server_state->tcp_server);
  gpr_free(connection_state->acceptor);
  gpr_free(connection_state);
}

static void on_accept(grpc_exec_ctx *exec_ctx, void *arg, grpc_endpoint *tcp,
                      grpc_pollset *accepting_pollset,
                      grpc_tcp_server_acceptor *acceptor) {
  server_state *state = arg;
  gpr_mu_lock(&state->mu);
  if (state->shutdown) {
    gpr_mu_unlock(&state->mu);
    grpc_endpoint_shutdown(exec_ctx, tcp, GRPC_ERROR_NONE);
    grpc_endpoint_destroy(exec_ctx, tcp);
    gpr_free(acceptor);
    return;
  }
  grpc_handshake_manager *handshake_mgr = grpc_handshake_manager_create();
  grpc_handshake_manager_pending_list_add(&state->pending_handshake_mgrs,
                                          handshake_mgr);
  gpr_mu_unlock(&state->mu);
  grpc_tcp_server_ref(state->tcp_server);
  server_connection_state *connection_state =
      gpr_malloc(sizeof(*connection_state));
  connection_state->server_state = state;
  connection_state->accepting_pollset = accepting_pollset;
  connection_state->acceptor = acceptor;
  connection_state->handshake_mgr = handshake_mgr;
  grpc_handshakers_add(exec_ctx, HANDSHAKER_SERVER, state->args,
                       connection_state->handshake_mgr);
  // TODO(roth): We should really get this timeout value from channel
  // args instead of hard-coding it.
  const gpr_timespec deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_seconds(120, GPR_TIMESPAN));
  grpc_handshake_manager_do_handshake(exec_ctx, connection_state->handshake_mgr,
                                      tcp, state->args, deadline, acceptor,
                                      on_handshake_done, connection_state);
}

/* Server callback: start listening on our ports */
static void server_start_listener(grpc_exec_ctx *exec_ctx, grpc_server *server,
                                  void *arg, grpc_pollset **pollsets,
                                  size_t pollset_count) {
  server_state *state = arg;
  gpr_mu_lock(&state->mu);
  state->shutdown = false;
  gpr_mu_unlock(&state->mu);
  grpc_tcp_server_start(exec_ctx, state->tcp_server, pollsets, pollset_count,
                        on_accept, state);
}

static void tcp_server_shutdown_complete(grpc_exec_ctx *exec_ctx, void *arg,
                                         grpc_error *error) {
  server_state *state = arg;
  /* ensure all threads have unlocked */
  gpr_mu_lock(&state->mu);
  grpc_closure *destroy_done = state->server_destroy_listener_done;
  GPR_ASSERT(state->shutdown);
  grpc_handshake_manager_pending_list_shutdown_all(
      exec_ctx, state->pending_handshake_mgrs, GRPC_ERROR_REF(error));
  gpr_mu_unlock(&state->mu);
  // Flush queued work before destroying handshaker factory, since that
  // may do a synchronous unref.
  grpc_exec_ctx_flush(exec_ctx);
  if (destroy_done != NULL) {
    destroy_done->cb(exec_ctx, destroy_done->cb_arg, GRPC_ERROR_REF(error));
    grpc_exec_ctx_flush(exec_ctx);
  }
  grpc_channel_args_destroy(exec_ctx, state->args);
  gpr_mu_destroy(&state->mu);
  gpr_free(state);
}

/* Server callback: destroy the tcp listener (so we don't generate further
   callbacks) */
static void server_destroy_listener(grpc_exec_ctx *exec_ctx,
                                    grpc_server *server, void *arg,
                                    grpc_closure *destroy_done) {
  server_state *state = arg;
  gpr_mu_lock(&state->mu);
  state->shutdown = true;
  state->server_destroy_listener_done = destroy_done;
  grpc_tcp_server *tcp_server = state->tcp_server;
  gpr_mu_unlock(&state->mu);
  grpc_tcp_server_shutdown_listeners(exec_ctx, tcp_server);
  grpc_tcp_server_unref(exec_ctx, tcp_server);
}

grpc_error *grpc_chttp2_server_add_port(grpc_exec_ctx *exec_ctx,
                                        grpc_server *server, const char *addr,
                                        grpc_channel_args *args,
                                        int *port_num) {
  grpc_resolved_addresses *resolved = NULL;
  grpc_tcp_server *tcp_server = NULL;
  size_t i;
  size_t count = 0;
  int port_temp;
  grpc_error *err = GRPC_ERROR_NONE;
  server_state *state = NULL;
  grpc_error **errors = NULL;

  *port_num = -1;

  /* resolve address */
  err = grpc_blocking_resolve_address(addr, "https", &resolved);
  if (err != GRPC_ERROR_NONE) {
    goto error;
  }
  state = gpr_zalloc(sizeof(*state));
  GRPC_CLOSURE_INIT(&state->tcp_server_shutdown_complete,
                    tcp_server_shutdown_complete, state,
                    grpc_schedule_on_exec_ctx);
  err = grpc_tcp_server_create(exec_ctx, &state->tcp_server_shutdown_complete,
                               args, &tcp_server);
  if (err != GRPC_ERROR_NONE) {
    goto error;
  }

  state->server = server;
  state->tcp_server = tcp_server;
  state->args = args;
  state->shutdown = true;
  gpr_mu_init(&state->mu);

  const size_t naddrs = resolved->naddrs;
  errors = gpr_malloc(sizeof(*errors) * naddrs);
  for (i = 0; i < naddrs; i++) {
    errors[i] =
        grpc_tcp_server_add_port(tcp_server, &resolved->addrs[i], &port_temp);
    if (errors[i] == GRPC_ERROR_NONE) {
      if (*port_num == -1) {
        *port_num = port_temp;
      } else {
        GPR_ASSERT(*port_num == port_temp);
      }
      count++;
    }
  }
  if (count == 0) {
    char *msg;
    gpr_asprintf(&msg, "No address added out of total %" PRIuPTR " resolved",
                 naddrs);
    err = GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(msg, errors, naddrs);
    gpr_free(msg);
    goto error;
  } else if (count != naddrs) {
    char *msg;
    gpr_asprintf(&msg, "Only %" PRIuPTR
                       " addresses added out of total %" PRIuPTR " resolved",
                 count, naddrs);
    err = GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(msg, errors, naddrs);
    gpr_free(msg);

    const char *warning_message = grpc_error_string(err);
    gpr_log(GPR_INFO, "WARNING: %s", warning_message);

    /* we managed to bind some addresses: continue */
  }
  grpc_resolved_addresses_destroy(resolved);

  /* Register with the server only upon success */
  grpc_server_add_listener(exec_ctx, server, state, server_start_listener,
                           server_destroy_listener);
  goto done;

/* Error path: cleanup and return */
error:
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  if (resolved) {
    grpc_resolved_addresses_destroy(resolved);
  }
  if (tcp_server) {
    grpc_tcp_server_unref(exec_ctx, tcp_server);
  } else {
    grpc_channel_args_destroy(exec_ctx, args);
    gpr_free(state);
  }
  *port_num = 0;

done:
  if (errors != NULL) {
    for (i = 0; i < naddrs; i++) {
      GRPC_ERROR_UNREF(errors[i]);
    }
    gpr_free(errors);
  }
  return err;
}
