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

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/slice.h>
#include <grpc/support/slice_buffer.h>

#include "src/core/ext/client_config/client_channel.h"
#include "src/core/ext/client_config/resolver_registry.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/tsi/transport_security_interface.h"

typedef struct {
  grpc_connector base;
  gpr_refcount refs;

  grpc_channel_security_connector *security_connector;

  grpc_closure *notify;
  grpc_connect_in_args args;
  grpc_connect_out_args *result;
  grpc_closure initial_string_sent;
  gpr_slice_buffer initial_string_buffer;

  gpr_mu mu;
  grpc_endpoint *connecting_endpoint;
  grpc_endpoint *newly_connecting_endpoint;

  grpc_closure connected_closure;
} connector;

static void connector_ref(grpc_connector *con) {
  connector *c = (connector *)con;
  gpr_ref(&c->refs);
}

static void connector_unref(grpc_exec_ctx *exec_ctx, grpc_connector *con) {
  connector *c = (connector *)con;
  if (gpr_unref(&c->refs)) {
    /* c->initial_string_buffer does not need to be destroyed */
    gpr_free(c);
  }
}

static void on_secure_handshake_done(grpc_exec_ctx *exec_ctx, void *arg,
                                     grpc_security_status status,
                                     grpc_endpoint *secure_endpoint,
                                     grpc_auth_context *auth_context) {
  connector *c = arg;
  grpc_closure *notify;
  grpc_channel_args *args_copy = NULL;
  gpr_mu_lock(&c->mu);
  if (c->connecting_endpoint == NULL) {
    memset(c->result, 0, sizeof(*c->result));
    gpr_mu_unlock(&c->mu);
  } else if (status != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR, "Secure handshake failed with error %d.", status);
    memset(c->result, 0, sizeof(*c->result));
    c->connecting_endpoint = NULL;
    gpr_mu_unlock(&c->mu);
  } else {
    grpc_arg auth_context_arg;
    c->connecting_endpoint = NULL;
    gpr_mu_unlock(&c->mu);
    c->result->transport = grpc_create_chttp2_transport(
        exec_ctx, c->args.channel_args, secure_endpoint, 1);
    grpc_chttp2_transport_start_reading(exec_ctx, c->result->transport, NULL,
                                        0);
    auth_context_arg = grpc_auth_context_to_arg(auth_context);
    args_copy = grpc_channel_args_copy_and_add(c->args.channel_args,
                                               &auth_context_arg, 1);
    c->result->channel_args = args_copy;
  }
  notify = c->notify;
  c->notify = NULL;
  /* look at c->args which are connector args. */
  notify->cb(exec_ctx, notify->cb_arg, 1);
  if (args_copy != NULL) grpc_channel_args_destroy(args_copy);
}

static void on_initial_connect_string_sent(grpc_exec_ctx *exec_ctx, void *arg,
                                           bool success) {
  connector *c = arg;
  grpc_channel_security_connector_do_handshake(exec_ctx, c->security_connector,
                                               c->connecting_endpoint,
                                               on_secure_handshake_done, c);
}

static void connected(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  connector *c = arg;
  grpc_closure *notify;
  grpc_endpoint *tcp = c->newly_connecting_endpoint;
  if (tcp != NULL) {
    gpr_mu_lock(&c->mu);
    GPR_ASSERT(c->connecting_endpoint == NULL);
    c->connecting_endpoint = tcp;
    gpr_mu_unlock(&c->mu);
    if (!GPR_SLICE_IS_EMPTY(c->args.initial_connect_string)) {
      grpc_closure_init(&c->initial_string_sent, on_initial_connect_string_sent,
                        c);
      gpr_slice_buffer_init(&c->initial_string_buffer);
      gpr_slice_buffer_add(&c->initial_string_buffer,
                           c->args.initial_connect_string);
      grpc_endpoint_write(exec_ctx, tcp, &c->initial_string_buffer,
                          &c->initial_string_sent);
    } else {
      grpc_channel_security_connector_do_handshake(
          exec_ctx, c->security_connector, tcp, on_secure_handshake_done, c);
    }
  } else {
    memset(c->result, 0, sizeof(*c->result));
    notify = c->notify;
    c->notify = NULL;
    notify->cb(exec_ctx, notify->cb_arg, 1);
  }
}

static void connector_shutdown(grpc_exec_ctx *exec_ctx, grpc_connector *con) {
  connector *c = (connector *)con;
  grpc_endpoint *ep;
  gpr_mu_lock(&c->mu);
  ep = c->connecting_endpoint;
  c->connecting_endpoint = NULL;
  gpr_mu_unlock(&c->mu);
  if (ep) {
    grpc_endpoint_shutdown(exec_ctx, ep);
  }
}

static void connector_connect(grpc_exec_ctx *exec_ctx, grpc_connector *con,
                              const grpc_connect_in_args *args,
                              grpc_connect_out_args *result,
                              grpc_closure *notify) {
  connector *c = (connector *)con;
  GPR_ASSERT(c->notify == NULL);
  GPR_ASSERT(notify->cb);
  c->notify = notify;
  c->args = *args;
  c->result = result;
  gpr_mu_lock(&c->mu);
  GPR_ASSERT(c->connecting_endpoint == NULL);
  gpr_mu_unlock(&c->mu);
  grpc_closure_init(&c->connected_closure, connected, c);
  grpc_tcp_client_connect(
      exec_ctx, &c->connected_closure, &c->newly_connecting_endpoint,
      args->interested_parties, args->addr, args->addr_len, args->deadline);
}

static const grpc_connector_vtable connector_vtable = {
    connector_ref, connector_unref, connector_shutdown, connector_connect};

