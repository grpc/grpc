/*
 * tunnel_connector.c
 *
 *  Created on: Oct 29, 2016
 *      Author: gnirodi
 */

#include "src/core/lib/tunnel/tunnel_connector.h"

#include <grpc/grpc.h>

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/slice.h>
#include <grpc/support/slice_buffer.h>

#include "src/core/ext/client_config/client_channel.h"
#include "src/core/ext/client_config/resolver_registry.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/compress_filter.h"
#include "src/core/lib/channel/http_client_filter.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/tunnel/tunnel.h"


typedef struct {
  grpc_connector base;
  gpr_refcount refs;

  grpc_closure *notify;
  grpc_connect_in_args args;
  grpc_connect_out_args *result;
  grpc_closure initial_string_sent;
  gpr_slice_buffer initial_string_buffer;

  /** The tunneling endpoint associated with this connector */
  grpc_endpoint *tunneling_endpoint;
  /** The tunnel for which this channel factory was created */
  grpc_tunnel *tunnel;

  grpc_closure connected;
} tunnel_connector;

static void connector_ref(grpc_connector *con) {
  tunnel_connector *c = (tunnel_connector *)con;
  gpr_ref(&c->refs);
}

static void connector_unref(grpc_exec_ctx *exec_ctx, grpc_connector *con) {
  tunnel_connector *c = (tunnel_connector *)con;
  if (gpr_unref(&c->refs)) {
    /* c->initial_string_buffer does not need to be destroyed */
    gpr_free(c);
  }
}

static void on_initial_connect_string_sent(grpc_exec_ctx *exec_ctx, void *arg,
                                           grpc_error *error) {
  connector_unref(exec_ctx, arg);
}

static void connected(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  tunnel_connector *c = arg;
  grpc_closure *notify;
  grpc_endpoint *tunneling_ep = c->tunneling_endpoint;
  if (tunneling_ep != NULL) {
    if (!GPR_SLICE_IS_EMPTY(c->args.initial_connect_string)) {
      grpc_closure_init(&c->initial_string_sent, on_initial_connect_string_sent,
                        c);
      gpr_slice_buffer_init(&c->initial_string_buffer);
      gpr_slice_buffer_add(&c->initial_string_buffer,
                           c->args.initial_connect_string);
      connector_ref(arg);
      grpc_endpoint_write(exec_ctx, tunneling_ep, &c->initial_string_buffer,
                          &c->initial_string_sent);
    }
    c->result->transport =
        grpc_create_chttp2_transport(exec_ctx, c->args.channel_args,
                                     tunneling_ep, 1);
    grpc_chttp2_transport_start_reading(exec_ctx, c->result->transport, NULL,
                                        0);
    GPR_ASSERT(c->result->transport);
    c->result->channel_args = grpc_channel_args_copy(c->args.channel_args);
  } else {
    memset(c->result, 0, sizeof(*c->result));
  }
  notify = c->notify;
  c->notify = NULL;
  grpc_exec_ctx_sched(exec_ctx, notify, GRPC_ERROR_REF(error), NULL);
}

static void connector_shutdown(grpc_exec_ctx *exec_ctx, grpc_connector *con) {}

static void connector_connect(grpc_exec_ctx *exec_ctx, grpc_connector *con,
                              const grpc_connect_in_args *args,
                              grpc_connect_out_args *result,
                              grpc_closure *notify) {
  tunnel_connector *c = (tunnel_connector *)con;
  GPR_ASSERT(c->notify == NULL);
  GPR_ASSERT(notify->cb);
  c->notify = notify;
  c->args = *args;
  c->result = result;
  c->tunneling_endpoint = NULL;
  grpc_closure_init(&c->connected, connected, c);

  // TODO(gnirodi): Not sure if we need interested parties.
  c->tunnel->vtable->tunnel_channel_endpoint_create(c->tunnel, exec_ctx,
      &c->connected, &c->tunneling_endpoint, args->interested_parties,
      args->addr, args->addr_len, args->deadline);
}

static const grpc_connector_vtable connector_vtable = {
    connector_ref, connector_unref, connector_shutdown, connector_connect};

typedef struct {
  grpc_client_channel_factory base;
  gpr_refcount refs;
  grpc_channel_args *merge_args;
  grpc_channel *master;

  /** The tunnel for which this channel factory was created */
  grpc_tunnel *tunnel;
} tunnel_channel_factory;

