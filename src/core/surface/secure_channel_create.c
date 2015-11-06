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

#include "src/core/census/grpc_filter.h"
#include "src/core/channel/channel_args.h"
#include "src/core/channel/client_channel.h"
#include "src/core/channel/compress_filter.h"
#include "src/core/channel/http_client_filter.h"
#include "src/core/client_config/resolver_registry.h"
#include "src/core/iomgr/tcp_client.h"
#include "src/core/security/auth_filters.h"
#include "src/core/security/credentials.h"
#include "src/core/surface/api_trace.h"
#include "src/core/surface/channel.h"
#include "src/core/transport/chttp2_transport.h"
#include "src/core/tsi/transport_security_interface.h"

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

  grpc_mdctx *mdctx;
} connector;

static void connector_ref(grpc_connector *con) {
  connector *c = (connector *)con;
  gpr_ref(&c->refs);
}

static void connector_unref(grpc_exec_ctx *exec_ctx, grpc_connector *con) {
  connector *c = (connector *)con;
  if (gpr_unref(&c->refs)) {
    grpc_mdctx_unref(c->mdctx);
    /* c->initial_string_buffer does not need to be destroyed */
    gpr_free(c);
  }
}

static void on_secure_handshake_done(grpc_exec_ctx *exec_ctx, void *arg,
                                     grpc_security_status status,
                                     grpc_endpoint *wrapped_endpoint,
                                     grpc_endpoint *secure_endpoint) {
  connector *c = arg;
  grpc_closure *notify;
  gpr_mu_lock(&c->mu);
  if (c->connecting_endpoint == NULL) {
    memset(c->result, 0, sizeof(*c->result));
    gpr_mu_unlock(&c->mu);
  } else if (status != GRPC_SECURITY_OK) {
    GPR_ASSERT(c->connecting_endpoint == wrapped_endpoint);
    gpr_log(GPR_ERROR, "Secure handshake failed with error %d.", status);
    memset(c->result, 0, sizeof(*c->result));
    c->connecting_endpoint = NULL;
    gpr_mu_unlock(&c->mu);
  } else {
    GPR_ASSERT(c->connecting_endpoint == wrapped_endpoint);
    c->connecting_endpoint = NULL;
    gpr_mu_unlock(&c->mu);
    c->result->transport = grpc_create_chttp2_transport(
        exec_ctx, c->args.channel_args, secure_endpoint, c->mdctx, 1);
    grpc_chttp2_transport_start_reading(exec_ctx, c->result->transport, NULL,
                                        0);
    c->result->filters = gpr_malloc(sizeof(grpc_channel_filter *) * 2);
    c->result->filters[0] = &grpc_http_client_filter;
    c->result->filters[1] = &grpc_client_auth_filter;
    c->result->num_filters = 2;
  }
  notify = c->notify;
  c->notify = NULL;
  notify->cb(exec_ctx, notify->cb_arg, 1);
}

static void on_initial_connect_string_sent(grpc_exec_ctx *exec_ctx, void *arg,
                                           int success) {
  connector *c = arg;
  grpc_security_connector_do_handshake(exec_ctx, &c->security_connector->base,
                                       c->connecting_endpoint,
                                       on_secure_handshake_done, c);
}

static void connected(grpc_exec_ctx *exec_ctx, void *arg, int success) {
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
      grpc_security_connector_do_handshake(exec_ctx,
                                           &c->security_connector->base, tcp,
                                           on_secure_handshake_done, c);
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
  grpc_subchannel_factory base;
  gpr_refcount refs;
  grpc_mdctx *mdctx;
  grpc_channel_args *merge_args;
  grpc_channel_security_connector *security_connector;
  grpc_channel *master;
} subchannel_factory;

static void subchannel_factory_ref(grpc_subchannel_factory *scf) {
  subchannel_factory *f = (subchannel_factory *)scf;
  gpr_ref(&f->refs);
}

static void subchannel_factory_unref(grpc_exec_ctx *exec_ctx,
                                     grpc_subchannel_factory *scf) {
  subchannel_factory *f = (subchannel_factory *)scf;
  if (gpr_unref(&f->refs)) {
    GRPC_SECURITY_CONNECTOR_UNREF(&f->security_connector->base,
                                  "subchannel_factory");
    GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, f->master, "subchannel_factory");
    grpc_channel_args_destroy(f->merge_args);
    grpc_mdctx_unref(f->mdctx);
    gpr_free(f);
  }
}