typedef struct {
  grpc_client_channel_factory base;
  gpr_refcount refs;
  grpc_channel_args *merge_args;
  grpc_channel_security_connector *security_connector;
  grpc_channel *master;
} client_channel_factory;

static void client_channel_factory_ref(
    grpc_client_channel_factory *cc_factory) {
  client_channel_factory *f = (client_channel_factory *)cc_factory;
  gpr_ref(&f->refs);
}

static void client_channel_factory_unref(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory) {
  client_channel_factory *f = (client_channel_factory *)cc_factory;
  if (gpr_unref(&f->refs)) {
    GRPC_SECURITY_CONNECTOR_UNREF(&f->security_connector->base,
                                  "client_channel_factory");
    if (f->master != NULL) {
      GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, f->master,
                                  "client_channel_factory");
    }
    grpc_channel_args_destroy(f->merge_args);
    gpr_free(f);
  }
}

static grpc_subchannel *client_channel_factory_create_subchannel(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory,
    grpc_subchannel_args *args) {
  client_channel_factory *f = (client_channel_factory *)cc_factory;
  connector *c = gpr_malloc(sizeof(*c));
  grpc_channel_args *final_args =
      grpc_channel_args_merge(args->args, f->merge_args);
  grpc_subchannel *s;
  memset(c, 0, sizeof(*c));
  c->base.vtable = &connector_vtable;
  c->security_connector = f->security_connector;
  gpr_mu_init(&c->mu);
  gpr_ref_init(&c->refs, 1);
  args->args = final_args;
  s = grpc_subchannel_create(exec_ctx, &c->base, args);
  grpc_connector_unref(exec_ctx, &c->base);
  grpc_channel_args_destroy(final_args);
  return s;
}

static grpc_channel *client_channel_factory_create_channel(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory,
    const char *target, grpc_client_channel_type type,
    grpc_channel_args *args) {
  client_channel_factory *f = (client_channel_factory *)cc_factory;

  grpc_channel_args *final_args = grpc_channel_args_merge(args, f->merge_args);
  grpc_channel *channel = grpc_channel_create(exec_ctx, target, final_args,
                                              GRPC_CLIENT_CHANNEL, NULL);
  grpc_channel_args_destroy(final_args);

  grpc_resolver *resolver = grpc_resolver_create(target, &f->base);
  if (resolver != NULL) {
    grpc_client_channel_set_resolver(
        exec_ctx, grpc_channel_get_channel_stack(channel), resolver);
    GRPC_RESOLVER_UNREF(exec_ctx, resolver, "create");
  } else {
    GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, channel,
                                "client_channel_factory_create_channel");
    channel = NULL;
  }

  GRPC_SECURITY_CONNECTOR_UNREF(&f->security_connector->base,
                                "client_channel_factory_create_channel");
  return channel;
}

static const grpc_client_channel_factory_vtable client_channel_factory_vtable =
    {client_channel_factory_ref, client_channel_factory_unref,
     client_channel_factory_create_subchannel,
     client_channel_factory_create_channel};

/* Create a secure client channel:
   Asynchronously: - resolve target
                   - connect to it (trying alternatives as presented)
                   - perform handshakes */
grpc_channel *grpc_secure_channel_create(grpc_channel_credentials *creds,
                                         const char *target,
                                         const grpc_channel_args *args,
                                         void *reserved) {
  grpc_arg connector_arg;
  grpc_channel_args *args_copy;
  grpc_channel_args *new_args_from_connector;
  grpc_channel_security_connector *security_connector;
  client_channel_factory *f;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GRPC_API_TRACE(
      "grpc_secure_channel_create(creds=%p, target=%s, args=%p, "
      "reserved=%p)",
      4, (creds, target, args, reserved));
  GPR_ASSERT(reserved == NULL);

  if (grpc_find_security_connector_in_args(args) != NULL) {
    gpr_log(GPR_ERROR, "Cannot set security context in channel args.");
    grpc_exec_ctx_finish(&exec_ctx);
    return grpc_lame_client_channel_create(
        target, GRPC_STATUS_INTERNAL,
        "Security connector exists in channel args.");
  }

  if (grpc_channel_credentials_create_security_connector(
          creds, target, args, &security_connector, &new_args_from_connector) !=
      GRPC_SECURITY_OK) {
    grpc_exec_ctx_finish(&exec_ctx);
    return grpc_lame_client_channel_create(
        target, GRPC_STATUS_INTERNAL, "Failed to create security connector.");
  }

  connector_arg = grpc_security_connector_to_arg(&security_connector->base);
  args_copy = grpc_channel_args_copy_and_add(
      new_args_from_connector != NULL ? new_args_from_connector : args,
      &connector_arg, 1);

  f = gpr_malloc(sizeof(*f));
  memset(f, 0, sizeof(*f));
  f->base.vtable = &client_channel_factory_vtable;
  gpr_ref_init(&f->refs, 1);

  f->merge_args = grpc_channel_args_copy(args_copy);
  grpc_channel_args_destroy(args_copy);
  if (new_args_from_connector != NULL) {
    grpc_channel_args_destroy(new_args_from_connector);
  }

  GRPC_SECURITY_CONNECTOR_REF(&security_connector->base,
                              "grpc_secure_channel_create");
  f->security_connector = security_connector;

  grpc_channel *channel = client_channel_factory_create_channel(
      &exec_ctx, &f->base, target, GRPC_CLIENT_CHANNEL_TYPE_REGULAR, NULL);
  if (channel != NULL) {
    f->master = channel;
    GRPC_CHANNEL_INTERNAL_REF(f->master, "grpc_secure_channel_create");
  }

  grpc_client_channel_factory_unref(&exec_ctx, &f->base);
  grpc_exec_ctx_finish(&exec_ctx);

  return channel; /* may be NULL */
}
