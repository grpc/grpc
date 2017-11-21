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

#include "src/core/ext/transport/chttp2/client/chttp2_connector.h"

#include <grpc/grpc.h>

#include <string.h>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/slice/slice_internal.h"

typedef struct {
  grpc_connector base;

  gpr_mu mu;
  gpr_refcount refs;

  bool shutdown;
  bool connecting;

  grpc_closure* notify;
  grpc_connect_in_args args;
  grpc_connect_out_args* result;

  grpc_endpoint* endpoint;  // Non-NULL until handshaking starts.

  grpc_closure connected;

  grpc_handshake_manager* handshake_mgr;
} chttp2_connector;

static void chttp2_connector_ref(grpc_connector* con) {
  chttp2_connector* c = (chttp2_connector*)con;
  gpr_ref(&c->refs);
}

static void chttp2_connector_unref(grpc_exec_ctx* exec_ctx,
                                   grpc_connector* con) {
  chttp2_connector* c = (chttp2_connector*)con;
  if (gpr_unref(&c->refs)) {
    gpr_mu_destroy(&c->mu);
    // If handshaking is not yet in progress, destroy the endpoint.
    // Otherwise, the handshaker will do this for us.
    if (c->endpoint != nullptr) grpc_endpoint_destroy(exec_ctx, c->endpoint);
    gpr_free(c);
  }
}

static void chttp2_connector_shutdown(grpc_exec_ctx* exec_ctx,
                                      grpc_connector* con, grpc_error* why) {
  chttp2_connector* c = (chttp2_connector*)con;
  gpr_mu_lock(&c->mu);
  c->shutdown = true;
  if (c->handshake_mgr != nullptr) {
    grpc_handshake_manager_shutdown(exec_ctx, c->handshake_mgr,
                                    GRPC_ERROR_REF(why));
  }
  // If handshaking is not yet in progress, shutdown the endpoint.
  // Otherwise, the handshaker will do this for us.
  if (!c->connecting && c->endpoint != nullptr) {
    grpc_endpoint_shutdown(exec_ctx, c->endpoint, GRPC_ERROR_REF(why));
  }
  gpr_mu_unlock(&c->mu);
  GRPC_ERROR_UNREF(why);
}

static void on_handshake_done(grpc_exec_ctx* exec_ctx, void* arg,
                              grpc_error* error) {
  grpc_handshaker_args* args = (grpc_handshaker_args*)arg;
  chttp2_connector* c = (chttp2_connector*)args->user_data;
  gpr_mu_lock(&c->mu);
  if (error != GRPC_ERROR_NONE || c->shutdown) {
    if (error == GRPC_ERROR_NONE) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("connector shutdown");
      // We were shut down after handshaking completed successfully, so
      // destroy the endpoint here.
      // TODO(ctiller): It is currently necessary to shutdown endpoints
      // before destroying them, even if we know that there are no
      // pending read/write callbacks.  This should be fixed, at which
      // point this can be removed.
      grpc_endpoint_shutdown(exec_ctx, args->endpoint, GRPC_ERROR_REF(error));
      grpc_endpoint_destroy(exec_ctx, args->endpoint);
      grpc_channel_args_destroy(exec_ctx, args->args);
      grpc_slice_buffer_destroy_internal(exec_ctx, args->read_buffer);
      gpr_free(args->read_buffer);
    } else {
      error = GRPC_ERROR_REF(error);
    }
    memset(c->result, 0, sizeof(*c->result));
  } else {
    grpc_endpoint_delete_from_pollset_set(exec_ctx, args->endpoint,
                                          c->args.interested_parties);
    c->result->transport =
        grpc_create_chttp2_transport(exec_ctx, args->args, args->endpoint, 1);
    GPR_ASSERT(c->result->transport);
    grpc_chttp2_transport_start_reading(exec_ctx, c->result->transport,
                                        args->read_buffer);
    c->result->channel_args = args->args;
  }
  grpc_closure* notify = c->notify;
  c->notify = nullptr;
  GRPC_CLOSURE_SCHED(exec_ctx, notify, error);
  grpc_handshake_manager_destroy(exec_ctx, c->handshake_mgr);
  c->handshake_mgr = nullptr;
  gpr_mu_unlock(&c->mu);
  chttp2_connector_unref(exec_ctx, (grpc_connector*)c);
}

