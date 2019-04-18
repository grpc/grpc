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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/server/chttp2_server.h"

#include <grpc/grpc.h>

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

typedef struct {
  grpc_server* server;
  grpc_tcp_server* tcp_server;
  grpc_channel_args* args;
  gpr_mu mu;
  bool shutdown;
  grpc_closure tcp_server_shutdown_complete;
  grpc_closure* server_destroy_listener_done;
  grpc_core::HandshakeManager* pending_handshake_mgrs;
  grpc_core::RefCountedPtr<grpc_core::channelz::ListenSocketNode>
      channelz_listen_socket;
} server_state;

typedef struct {
  gpr_refcount refs;
  server_state* svr_state;
  grpc_pollset* accepting_pollset;
  grpc_tcp_server_acceptor* acceptor;
  grpc_core::RefCountedPtr<grpc_core::HandshakeManager> handshake_mgr;
  // State for enforcing handshake timeout on receiving HTTP/2 settings.
  grpc_chttp2_transport* transport;
  grpc_millis deadline;
  grpc_timer timer;
  grpc_closure on_timeout;
  grpc_closure on_receive_settings;
  grpc_pollset_set* interested_parties;
} server_connection_state;

static void server_connection_state_unref(
    server_connection_state* connection_state) {
  if (gpr_unref(&connection_state->refs)) {
    if (connection_state->transport != nullptr) {
      GRPC_CHTTP2_UNREF_TRANSPORT(connection_state->transport,
                                  "receive settings timeout");
    }
    grpc_pollset_set_del_pollset(connection_state->interested_parties,
                                 connection_state->accepting_pollset);
    grpc_pollset_set_destroy(connection_state->interested_parties);
    gpr_free(connection_state);
  }
}

static void on_timeout(void* arg, grpc_error* error) {
  server_connection_state* connection_state =
      static_cast<server_connection_state*>(arg);
  // Note that we may be called with GRPC_ERROR_NONE when the timer fires
  // or with an error indicating that the timer system is being shut down.
  if (error != GRPC_ERROR_CANCELLED) {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->disconnect_with_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Did not receive HTTP/2 settings before handshake timeout");
    grpc_transport_perform_op(&connection_state->transport->base, op);
  }
  server_connection_state_unref(connection_state);
}

static void on_receive_settings(void* arg, grpc_error* error) {
  server_connection_state* connection_state =
      static_cast<server_connection_state*>(arg);
  if (error == GRPC_ERROR_NONE) {
    grpc_timer_cancel(&connection_state->timer);
  }
  server_connection_state_unref(connection_state);
}

static void on_handshake_done(void* arg, grpc_error* error) {
  auto* args = static_cast<grpc_core::HandshakerArgs*>(arg);
  server_connection_state* connection_state =
      static_cast<server_connection_state*>(args->user_data);
  gpr_mu_lock(&connection_state->svr_state->mu);
  grpc_resource_user* resource_user = grpc_server_get_default_resource_user(
      connection_state->svr_state->server);
  if (error != GRPC_ERROR_NONE || connection_state->svr_state->shutdown) {
    const char* error_str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "Handshaking failed: %s", error_str);
    grpc_resource_user* resource_user = grpc_server_get_default_resource_user(
        connection_state->svr_state->server);
    if (resource_user != nullptr) {
      grpc_resource_user_free(resource_user, GRPC_RESOURCE_QUOTA_CHANNEL_SIZE);
    }
    if (error == GRPC_ERROR_NONE && args->endpoint != nullptr) {
      // We were shut down after handshaking completed successfully, so
      // destroy the endpoint here.
      // TODO(ctiller): It is currently necessary to shutdown endpoints
      // before destroying them, even if we know that there are no
      // pending read/write callbacks.  This should be fixed, at which
      // point this can be removed.
      grpc_endpoint_shutdown(args->endpoint, GRPC_ERROR_NONE);
      grpc_endpoint_destroy(args->endpoint);
      grpc_channel_args_destroy(args->args);
      grpc_slice_buffer_destroy_internal(args->read_buffer);
      gpr_free(args->read_buffer);
    }
  } else {
    // If the handshaking succeeded but there is no endpoint, then the
    // handshaker may have handed off the connection to some external
    // code, so we can just clean up here without creating a transport.
    if (args->endpoint != nullptr) {
      grpc_transport* transport = grpc_create_chttp2_transport(
          args->args, args->endpoint, false, resource_user);
      grpc_server_setup_transport(
          connection_state->svr_state->server, transport,
          connection_state->accepting_pollset, args->args,
          grpc_chttp2_transport_get_socket_node(transport), resource_user);
      // Use notify_on_receive_settings callback to enforce the
      // handshake deadline.
      connection_state->transport =
          reinterpret_cast<grpc_chttp2_transport*>(transport);
      gpr_ref(&connection_state->refs);
      GRPC_CLOSURE_INIT(&connection_state->on_receive_settings,
                        on_receive_settings, connection_state,
                        grpc_schedule_on_exec_ctx);
      grpc_chttp2_transport_start_reading(
          transport, args->read_buffer, &connection_state->on_receive_settings);
      grpc_channel_args_destroy(args->args);
      gpr_ref(&connection_state->refs);
      GRPC_CHTTP2_REF_TRANSPORT((grpc_chttp2_transport*)transport,
                                "receive settings timeout");
      GRPC_CLOSURE_INIT(&connection_state->on_timeout, on_timeout,
                        connection_state, grpc_schedule_on_exec_ctx);
      grpc_timer_init(&connection_state->timer, connection_state->deadline,
                      &connection_state->on_timeout);
    } else {
      if (resource_user != nullptr) {
        grpc_resource_user_free(resource_user,
                                GRPC_RESOURCE_QUOTA_CHANNEL_SIZE);
      }
    }
  }
  connection_state->handshake_mgr->RemoveFromPendingMgrList(
      &connection_state->svr_state->pending_handshake_mgrs);
  gpr_mu_unlock(&connection_state->svr_state->mu);
  connection_state->handshake_mgr.reset();
  gpr_free(connection_state->acceptor);
  grpc_tcp_server_unref(connection_state->svr_state->tcp_server);
  server_connection_state_unref(connection_state);
}

