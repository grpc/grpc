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

#include "test/core/end2end/end2end_tests.h"

#include <string.h>

#include "src/core/channel/channel_args.h"
#include "src/core/channel/client_channel.h"
#include "src/core/channel/client_uchannel.h"
#include "src/core/channel/connected_channel.h"
#include "src/core/channel/http_client_filter.h"
#include "src/core/channel/http_server_filter.h"
#include "src/core/client_config/resolver_registry.h"
#include "src/core/iomgr/tcp_client.h"
#include "src/core/surface/channel.h"
#include "src/core/surface/server.h"
#include "src/core/transport/chttp2_transport.h"
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

typedef struct {
  grpc_connector base;
  gpr_refcount refs;

  grpc_closure *notify;
  grpc_connect_in_args args;
  grpc_connect_out_args *result;

  grpc_endpoint *tcp;

  grpc_mdctx *mdctx;

  grpc_closure connected;
} connector;

static void connector_ref(grpc_connector *con) {
  connector *c = (connector *)con;
  gpr_ref(&c->refs);
}

static void connector_unref(grpc_exec_ctx *exec_ctx, grpc_connector *con) {
  connector *c = (connector *)con;
  if (gpr_unref(&c->refs)) {
    grpc_mdctx_unref(c->mdctx);
    gpr_free(c);
  }
}

static void connected(grpc_exec_ctx *exec_ctx, void *arg, int success) {
  connector *c = arg;
  grpc_closure *notify;
  grpc_endpoint *tcp = c->tcp;
  if (tcp != NULL) {
    c->result->transport = grpc_create_chttp2_transport(
        exec_ctx, c->args.channel_args, tcp, c->mdctx, 1);
    grpc_chttp2_transport_start_reading(exec_ctx, c->result->transport, NULL,
                                        0);
    GPR_ASSERT(c->result->transport);
    c->result->filters = gpr_malloc(sizeof(grpc_channel_filter *));
    c->result->filters[0] = &grpc_http_client_filter;
    c->result->num_filters = 1;
  } else {
    memset(c->result, 0, sizeof(*c->result));
  }
  notify = c->notify;
  c->notify = NULL;
  notify->cb(exec_ctx, notify->cb_arg, 1);
}

static void connector_shutdown(grpc_exec_ctx *exec_ctx, grpc_connector *con) {}

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
  c->tcp = NULL;
  grpc_closure_init(&c->connected, connected, c);
  grpc_tcp_client_connect(exec_ctx, &c->connected, &c->tcp,
                          args->interested_parties, args->addr, args->addr_len,
                          args->deadline);
}

static const grpc_connector_vtable connector_vtable = {
    connector_ref, connector_unref, connector_shutdown, connector_connect};

typedef struct {
  grpc_subchannel_factory base;
  gpr_refcount refs;
  grpc_mdctx *mdctx;
  grpc_channel_args *merge_args;
  grpc_channel *master;
  grpc_subchannel **sniffed_subchannel;
} subchannel_factory;

static void subchannel_factory_ref(grpc_subchannel_factory *scf) {
  subchannel_factory *f = (subchannel_factory *)scf;
  gpr_ref(&f->refs);
}