static grpc_subchannel *subchannel_factory_create_subchannel(
    grpc_exec_ctx *exec_ctx, grpc_subchannel_factory *scf,
    grpc_subchannel_args *args) {
  subchannel_factory *f = (subchannel_factory *)scf;
  connector *c = gpr_malloc(sizeof(*c));
  grpc_channel_args *final_args =
      grpc_channel_args_merge(args->args, f->merge_args);
  grpc_subchannel *s;
  memset(c, 0, sizeof(*c));
  c->base.vtable = &connector_vtable;
  c->security_connector = f->security_connector;
  c->mdctx = f->mdctx;
  gpr_mu_init(&c->mu);
  grpc_mdctx_ref(c->mdctx);
  gpr_ref_init(&c->refs, 1);
  args->args = final_args;
  args->master = f->master;
  args->mdctx = f->mdctx;
  s = grpc_subchannel_create(&c->base, args);
  grpc_connector_unref(exec_ctx, &c->base);
  grpc_channel_args_destroy(final_args);
  return s;
}

static const grpc_subchannel_factory_vtable subchannel_factory_vtable = {
    subchannel_factory_ref, subchannel_factory_unref,
    subchannel_factory_create_subchannel};

/* Create a secure client channel:
   Asynchronously: - resolve target
                   - connect to it (trying alternatives as presented)
                   - perform handshakes */
grpc_channel *grpc_secure_channel_create(grpc_credentials *creds,
                                         const char *target,
                                         const grpc_channel_args *args,
                                         void *reserved) {
  grpc_channel *channel;
  grpc_arg connector_arg;
  grpc_channel_args *args_copy;
  grpc_channel_args *new_args_from_connector;
  grpc_channel_security_connector *security_connector;
  grpc_mdctx *mdctx;
  grpc_resolver *resolver;
  subchannel_factory *f;
#define MAX_FILTERS 3
  const grpc_channel_filter *filters[MAX_FILTERS];
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  size_t n = 0;

  GRPC_API_TRACE(
      "grpc_secure_channel_create(creds=%p, target=%s, args=%p, "
      "reserved=%p)",
      4, (creds, target, args, reserved));
  GPR_ASSERT(reserved == NULL);

  if (grpc_find_security_connector_in_args(args) != NULL) {
    gpr_log(GPR_ERROR, "Cannot set security context in channel args.");
    grpc_exec_ctx_finish(&exec_ctx);
    return grpc_lame_client_channel_create(
        target, GRPC_STATUS_INVALID_ARGUMENT,
        "Security connector exists in channel args.");
  }

  if (grpc_credentials_create_security_connector(
          creds, target, args, NULL, &security_connector,
          &new_args_from_connector) != GRPC_SECURITY_OK) {
    grpc_exec_ctx_finish(&exec_ctx);
    return grpc_lame_client_channel_create(
        target, GRPC_STATUS_INVALID_ARGUMENT,
        "Failed to create security connector.");
  }
  mdctx = grpc_mdctx_create();

  connector_arg = grpc_security_connector_to_arg(&security_connector->base);
  args_copy = grpc_channel_args_copy_and_add(
      new_args_from_connector != NULL ? new_args_from_connector : args,
      &connector_arg, 1);
  if (grpc_channel_args_is_census_enabled(args)) {
    filters[n++] = &grpc_client_census_filter;
  }
  filters[n++] = &grpc_compress_filter;
  filters[n++] = &grpc_client_channel_filter;
  GPR_ASSERT(n <= MAX_FILTERS);

  channel = grpc_channel_create_from_filters(&exec_ctx, target, filters, n,
                                             args_copy, mdctx, 1);

  f = gpr_malloc(sizeof(*f));
  f->base.vtable = &subchannel_factory_vtable;
  gpr_ref_init(&f->refs, 1);
  grpc_mdctx_ref(mdctx);
  f->mdctx = mdctx;
  GRPC_SECURITY_CONNECTOR_REF(&security_connector->base, "subchannel_factory");
  f->security_connector = security_connector;
  f->merge_args = grpc_channel_args_copy(args_copy);
  f->master = channel;
  GRPC_CHANNEL_INTERNAL_REF(channel, "subchannel_factory");
  resolver = grpc_resolver_create(target, &f->base);
  if (!resolver) {
    grpc_exec_ctx_finish(&exec_ctx);
    return NULL;
  }

  grpc_client_channel_set_resolver(
      &exec_ctx, grpc_channel_get_channel_stack(channel), resolver);
  GRPC_RESOLVER_UNREF(&exec_ctx, resolver, "create");
  grpc_subchannel_factory_unref(&exec_ctx, &f->base);
  GRPC_SECURITY_CONNECTOR_UNREF(&security_connector->base, "channel_create");

  grpc_channel_args_destroy(args_copy);
  if (new_args_from_connector != NULL) {
    grpc_channel_args_destroy(new_args_from_connector);
  }

  grpc_exec_ctx_finish(&exec_ctx);

  return channel;
}