static void on_accept(void* arg, grpc_endpoint* tcp,
                      grpc_pollset* accepting_pollset,
                      grpc_tcp_server_acceptor* acceptor) {
  server_state* state = static_cast<server_state*>(arg);
  gpr_mu_lock(&state->mu);
  if (state->shutdown) {
    gpr_mu_unlock(&state->mu);
    grpc_endpoint_shutdown(tcp, GRPC_ERROR_NONE);
    grpc_endpoint_destroy(tcp);
    gpr_free(acceptor);
    return;
  }
  grpc_resource_user* resource_user =
      grpc_server_get_default_resource_user(state->server);
  if (resource_user != nullptr &&
      !grpc_resource_user_safe_alloc(resource_user,
                                     GRPC_RESOURCE_QUOTA_CHANNEL_SIZE)) {
    gpr_log(
        GPR_ERROR,
        "Memory quota exhausted, rejecting the connection, no handshaking.");
    gpr_mu_unlock(&state->mu);
    grpc_endpoint_shutdown(tcp, GRPC_ERROR_NONE);
    grpc_endpoint_destroy(tcp);
    gpr_free(acceptor);
    return;
  }
  auto handshake_mgr = grpc_core::MakeRefCounted<grpc_core::HandshakeManager>();
  handshake_mgr->AddToPendingMgrList(&state->pending_handshake_mgrs);
  grpc_tcp_server_ref(state->tcp_server);
  gpr_mu_unlock(&state->mu);
  server_connection_state* connection_state =
      static_cast<server_connection_state*>(
          gpr_zalloc(sizeof(*connection_state)));
  gpr_ref_init(&connection_state->refs, 1);
  connection_state->svr_state = state;
  connection_state->accepting_pollset = accepting_pollset;
  connection_state->acceptor = acceptor;
  connection_state->handshake_mgr = handshake_mgr;
  connection_state->interested_parties = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(connection_state->interested_parties,
                               connection_state->accepting_pollset);
  grpc_core::HandshakerRegistry::AddHandshakers(
      grpc_core::HANDSHAKER_SERVER, state->args,
      connection_state->interested_parties,
      connection_state->handshake_mgr.get());
  const grpc_arg* timeout_arg =
      grpc_channel_args_find(state->args, GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS);
  connection_state->deadline =
      grpc_core::ExecCtx::Get()->Now() +
      grpc_channel_arg_get_integer(timeout_arg,
                                   {120 * GPR_MS_PER_SEC, 1, INT_MAX});
  connection_state->handshake_mgr->DoHandshake(
      tcp, state->args, connection_state->deadline, acceptor, on_handshake_done,
      connection_state);
}

/* Server callback: start listening on our ports */
static void server_start_listener(grpc_server* server, void* arg,
                                  grpc_pollset** pollsets,
                                  size_t pollset_count) {
  server_state* state = static_cast<server_state*>(arg);
  gpr_mu_lock(&state->mu);
  state->shutdown = false;
  gpr_mu_unlock(&state->mu);
  grpc_tcp_server_start(state->tcp_server, pollsets, pollset_count, on_accept,
                        state);
}

