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
  chttp2_connector* c = reinterpret_cast<chttp2_connector*>(con);
  gpr_ref(&c->refs);
}

static void chttp2_connector_unref(grpc_connector* con) {
  chttp2_connector* c = reinterpret_cast<chttp2_connector*>(con);
  if (gpr_unref(&c->refs)) {
    gpr_mu_destroy(&c->mu);
    // If handshaking is not yet in progress, destroy the endpoint.
    // Otherwise, the handshaker will do this for us.
    if (c->endpoint != nullptr) grpc_endpoint_destroy(c->endpoint);
    gpr_free(c);
  }
}

static void chttp2_connector_shutdown(grpc_connector* con, grpc_error* why) {
  chttp2_connector* c = reinterpret_cast<chttp2_connector*>(con);
  gpr_mu_lock(&c->mu);
  c->shutdown = true;
  if (c->handshake_mgr != nullptr) {
    grpc_handshake_manager_shutdown(c->handshake_mgr, GRPC_ERROR_REF(why));
  }
  // If handshaking is not yet in progress, shutdown the endpoint.
  // Otherwise, the handshaker will do this for us.
  if (!c->connecting && c->endpoint != nullptr) {
    grpc_endpoint_shutdown(c->endpoint, GRPC_ERROR_REF(why));
  }
  gpr_mu_unlock(&c->mu);
  GRPC_ERROR_UNREF(why);
}

static void on_handshake_done(void* arg, grpc_error* error) {
  grpc_handshaker_args* args = static_cast<grpc_handshaker_args*>(arg);
  chttp2_connector* c = static_cast<chttp2_connector*>(args->user_data);
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
      grpc_endpoint_shutdown(args->endpoint, GRPC_ERROR_REF(error));
      grpc_endpoint_destroy(args->endpoint);
      grpc_channel_args_destroy(args->args);
      grpc_slice_buffer_destroy_internal(args->read_buffer);
      gpr_free(args->read_buffer);
    } else {
      error = GRPC_ERROR_REF(error);
    }
    memset(c->result, 0, sizeof(*c->result));
  } else {
    grpc_endpoint_delete_from_pollset_set(args->endpoint,
                                          c->args.interested_parties);
    c->result->transport =
        grpc_create_chttp2_transport(args->args, args->endpoint, true);
    grpc_core::RefCountedPtr<grpc_core::channelz::SocketNode> socket_node =
        grpc_chttp2_transport_get_socket_node(c->result->transport);
    c->result->socket_uuid = socket_node == nullptr ? 0 : socket_node->uuid();
    GPR_ASSERT(c->result->transport);
    // TODO(roth): We ideally want to wait until we receive HTTP/2
    // settings from the server before we consider the connection
    // established.  If that doesn't happen before the connection
    // timeout expires, then we should consider the connection attempt a
    // failure and feed that information back into the backoff code.
    // We could pass a notify_on_receive_settings callback to
    // grpc_chttp2_transport_start_reading() to let us know when
    // settings are received, but we would need to figure out how to use
    // that information here.
    //
    // Unfortunately, we don't currently have a way to split apart the two
    // effects of scheduling c->notify: we start sending RPCs immediately
    // (which we want to do) and we consider the connection attempt successful
    // (which we don't want to do until we get the notify_on_receive_settings
    // callback from the transport).  If we could split those things
    // apart, then we could start sending RPCs but then wait for our
    // timeout before deciding if the connection attempt is successful.
    // If the attempt is not successful, then we would tear down the
    // transport and feed the failure back into the backoff code.
    //
    // In addition, even if we did that, we would probably not want to do
    // so until after transparent retries is implemented.  Otherwise, any
    // RPC that we attempt to send on the connection before the timeout
    // would fail instead of being retried on a subsequent attempt.
    grpc_chttp2_transport_start_reading(c->result->transport, args->read_buffer,
                                        nullptr);
    c->result->channel_args = args->args;
  }
  grpc_closure* notify = c->notify;
  c->notify = nullptr;
  GRPC_CLOSURE_SCHED(notify, error);
  grpc_handshake_manager_destroy(c->handshake_mgr);
  c->handshake_mgr = nullptr;
  gpr_mu_unlock(&c->mu);
  chttp2_connector_unref(reinterpret_cast<grpc_connector*>(c));
}

static void start_handshake_locked(chttp2_connector* c) {
  c->handshake_mgr = grpc_handshake_manager_create();
  grpc_handshakers_add(HANDSHAKER_CLIENT, c->args.channel_args,
                       c->args.interested_parties, c->handshake_mgr);
  grpc_endpoint_add_to_pollset_set(c->endpoint, c->args.interested_parties);
  grpc_handshake_manager_do_handshake(
      c->handshake_mgr, c->endpoint, c->args.channel_args, c->args.deadline,
      nullptr /* acceptor */, on_handshake_done, c);
  c->endpoint = nullptr;  // Endpoint handed off to handshake manager.
}

static void connected(void* arg, grpc_error* error) {
  chttp2_connector* c = static_cast<chttp2_connector*>(arg);
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
    GRPC_CLOSURE_SCHED(notify, error);
    if (c->endpoint != nullptr) {
      grpc_endpoint_shutdown(c->endpoint, GRPC_ERROR_REF(error));
    }
    gpr_mu_unlock(&c->mu);
    chttp2_connector_unref(static_cast<grpc_connector*>(arg));
  } else {
    GPR_ASSERT(c->endpoint != nullptr);
    start_handshake_locked(c);
    gpr_mu_unlock(&c->mu);
  }
}

static void chttp2_connector_connect(grpc_connector* con,
                                     const grpc_connect_in_args* args,
                                     grpc_connect_out_args* result,
                                     grpc_closure* notify) {
  chttp2_connector* c = reinterpret_cast<chttp2_connector*>(con);
  grpc_resolved_address addr;
  grpc_get_subchannel_address_arg(args->channel_args, &addr);
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
  grpc_closure* closure = &c->connected;
  grpc_endpoint** ep = &c->endpoint;
  gpr_mu_unlock(&c->mu);
  // In some implementations, the closure can be flushed before
  // grpc_tcp_client_connect and since the closure requires access to c->mu,
  // this can result in a deadlock. Refer
  // https://github.com/grpc/grpc/issues/16427
  // grpc_tcp_client_connect would fill c->endpoint with proper contents and we
  // make sure that we would still exist at that point by taking a ref.
  grpc_tcp_client_connect(closure, ep, args->interested_parties,
                          args->channel_args, &addr, args->deadline);
}

static const grpc_connector_vtable chttp2_connector_vtable = {
    chttp2_connector_ref, chttp2_connector_unref, chttp2_connector_shutdown,
    chttp2_connector_connect};

grpc_connector* grpc_chttp2_connector_create() {
  chttp2_connector* c = static_cast<chttp2_connector*>(gpr_zalloc(sizeof(*c)));
  c->base.vtable = &chttp2_connector_vtable;
  gpr_mu_init(&c->mu);
  gpr_ref_init(&c->refs, 1);
  return &c->base;
}
