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

#include "src/core/client_config/resolvers/dns_resolver.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/string_util.h>

#include "src/core/client_config/lb_policy_registry.h"
#include "src/core/client_config/subchannel_factory_decorators/add_channel_arg.h"
#include "src/core/iomgr/resolve_address.h"
#include "src/core/support/string.h"

typedef struct {
  /** base class: must be first */
  grpc_resolver base;
  /** refcount */
  gpr_refcount refs;
  /** name to resolve */
  char *name;
  /** default port to use */
  char *default_port;
  /** subchannel factory */
  grpc_subchannel_factory *subchannel_factory;
  /** load balancing policy name */
  char *lb_policy_name;

  /** mutex guarding the rest of the state */
  gpr_mu mu;
  /** are we currently resolving? */
  int resolving;
  /** which version of resolved_config have we published? */
  int published_version;
  /** which version of resolved_config is current? */
  int resolved_version;
  /** pending next completion, or NULL */
  grpc_closure *next_completion;
  /** target config address for next completion */
  grpc_client_config **target_config;
  /** current (fully resolved) config */
  grpc_client_config *resolved_config;
} dns_resolver;

static void dns_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *r);

static void dns_start_resolving_locked(dns_resolver *r);
static void dns_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                         dns_resolver *r);

static void dns_shutdown(grpc_exec_ctx *exec_ctx, grpc_resolver *r);
static void dns_channel_saw_error(grpc_exec_ctx *exec_ctx, grpc_resolver *r,
                                  struct sockaddr *failing_address,
                                  int failing_address_len);
static void dns_next(grpc_exec_ctx *exec_ctx, grpc_resolver *r,
                     grpc_client_config **target_config,
                     grpc_closure *on_complete);

static const grpc_resolver_vtable dns_resolver_vtable = {
    dns_destroy, dns_shutdown, dns_channel_saw_error, dns_next};

static void dns_shutdown(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver) {
  dns_resolver *r = (dns_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  if (r->next_completion != NULL) {
    *r->target_config = NULL;
    grpc_exec_ctx_enqueue(exec_ctx, r->next_completion, 1);
    r->next_completion = NULL;
  }
  gpr_mu_unlock(&r->mu);
}

static void dns_channel_saw_error(grpc_exec_ctx *exec_ctx,
                                  grpc_resolver *resolver, struct sockaddr *sa,
                                  int len) {
  dns_resolver *r = (dns_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  if (!r->resolving) {
    dns_start_resolving_locked(r);
  }
  gpr_mu_unlock(&r->mu);
}

static void dns_next(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver,
                     grpc_client_config **target_config,
                     grpc_closure *on_complete) {
  dns_resolver *r = (dns_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_config = target_config;
  if (r->resolved_version == 0 && !r->resolving) {
    dns_start_resolving_locked(r);
  } else {
    dns_maybe_finish_next_locked(exec_ctx, r);
  }
  gpr_mu_unlock(&r->mu);
}

static void dns_on_resolved(grpc_exec_ctx *exec_ctx, void *arg,
                            grpc_resolved_addresses *addresses) {
  dns_resolver *r = arg;
  grpc_client_config *config = NULL;
  grpc_subchannel **subchannels;
  grpc_subchannel_args args;
  grpc_lb_policy *lb_policy;
  size_t i;
  if (addresses) {
    grpc_lb_policy_args lb_policy_args;
    config = grpc_client_config_create();
    subchannels = gpr_malloc(sizeof(grpc_subchannel *) * addresses->naddrs);
    for (i = 0; i < addresses->naddrs; i++) {
      memset(&args, 0, sizeof(args));
      args.addr = (struct sockaddr *)(addresses->addrs[i].addr);
      args.addr_len = (size_t)addresses->addrs[i].len;
      subchannels[i] = grpc_subchannel_factory_create_subchannel(
          exec_ctx, r->subchannel_factory, &args);
    }
    memset(&lb_policy_args, 0, sizeof(lb_policy_args));
    lb_policy_args.subchannels = subchannels;
    lb_policy_args.num_subchannels = addresses->naddrs;
    lb_policy = grpc_lb_policy_create(r->lb_policy_name, &lb_policy_args);
    grpc_client_config_set_lb_policy(config, lb_policy);
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "construction");
    grpc_resolved_addresses_destroy(addresses);
    gpr_free(subchannels);
  }
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(r->resolving);
  r->resolving = 0;
  if (r->resolved_config) {
    grpc_client_config_unref(exec_ctx, r->resolved_config);
  }
  r->resolved_config = config;
  r->resolved_version++;
  dns_maybe_finish_next_locked(exec_ctx, r);
  gpr_mu_unlock(&r->mu);

  GRPC_RESOLVER_UNREF(exec_ctx, &r->base, "dns-resolving");
}

static void dns_start_resolving_locked(dns_resolver *r) {
  GRPC_RESOLVER_REF(&r->base, "dns-resolving");
  GPR_ASSERT(!r->resolving);
  r->resolving = 1;
  grpc_resolve_address(r->name, r->default_port, dns_on_resolved, r);
}

static void dns_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                         dns_resolver *r) {
  if (r->next_completion != NULL &&
      r->resolved_version != r->published_version) {
    *r->target_config = r->resolved_config;
    if (r->resolved_config) {
      grpc_client_config_ref(r->resolved_config);
    }
    grpc_exec_ctx_enqueue(exec_ctx, r->next_completion, 1);
    r->next_completion = NULL;
    r->published_version = r->resolved_version;
  }
}

static void dns_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *gr) {
  dns_resolver *r = (dns_resolver *)gr;
  gpr_mu_destroy(&r->mu);
  if (r->resolved_config) {
    grpc_client_config_unref(exec_ctx, r->resolved_config);
  }
  grpc_subchannel_factory_unref(exec_ctx, r->subchannel_factory);
  gpr_free(r->name);
  gpr_free(r->default_port);
  gpr_free(r->lb_policy_name);
  gpr_free(r);
}