static void tcp_server_shutdown_complete(void* arg, grpc_error* error) {
  server_state* state = static_cast<server_state*>(arg);
  /* ensure all threads have unlocked */
  gpr_mu_lock(&state->mu);
  grpc_closure* destroy_done = state->server_destroy_listener_done;
  GPR_ASSERT(state->shutdown);
  if (state->pending_handshake_mgrs != nullptr) {
    state->pending_handshake_mgrs->ShutdownAllPending(GRPC_ERROR_REF(error));
  }
  state->channelz_listen_socket.reset();
  gpr_mu_unlock(&state->mu);
  // Flush queued work before destroying handshaker factory, since that
  // may do a synchronous unref.
  grpc_core::ExecCtx::Get()->Flush();
  if (destroy_done != nullptr) {
    destroy_done->cb(destroy_done->cb_arg, GRPC_ERROR_REF(error));
    grpc_core::ExecCtx::Get()->Flush();
  }
  grpc_channel_args_destroy(state->args);
  gpr_mu_destroy(&state->mu);
  gpr_free(state);
}

/* Server callback: destroy the tcp listener (so we don't generate further
   callbacks) */
static void server_destroy_listener(grpc_server* server, void* arg,
                                    grpc_closure* destroy_done) {
  server_state* state = static_cast<server_state*>(arg);
  gpr_mu_lock(&state->mu);
  state->shutdown = true;
  state->server_destroy_listener_done = destroy_done;
  grpc_tcp_server* tcp_server = state->tcp_server;
  gpr_mu_unlock(&state->mu);
  grpc_tcp_server_shutdown_listeners(tcp_server);
  grpc_tcp_server_unref(tcp_server);
}

grpc_error* grpc_chttp2_server_add_port(grpc_server* server, const char* addr,
                                        grpc_channel_args* args,
                                        int* port_num) {
  grpc_resolved_addresses* resolved = nullptr;
  grpc_tcp_server* tcp_server = nullptr;
  size_t i;
  size_t count = 0;
  int port_temp;
  grpc_error* err = GRPC_ERROR_NONE;
  server_state* state = nullptr;
  grpc_error** errors = nullptr;
  size_t naddrs = 0;
  const grpc_arg* arg = nullptr;
  intptr_t socket_uuid = 0;

  *port_num = -1;

  /* resolve address */
  err = grpc_blocking_resolve_address(addr, "https", &resolved);
  if (err != GRPC_ERROR_NONE) {
    goto error;
  }
  state = static_cast<server_state*>(gpr_zalloc(sizeof(*state)));
  GRPC_CLOSURE_INIT(&state->tcp_server_shutdown_complete,
                    tcp_server_shutdown_complete, state,
                    grpc_schedule_on_exec_ctx);
  err = grpc_tcp_server_create(&state->tcp_server_shutdown_complete, args,
                               &tcp_server);
  if (err != GRPC_ERROR_NONE) {
    goto error;
  }

  state->server = server;
  state->tcp_server = tcp_server;
  state->args = args;
  state->shutdown = true;
  gpr_mu_init(&state->mu);

  naddrs = resolved->naddrs;
  errors = static_cast<grpc_error**>(gpr_malloc(sizeof(*errors) * naddrs));
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
    char* msg;
    gpr_asprintf(&msg, "No address added out of total %" PRIuPTR " resolved",
                 naddrs);
    err = GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(msg, errors, naddrs);
    gpr_free(msg);
    goto error;
  } else if (count != naddrs) {
    char* msg;
    gpr_asprintf(&msg,
                 "Only %" PRIuPTR " addresses added out of total %" PRIuPTR
                 " resolved",
                 count, naddrs);
    err = GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(msg, errors, naddrs);
    gpr_free(msg);

    const char* warning_message = grpc_error_string(err);
    gpr_log(GPR_INFO, "WARNING: %s", warning_message);

    /* we managed to bind some addresses: continue */
  }
  grpc_resolved_addresses_destroy(resolved);

  arg = grpc_channel_args_find(args, GRPC_ARG_ENABLE_CHANNELZ);
  if (grpc_channel_arg_get_bool(arg, GRPC_ENABLE_CHANNELZ_DEFAULT)) {
    state->channelz_listen_socket =
        grpc_core::MakeRefCounted<grpc_core::channelz::ListenSocketNode>(
            grpc_core::UniquePtr<char>(gpr_strdup(addr)));
    socket_uuid = state->channelz_listen_socket->uuid();
  }

  /* Register with the server only upon success */
  grpc_server_add_listener(server, state, server_start_listener,
                           server_destroy_listener, socket_uuid);
  goto done;

/* Error path: cleanup and return */
error:
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  if (resolved) {
    grpc_resolved_addresses_destroy(resolved);
  }
  if (tcp_server) {
    grpc_tcp_server_unref(tcp_server);
  } else {
    grpc_channel_args_destroy(args);
    gpr_free(state);
  }
  *port_num = 0;

done:
  if (errors != nullptr) {
    for (i = 0; i < naddrs; i++) {
      GRPC_ERROR_UNREF(errors[i]);
    }
    gpr_free(errors);
  }
  return err;
}