static void start_handshake_locked(grpc_exec_ctx* exec_ctx,
                                   chttp2_connector* c) {
  c->handshake_mgr = grpc_handshake_manager_create();
  grpc_handshakers_add(exec_ctx, HANDSHAKER_CLIENT, c->args.channel_args,
                       c->handshake_mgr);
  grpc_endpoint_add_to_pollset_set(exec_ctx, c->endpoint,
                                   c->args.interested_parties);
  grpc_handshake_manager_do_handshake(
      exec_ctx, c->handshake_mgr, c->endpoint, c->args.channel_args,
      c->args.deadline, nullptr /* acceptor */, on_handshake_done, c);
  c->endpoint = nullptr;  // Endpoint handed off to handshake manager.
}

static void connected(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {
  chttp2_connector* c = (chttp2_connector*)arg;
  gpr_mu_lock(&c->mu);
  GPR_ASSERT(c->connecting);
  c->connecting = false;
  if (error != GRPC_ERROR_NONE || c->shutdown) {
    if (error == GRPC_ERROR_NONE) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("connector shutdown");
    } else {
      error = GRPC_ERROR_REF(error);
    }
    memset(c->result, 0, sizeof(*c->result));
    grpc_closure* notify = c->notify;
    c->notify = nullptr;
    GRPC_CLOSURE_SCHED(exec_ctx, notify, error);
    if (c->endpoint != nullptr) {
      grpc_endpoint_shutdown(exec_ctx, c->endpoint, GRPC_ERROR_REF(error));
    }
    gpr_mu_unlock(&c->mu);
    chttp2_connector_unref(exec_ctx, (grpc_connector*)arg);
  } else {
    GPR_ASSERT(c->endpoint != nullptr);
    start_handshake_locked(exec_ctx, c);
    gpr_mu_unlock(&c->mu);
  }
}

static void chttp2_connector_connect(grpc_exec_ctx* exec_ctx,
                                     grpc_connector* con,
                                     const grpc_connect_in_args* args,
                                     grpc_connect_out_args* result,
                                     grpc_closure* notify) {
  chttp2_connector* c = (chttp2_connector*)con;
  grpc_resolved_address addr;
  grpc_get_subchannel_address_arg(exec_ctx, args->channel_args, &addr);
  gpr_mu_lock(&c->mu);
  GPR_ASSERT(c->notify == nullptr);
  c->notify = notify;
  c->args = *args;
  c->result = result;
  GPR_ASSERT(c->endpoint == nullptr);
  chttp2_connector_ref(con);  // Ref taken for callback.
  GRPC_CLOSURE_INIT(&c->connected, connected, c, grpc_schedule_on_exec_ctx);
  GPR_ASSERT(!c->connecting);
  c->connecting = true;
  grpc_tcp_client_connect(exec_ctx, &c->connected, &c->endpoint,
                          args->interested_parties, args->channel_args, &addr,
                          args->deadline);
  gpr_mu_unlock(&c->mu);
}

static const grpc_connector_vtable chttp2_connector_vtable = {
    chttp2_connector_ref, chttp2_connector_unref, chttp2_connector_shutdown,
    chttp2_connector_connect};

grpc_connector* grpc_chttp2_connector_create() {
  chttp2_connector* c = (chttp2_connector*)gpr_zalloc(sizeof(*c));
  c->base.vtable = &chttp2_connector_vtable;
  gpr_mu_init(&c->mu);
  gpr_ref_init(&c->refs, 1);
  return &c->base;
}
