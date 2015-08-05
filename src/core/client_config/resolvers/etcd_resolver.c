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

#include "src/core/client_config/resolvers/etcd_resolver.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/grpc_etcd.h>

#include "src/core/client_config/lb_policies/pick_first.h"
#include "src/core/client_config/resolver_registry.h"
#include "src/core/iomgr/resolve_address.h"
#include "src/core/support/string.h"
#include "src/core/httpcli/httpcli.h"

typedef struct {
  /** base class: must be first */
  grpc_resolver base;
  /** refcount */
  gpr_refcount refs;
  /** name to resolve */
  char *name;
  /** authority */
  char *authority;
  /** subchannel factory */
  grpc_subchannel_factory *subchannel_factory;
  /** load balancing policy factory */
  grpc_lb_policy *(*lb_policy_factory)(grpc_subchannel **subchannels,
                                       size_t num_subchannels);

  /** mutex guarding the rest of the state */
  gpr_mu mu;
  /** are we currently resolving? */
  int resolving;
  /** which version of resolved_config have we published? */
  int published_version;
  /** which version of resolved_config is current? */
  int resolved_version;
  /** pending next completion, or NULL */
  grpc_iomgr_closure *next_completion;
  /** target config address for next completion */
  grpc_client_config **target_config;
  /** current (fully resolved) config */
  grpc_client_config *resolved_config;
  /** is HTTP request/response done?*/
  int http_done;
  grpc_pollset pollset;
} etcd_resolver;

static void etcd_destroy(grpc_resolver *r);

static void etcd_start_resolving_locked(etcd_resolver *r);
static void etcd_maybe_finish_next_locked(etcd_resolver *r);

static void etcd_shutdown(grpc_resolver *r);
static void etcd_channel_saw_error(grpc_resolver *r,
                                  struct sockaddr *failing_address,
                                  int failing_address_len);
static void etcd_next(grpc_resolver *r, grpc_client_config **target_config,
                     grpc_iomgr_closure *on_complete);

static const grpc_resolver_vtable etcd_resolver_vtable = {
    etcd_destroy, etcd_shutdown, etcd_channel_saw_error, etcd_next};