static void subchannel_factory_unref(grpc_exec_ctx *exec_ctx,
                                     grpc_subchannel_factory *scf) {
  subchannel_factory *f = (subchannel_factory *)scf;
  if (gpr_unref(&f->refs)) {
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
  c->mdctx = f->mdctx;
  grpc_mdctx_ref(c->mdctx);
  gpr_ref_init(&c->refs, 1);
  args->mdctx = f->mdctx;
  args->args = final_args;
  args->master = f->master;
  s = grpc_subchannel_create(&c->base, args);
  grpc_connector_unref(exec_ctx, &c->base);
  grpc_channel_args_destroy(final_args);
  *f->sniffed_subchannel = s;
  return s;
}

static const grpc_subchannel_factory_vtable test_subchannel_factory_vtable = {
    subchannel_factory_ref, subchannel_factory_unref,
    subchannel_factory_create_subchannel};

/* The evil twin of grpc_insecure_channel_create. It allows the test to use the
 * custom-built sniffing subchannel_factory */
grpc_channel *channel_create(const char *target, const grpc_channel_args *args,
                             grpc_subchannel **sniffed_subchannel) {
  grpc_channel *channel = NULL;
#define MAX_FILTERS 1
  const grpc_channel_filter *filters[MAX_FILTERS];
  grpc_resolver *resolver;
  subchannel_factory *f;
  grpc_mdctx *mdctx = grpc_mdctx_create();
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  size_t n = 0;

  filters[n++] = &grpc_client_channel_filter;
  GPR_ASSERT(n <= MAX_FILTERS);

  channel = grpc_channel_create_from_filters(&exec_ctx, target, filters, n,
                                             args, mdctx, 1);

  f = gpr_malloc(sizeof(*f));
  f->sniffed_subchannel = sniffed_subchannel;
  f->base.vtable = &test_subchannel_factory_vtable;
  gpr_ref_init(&f->refs, 1);
  grpc_mdctx_ref(mdctx);
  f->mdctx = mdctx;
  f->merge_args = grpc_channel_args_copy(args);
  f->master = channel;
  GRPC_CHANNEL_INTERNAL_REF(f->master, "test_subchannel_factory");
  resolver = grpc_resolver_create(target, &f->base);
  if (!resolver) {
    return NULL;
  }

  grpc_client_channel_set_resolver(
      &exec_ctx, grpc_channel_get_channel_stack(channel), resolver);
  GRPC_RESOLVER_UNREF(&exec_ctx, resolver, "test_create");
  grpc_subchannel_factory_unref(&exec_ctx, &f->base);

  grpc_exec_ctx_finish(&exec_ctx);

  return channel;
}

typedef struct micro_fullstack_fixture_data {
  char *localaddr;
  grpc_channel *master_channel;
  grpc_subchannel *sniffed_subchannel;
} micro_fullstack_fixture_data;

static grpc_end2end_test_fixture chttp2_create_fixture_micro_fullstack(
    grpc_channel_args *client_args, grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  micro_fullstack_fixture_data *ffd =
      gpr_malloc(sizeof(micro_fullstack_fixture_data));
  memset(&f, 0, sizeof(f));

  gpr_join_host_port(&ffd->localaddr, "127.0.0.1", port);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create(NULL);

  return f;
}

static void chttp2_init_client_micro_fullstack(grpc_end2end_test_fixture *f,
                                               grpc_channel_args *client_args) {
  micro_fullstack_fixture_data *ffd = f->fixture_data;
  grpc_connectivity_state conn_state;
  char *ipv4_localaddr;

  gpr_asprintf(&ipv4_localaddr, "ipv4:%s", ffd->localaddr);
  ffd->master_channel =
      channel_create(ipv4_localaddr, client_args, &ffd->sniffed_subchannel);
  gpr_free(ipv4_localaddr);
  gpr_log(GPR_INFO, "MASTER CHANNEL %p ", ffd->master_channel);
  /* the following will block. That's ok for this test */
  conn_state = grpc_channel_check_connectivity_state(ffd->master_channel,
                                                     1 /* try to connect */);
  GPR_ASSERT(conn_state == GRPC_CHANNEL_IDLE);

  /* here sniffed_subchannel should be ready to use */
  GPR_ASSERT(conn_state == GRPC_CHANNEL_IDLE);
  GPR_ASSERT(ffd->sniffed_subchannel != NULL);
  f->client = grpc_client_uchannel_create(ffd->sniffed_subchannel, client_args);
  grpc_client_uchannel_set_subchannel(f->client, ffd->sniffed_subchannel);
  gpr_log(GPR_INFO, "CHANNEL WRAPPING SUBCHANNEL: %p(%p)", f->client,
          ffd->sniffed_subchannel);

  GPR_ASSERT(f->client);
}

static void chttp2_init_server_micro_fullstack(grpc_end2end_test_fixture *f,
                                               grpc_channel_args *server_args) {
  micro_fullstack_fixture_data *ffd = f->fixture_data;
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, NULL);
  grpc_server_register_completion_queue(f->server, f->cq, NULL);
  GPR_ASSERT(grpc_server_add_insecure_http2_port(f->server, ffd->localaddr));
  grpc_server_start(f->server);
}

static void chttp2_tear_down_micro_fullstack(grpc_end2end_test_fixture *f) {
  micro_fullstack_fixture_data *ffd = f->fixture_data;
  grpc_channel_destroy(ffd->master_channel);
  ffd->master_channel = NULL;
  gpr_free(ffd->localaddr);
  gpr_free(ffd);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/micro_fullstack", FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION,
     chttp2_create_fixture_micro_fullstack, chttp2_init_client_micro_fullstack,
     chttp2_init_server_micro_fullstack, chttp2_tear_down_micro_fullstack},
};

int main(int argc, char **argv) {
  size_t i;

  grpc_test_init(argc, argv);
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(configs[i]);
  }

  grpc_shutdown();

  return 0;
}