static grpc_resolver *dns_create(grpc_resolver_args *args,
                                 const char *default_port,
                                 const char *lb_policy_name) {
  dns_resolver *r;
  const char *path = args->uri->path;

  if (0 != strcmp(args->uri->authority, "")) {
    gpr_log(GPR_ERROR, "authority based dns uri's not supported");
    return NULL;
  }

  if (path[0] == '/') ++path;

  r = gpr_malloc(sizeof(dns_resolver));
  memset(r, 0, sizeof(*r));
  gpr_ref_init(&r->refs, 1);
  gpr_mu_init(&r->mu);
  grpc_resolver_init(&r->base, &dns_resolver_vtable);
  r->name = gpr_strdup(path);
  r->default_port = gpr_strdup(default_port);
  r->subchannel_factory = args->subchannel_factory;
  grpc_subchannel_factory_ref(r->subchannel_factory);
  r->lb_policy_name = gpr_strdup(lb_policy_name);
  return &r->base;
}

/*
 * FACTORY
 */

static void dns_factory_ref(grpc_resolver_factory *factory) {}

static void dns_factory_unref(grpc_resolver_factory *factory) {}

static grpc_resolver *dns_factory_create_resolver(
    grpc_resolver_factory *factory, grpc_resolver_args *args) {
  return dns_create(args, "https", "pick_first");
}

char *dns_factory_get_default_host_name(grpc_resolver_factory *factory,
                                        grpc_uri *uri) {
  const char *path = uri->path;
  if (path[0] == '/') ++path;
  return gpr_strdup(path);
}

static const grpc_resolver_factory_vtable dns_factory_vtable = {
    dns_factory_ref, dns_factory_unref, dns_factory_create_resolver,
    dns_factory_get_default_host_name, "dns"};
static grpc_resolver_factory dns_resolver_factory = {&dns_factory_vtable};

grpc_resolver_factory *grpc_dns_resolver_factory_create() {
  return &dns_resolver_factory;
}