static void tunnel_channel_factory_ref(
    grpc_client_channel_factory *cc_factory) {
  tunnel_channel_factory *f = (tunnel_channel_factory *)cc_factory;
  gpr_ref(&f->refs);
}

static void tunnel_channel_factory_unref(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory) {
  tunnel_channel_factory *f = (tunnel_channel_factory *)cc_factory;
  if (gpr_unref(&f->refs)) {
    if (f->master != NULL) {
      GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, f->master,
                                  "tunnel_channel_factory");
    }
    grpc_channel_args_destroy(f->merge_args);
    gpr_free(f);
  }
}

static grpc_subchannel *tunnel_channel_factory_create_subchannel(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory,
    grpc_subchannel_args *args) {
  tunnel_channel_factory *f = (tunnel_channel_factory *)cc_factory;
  tunnel_connector *c = gpr_malloc(sizeof(*c));
  grpc_channel_args *final_args =
      grpc_channel_args_merge(args->args, f->merge_args);
  grpc_subchannel *s;
  memset(c, 0, sizeof(*c));
  c->base.vtable = &connector_vtable;
  c->tunnel = f->tunnel;
  gpr_ref_init(&c->refs, 1);
  args->args = final_args;
  s = grpc_subchannel_create(exec_ctx, &c->base, args);
  grpc_connector_unref(exec_ctx, &c->base);
  grpc_channel_args_destroy(final_args);
  return s;
}

static grpc_channel *tunnel_channel_factory_create_channel(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory,
    const char *target, grpc_client_channel_type type,
    grpc_channel_args *args) {
  tunnel_channel_factory *f = (tunnel_channel_factory *)cc_factory;
  grpc_channel_args *final_args = grpc_channel_args_merge(args, f->merge_args);
  grpc_channel *channel = grpc_channel_create(exec_ctx, target, final_args,
                                              GRPC_CLIENT_CHANNEL, NULL);
  grpc_channel_args_destroy(final_args);
  grpc_resolver *resolver = grpc_resolver_create(target, &f->base);
  if (!resolver) {
    GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, channel,
                                "tunnel_channel_factory_create_channel");
    return NULL;
  }

  grpc_client_channel_set_resolver(
      exec_ctx, grpc_channel_get_channel_stack(channel), resolver);
  GRPC_RESOLVER_UNREF(exec_ctx, resolver, "create_channel");

  return channel;
}

static const grpc_client_channel_factory_vtable tunnel_channel_factory_vtable =
    {tunnel_channel_factory_ref, tunnel_channel_factory_unref,
     tunnel_channel_factory_create_subchannel,
     tunnel_channel_factory_create_channel};

/* Create a client channel:
   Asynchronously: - resolve target
                   - connect to it (trying alternatives as presented)
                   - perform handshakes */
grpc_channel *tunnel_channel_create(const char *target,
                                    const grpc_channel_args *args,
                                    void *reserved,
                                    grpc_tunnel *tunnel) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_API_TRACE(
      "grpc_insecure_channel_create(target=%p, args=%p, reserved=%p)", 3,
      (target, args, reserved));
  GPR_ASSERT(!reserved);

  tunnel_channel_factory *f = gpr_malloc(sizeof(*f));
  memset(f, 0, sizeof(*f));
  f->base.vtable = &tunnel_channel_factory_vtable;
  f->tunnel = tunnel;
  gpr_ref_init(&f->refs, 1);
  f->merge_args = grpc_channel_args_copy(args);

  grpc_channel *channel = tunnel_channel_factory_create_channel(
      &exec_ctx, &f->base, target, GRPC_CLIENT_CHANNEL_TYPE_REGULAR, NULL);
  if (channel != NULL) {
    f->master = channel;
    GRPC_CHANNEL_INTERNAL_REF(f->master, "grpc_insecure_channel_create");
  }
  grpc_client_channel_factory_unref(&exec_ctx, &f->base);

  grpc_exec_ctx_finish(&exec_ctx);

  return channel != NULL ? channel : grpc_lame_client_channel_create(
                                         target, GRPC_STATUS_INTERNAL,
                                         "Failed to create client channel");
}
