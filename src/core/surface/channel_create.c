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

#include "src/core/channel/channel_args.h"
#include "src/core/channel/client_channel.h"
#include "src/core/client_config/resolver_registry.h"
#include "src/core/surface/channel.h"

typedef struct {
  grpc_connector base;
  gpr_refcount refs;
  struct sockaddr *addr;
  int addr_len;
  grpc_channel_args *args;
} connector;

typedef struct {
  grpc_subchannel_factory base;
  gpr_refcount refs;
  grpc_channel_args *args;
} subchannel_factory;

static void subchannel_factory_ref(grpc_subchannel_factory *scf) {
  subchannel_factory *f = (subchannel_factory*)scf;
  gpr_ref(&f->refs);
}

static void subchannel_factory_unref(grpc_subchannel_factory *scf) {
  subchannel_factory *f = (subchannel_factory*)scf;
  if (gpr_unref(&f->refs)) {
    grpc_channel_args_destroy(f->args);
    gpr_free(f);
  }
}

static grpc_subchannel *subchannel_factory_create_subchannel(grpc_subchannel_factory *scf, grpc_subchannel_args *args) {
  subchannel_factory *f = (subchannel_factory*)scf;
  connector *c = gpr_malloc(sizeof(*c));
  c->base.vtable = &connector_vtable;
  gpr_ref_init(&c->refs, 1);
  c->addr = gpr_malloc(args->addr_len);
  memcpy(c->addr, args->addr, args->addr_len);
  c->addr_len = args->addr_len;
  c->args = grpc_channel_args_merge(args->args, f->args);

  return grpc_subchannel_create(&c->base);
}

static const grpc_subchannel_factory_vtable subchannel_factory_vtable = {subchannel_factory_ref, subchannel_factory_unref, subchannel_factory_create_subchannel};

/* Create a client channel:
   Asynchronously: - resolve target
                   - connect to it (trying alternatives as presented)
                   - perform handshakes */
grpc_channel *grpc_channel_create(const char *target,
                                  const grpc_channel_args *args) {
  grpc_channel *channel = NULL;
  subchannel_factory *scfactory = gpr_malloc(sizeof(*scfactory));
#define MAX_FILTERS 3
  const grpc_channel_filter *filters[MAX_FILTERS];
  grpc_resolver *resolver;
  int n = 0;
  /* TODO(census)
  if (grpc_channel_args_is_census_enabled(args)) {
    filters[n++] = &grpc_client_census_filter;
    } */
  filters[n++] = &grpc_client_channel_filter;
  GPR_ASSERT(n <= MAX_FILTERS);

  scfactory->base.vtable = &subchannel_factory_vtable;
  gpr_ref_init(&scfactory->refs, 1);
  scfactory->args = grpc_channel_args_copy(args);

  resolver = grpc_resolver_create(target, &scfactory->base);
  if (!resolver) {
    return NULL;
  }

  channel = grpc_channel_create_from_filters(filters, n, args, grpc_mdctx_create(), 1);
  grpc_client_channel_set_resolver(grpc_channel_get_channel_stack(channel), resolver);

  return channel;
}