static void etcd_shutdown(grpc_resolver *resolver) {
  etcd_resolver *r = (etcd_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  if (r->next_completion != NULL) {
    *r->target_config = NULL;
    grpc_iomgr_add_callback(r->next_completion);
    r->next_completion = NULL;
  }
  gpr_mu_unlock(&r->mu);
}

static void etcd_channel_saw_error(grpc_resolver *resolver, struct sockaddr *sa,
                                  int len) {
  etcd_resolver *r = (etcd_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  if (!r->resolving) {
    etcd_start_resolving_locked(r);
  }
  gpr_mu_unlock(&r->mu);
}

static void etcd_next(grpc_resolver *resolver,
                     grpc_client_config **target_config,
                     grpc_iomgr_closure *on_complete) {
  etcd_resolver *r = (etcd_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_config = target_config;
  if (r->resolved_version == 0 && !r->resolving) {
    etcd_start_resolving_locked(r);
  } else {
    etcd_maybe_finish_next_locked(r);
  }
  gpr_mu_unlock(&r->mu);
}

/*static void etcd_on_resolved(void *arg, grpc_resolved_addresses *addresses) {
  etcd_resolver *r = arg;
  grpc_client_config *config = NULL;
  grpc_subchannel **subchannels;
  grpc_subchannel_args args;
  grpc_lb_policy *lb_policy;
  size_t i;
  if (addresses) {
    config = grpc_client_config_create();
    subchannels = gpr_malloc(sizeof(grpc_subchannel *) * addresses->naddrs);
    for (i = 0; i < addresses->naddrs; i++) {
      memset(&args, 0, sizeof(args));
      args.addr = (struct sockaddr *)(addresses->addrs[i].addr);
      args.addr_len = addresses->addrs[i].len;
      subchannels[i] = grpc_subchannel_factory_create_subchannel(
          r->subchannel_factory, &args);
    }
    lb_policy = r->lb_policy_factory(subchannels, addresses->naddrs);
    grpc_client_config_set_lb_policy(config, lb_policy);
    GRPC_LB_POLICY_UNREF(lb_policy, "construction");
    grpc_resolved_addresses_destroy(addresses);
    gpr_free(subchannels);
  }
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(r->resolving);
  r->resolving = 0;
  if (r->resolved_config) {
    grpc_client_config_unref(r->resolved_config);
  }
  r->resolved_config = config;
  r->resolved_version++;
  etcd_maybe_finish_next_locked(r);
  gpr_mu_unlock(&r->mu);

  GRPC_RESOLVER_UNREF(&r->base, "etcd-resolving");
}*/

/** Parse json format address of a etcd node */
/*static char *etcd_parse_address(char *buffer, int buffer_len) {
  const char *host;
  const char *port;
  char *address;
  grpc_json *json;
  grpc_json *cur;

  address = NULL;
  json = grpc_json_parse_string_with_len(buffer, buffer_len);
  if (json != NULL) {
    host = NULL;
    port = NULL;
    for (cur = json->child; cur != NULL; cur = cur->next) {
      if (!strcmp(cur->key, "host")) {
        host = cur->value;
        if (port != NULL) {
          break;
        }
      } else if (!strcmp(cur->key, "port")) {
        port = cur->value;
        if (host != NULL) {
          break;
        }
      }
    }
    if (host != NULL && port != NULL) {
      address = gpr_malloc(GRPC_MAX_SOCKADDR_SIZE);
      memset(address, 0, GRPC_MAX_SOCKADDR_SIZE);
      strcat(address, host);
      strcat(address, ":");
      strcat(address, port);
    }
    grpc_json_destroy(json);
  }

  return address;
}*/

static void etcd_on_response(void *arg, const grpc_httpcli_response *response) {
  etcd_resolver *r = (etcd_resolver *)arg;
  gpr_log(GPR_INFO, "etcd resolver");
  if (response == NULL || response->status != 200) {
    gpr_log(GPR_ERROR, "Error in getting etcd node %s", r->name);
    return;
  }
  gpr_log(GPR_INFO, response->body);
  gpr_mu_lock(GRPC_POLLSET_MU(&r->pollset));
  r->http_done = 1;
  grpc_pollset_kick(&r->pollset);
  gpr_mu_unlock(GRPC_POLLSET_MU(&r->pollset));
}

static void etcd_resolve_address(etcd_resolver *r) {
  grpc_httpcli_request request;
  grpc_httpcli_context context;
  gpr_timespec deadline;
  char *path;
  gpr_asprintf(&path, "/v2/keys%s", r->name);

  memset(&request, 0, sizeof(request));
  request.host = r->authority;
  request.path = path;
  request.use_ssl = 0;

  grpc_httpcli_context_init(&context);
  grpc_pollset_init(&r->pollset);

  gpr_log(GPR_INFO, "authority: %s, name: %s", r->authority, r->name);
  gpr_log(GPR_INFO, path);
  deadline = gpr_time_from_seconds(5, GPR_TIMESPAN);

  grpc_httpcli_get(&context, &r->pollset, &request, gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), deadline), etcd_on_response, r);

  gpr_log(GPR_INFO, "hello");

  gpr_mu_lock(GRPC_POLLSET_MU(&r->pollset));
  while (r->http_done == 0) {
    grpc_pollset_worker worker;
    grpc_pollset_work(&r->pollset, &worker,
                      gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(&r->pollset));

  grpc_httpcli_context_destroy(&context);
  grpc_pollset_destroy(&r->pollset);
  gpr_free(path);
}

static void etcd_start_resolving_locked(etcd_resolver *r) {
  GRPC_RESOLVER_REF(&r->base, "etcd-resolving");
  GPR_ASSERT(!r->resolving);
  r->resolving = 1;
  etcd_resolve_address(r);
}

static void etcd_maybe_finish_next_locked(etcd_resolver *r) {
  if (r->next_completion != NULL &&
      r->resolved_version != r->published_version) {
    *r->target_config = r->resolved_config;
    if (r->resolved_config) {
      grpc_client_config_ref(r->resolved_config);
    }
    grpc_iomgr_add_callback(r->next_completion);
    r->next_completion = NULL;
    r->published_version = r->resolved_version;
  }
}

static void etcd_destroy(grpc_resolver *gr) {
  etcd_resolver *r = (etcd_resolver *)gr;
  gpr_mu_destroy(&r->mu);
  if (r->resolved_config) {
    grpc_client_config_unref(r->resolved_config);
  }
  grpc_subchannel_factory_unref(r->subchannel_factory);
  gpr_free(r->name);
  gpr_free(r->authority);
  gpr_free(r);
}

static grpc_resolver *etcd_create(
    grpc_uri *uri,
    grpc_lb_policy *(*lb_policy_factory)(grpc_subchannel **subchannels,
                                         size_t num_subchannels),
    grpc_subchannel_factory *subchannel_factory) {
  etcd_resolver *r;
  const char *path = uri->path;

  if (0 == strcmp(uri->authority, "")) {
    gpr_log(GPR_ERROR, "No authority specified in etcd uri");
    return NULL;
  }

  r = gpr_malloc(sizeof(etcd_resolver));
  memset(r, 0, sizeof(*r));
  gpr_ref_init(&r->refs, 1);
  gpr_mu_init(&r->mu);
  grpc_resolver_init(&r->base, &etcd_resolver_vtable);
  r->name = gpr_strdup(path);
  if (r->name[strlen(r->name) - 1] == '/') {
    r->name[strlen(r->name) - 1] = 0;
  }
  r->authority = gpr_strdup(uri->authority);
  r->subchannel_factory = subchannel_factory;
  r->lb_policy_factory = lb_policy_factory;
  grpc_subchannel_factory_ref(subchannel_factory);
  return &r->base;
}

static void etcd_plugin_init() {
  grpc_register_resolver_type("etcd",
                              grpc_etcd_resolver_factory_create());
}

void grpc_etcd_register() {
  grpc_register_plugin(etcd_plugin_init, NULL);
}

/*
 * FACTORY
 */

static void etcd_factory_ref(grpc_resolver_factory *factory) {}

static void etcd_factory_unref(grpc_resolver_factory *factory) {}

static grpc_resolver *etcd_factory_create_resolver(
    grpc_resolver_factory *factory, grpc_uri *uri,
    grpc_subchannel_factory *subchannel_factory) {
  return etcd_create(uri, grpc_create_pick_first_lb_policy,
                    subchannel_factory);
}

static const grpc_resolver_factory_vtable etcd_factory_vtable = {
    etcd_factory_ref, etcd_factory_unref, etcd_factory_create_resolver};
static grpc_resolver_factory etcd_resolver_factory = {&etcd_factory_vtable};

grpc_resolver_factory *grpc_etcd_resolver_factory_create() {
  return &etcd_resolver_factory;
}
