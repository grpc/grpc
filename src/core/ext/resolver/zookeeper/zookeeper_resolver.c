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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include <grpc/grpc_zookeeper.h>
#include <zookeeper/zookeeper.h>

#include "src/core/ext/client_config/lb_policy_registry.h"
#include "src/core/ext/client_config/resolver_registry.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/api_trace.h"

/** Zookeeper session expiration time in milliseconds */
#define GRPC_ZOOKEEPER_SESSION_TIMEOUT 15000

typedef struct {
  /** base class: must be first */
  grpc_resolver base;
  /** refcount */
  gpr_refcount refs;
  /** name to resolve */
  char *name;
  /** subchannel factory */
  grpc_client_channel_factory *client_channel_factory;
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

  /** zookeeper handle */
  zhandle_t *zookeeper_handle;
  /** zookeeper resolved addresses */
  grpc_resolved_addresses *resolved_addrs;
  /** total number of addresses to be resolved */
  int resolved_total;
  /** number of addresses resolved */
  int resolved_num;
} zookeeper_resolver;

static void zookeeper_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *r);

static void zookeeper_start_resolving_locked(zookeeper_resolver *r);
static void zookeeper_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                               zookeeper_resolver *r);

static void zookeeper_shutdown(grpc_exec_ctx *exec_ctx, grpc_resolver *r);
static void zookeeper_channel_saw_error(grpc_exec_ctx *exec_ctx,
                                        grpc_resolver *r);
static void zookeeper_next(grpc_exec_ctx *exec_ctx, grpc_resolver *r,
                           grpc_client_config **target_config,
                           grpc_closure *on_complete);

static const grpc_resolver_vtable zookeeper_resolver_vtable = {
    zookeeper_destroy, zookeeper_shutdown, zookeeper_channel_saw_error,
    zookeeper_next};

static void zookeeper_shutdown(grpc_exec_ctx *exec_ctx,
                               grpc_resolver *resolver) {
  zookeeper_resolver *r = (zookeeper_resolver *)resolver;
  grpc_closure *call = NULL;
  gpr_mu_lock(&r->mu);
  if (r->next_completion != NULL) {
    *r->target_config = NULL;
    call = r->next_completion;
    r->next_completion = NULL;
  }
  zookeeper_close(r->zookeeper_handle);
  gpr_mu_unlock(&r->mu);
  if (call != NULL) {
    call->cb(exec_ctx, call->cb_arg, 1);
  }
}

static void zookeeper_channel_saw_error(grpc_exec_ctx *exec_ctx,
                                        grpc_resolver *resolver) {
  zookeeper_resolver *r = (zookeeper_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  if (r->resolving == 0) {
    zookeeper_start_resolving_locked(r);
  }
  gpr_mu_unlock(&r->mu);
}

static void zookeeper_next(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver,
                           grpc_client_config **target_config,
                           grpc_closure *on_complete) {
  zookeeper_resolver *r = (zookeeper_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(r->next_completion == NULL);
  r->next_completion = on_complete;
  r->target_config = target_config;
  if (r->resolved_version == 0 && r->resolving == 0) {
    zookeeper_start_resolving_locked(r);
  } else {
    zookeeper_maybe_finish_next_locked(exec_ctx, r);
  }
  gpr_mu_unlock(&r->mu);
}

/** Zookeeper global watcher for connection management
    TODO: better connection management besides logs */
static void zookeeper_global_watcher(zhandle_t *zookeeper_handle, int type,
                                     int state, const char *path,
                                     void *watcher_ctx) {
  if (type == ZOO_SESSION_EVENT) {
    if (state == ZOO_EXPIRED_SESSION_STATE) {
      gpr_log(GPR_ERROR, "Zookeeper session expired");
    } else if (state == ZOO_AUTH_FAILED_STATE) {
      gpr_log(GPR_ERROR, "Zookeeper authentication failed");
    }
  }
}

/** Zookeeper watcher triggered by changes to watched nodes
    Once triggered, it tries to resolve again to get updated addresses */
static void zookeeper_watcher(zhandle_t *zookeeper_handle, int type, int state,
                              const char *path, void *watcher_ctx) {
  if (watcher_ctx != NULL) {
    zookeeper_resolver *r = (zookeeper_resolver *)watcher_ctx;
    if (state == ZOO_CONNECTED_STATE) {
      gpr_mu_lock(&r->mu);
      if (r->resolving == 0) {
        zookeeper_start_resolving_locked(r);
      }
      gpr_mu_unlock(&r->mu);
    }
  }
}

/** Callback function after getting all resolved addresses
    Creates a subchannel for each address */
static void zookeeper_on_resolved(grpc_exec_ctx *exec_ctx, void *arg,
                                  grpc_resolved_addresses *addresses) {
  zookeeper_resolver *r = arg;
  grpc_client_config *config = NULL;
  grpc_lb_policy *lb_policy;

  if (addresses != NULL) {
    grpc_lb_policy_args lb_policy_args;
    config = grpc_client_config_create();
    lb_policy_args.addresses = addresses;
    lb_policy_args.client_channel_factory = r->client_channel_factory;
    lb_policy =
        grpc_lb_policy_create(exec_ctx, r->lb_policy_name, &lb_policy_args);

    if (lb_policy != NULL) {
      grpc_client_config_set_lb_policy(config, lb_policy);
      GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "construction");
    }
    grpc_resolved_addresses_destroy(addresses);
  }
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(r->resolving == 1);
  r->resolving = 0;
  if (r->resolved_config != NULL) {
    grpc_client_config_unref(exec_ctx, r->resolved_config);
  }
  r->resolved_config = config;
  r->resolved_version++;
  zookeeper_maybe_finish_next_locked(exec_ctx, r);
  gpr_mu_unlock(&r->mu);

  GRPC_RESOLVER_UNREF(exec_ctx, &r->base, "zookeeper-resolving");
}

/** Callback function for each DNS resolved address */
static void zookeeper_dns_resolved(grpc_exec_ctx *exec_ctx, void *arg,
                                   grpc_resolved_addresses *addresses) {
  size_t i;
  zookeeper_resolver *r = arg;
  int resolve_done = 0;

  gpr_mu_lock(&r->mu);
  r->resolved_num++;
  r->resolved_addrs->addrs =
      gpr_realloc(r->resolved_addrs->addrs,
                  sizeof(grpc_resolved_address) *
                      (r->resolved_addrs->naddrs + addresses->naddrs));
  for (i = 0; i < addresses->naddrs; i++) {
    memcpy(r->resolved_addrs->addrs[i + r->resolved_addrs->naddrs].addr,
           addresses->addrs[i].addr, addresses->addrs[i].len);
    r->resolved_addrs->addrs[i + r->resolved_addrs->naddrs].len =
        addresses->addrs[i].len;
  }

  r->resolved_addrs->naddrs += addresses->naddrs;
  grpc_resolved_addresses_destroy(addresses);

  /** Wait for all addresses to be resolved */
  resolve_done = (r->resolved_num == r->resolved_total);
  gpr_mu_unlock(&r->mu);
  if (resolve_done) {
    zookeeper_on_resolved(exec_ctx, r, r->resolved_addrs);
  }
}

/** Parses JSON format address of a zookeeper node */
static char *zookeeper_parse_address(const char *value, size_t value_len) {
  grpc_json *json;
  grpc_json *cur;
  const char *host;
  const char *port;
  char *buffer;
  char *address = NULL;

  buffer = gpr_malloc(value_len);
  memcpy(buffer, value, value_len);
  json = grpc_json_parse_string_with_len(buffer, value_len);
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
      gpr_asprintf(&address, "%s:%s", host, port);
    }
    grpc_json_destroy(json);
  }
  gpr_free(buffer);

  return address;
}

static void zookeeper_get_children_node_completion(int rc, const char *value,
                                                   int value_len,
                                                   const struct Stat *stat,
                                                   const void *arg) {
  char *address = NULL;
  zookeeper_resolver *r = (zookeeper_resolver *)arg;
  int resolve_done = 0;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  if (rc != 0) {
    gpr_log(GPR_ERROR, "Error in getting a child node of %s", r->name);
    grpc_exec_ctx_finish(&exec_ctx);
    return;
  }

  address = zookeeper_parse_address(value, (size_t)value_len);
  if (address != NULL) {
    /** Further resolves address by DNS */
    grpc_resolve_address(&exec_ctx, address, NULL, zookeeper_dns_resolved, r);
    gpr_free(address);
  } else {
    gpr_log(GPR_ERROR, "Error in resolving a child node of %s", r->name);
    gpr_mu_lock(&r->mu);
    r->resolved_total--;
    resolve_done = (r->resolved_num == r->resolved_total);
    gpr_mu_unlock(&r->mu);
    if (resolve_done) {
      zookeeper_on_resolved(&exec_ctx, r, r->resolved_addrs);
    }
  }

  grpc_exec_ctx_finish(&exec_ctx);
}

static void zookeeper_get_children_completion(
    int rc, const struct String_vector *children, const void *arg) {
  char *path;
  int status;
  int i;
  zookeeper_resolver *r = (zookeeper_resolver *)arg;

  if (rc != 0) {
    gpr_log(GPR_ERROR, "Error in getting zookeeper children of %s", r->name);
    return;
  }

  if (children->count == 0) {
    gpr_log(GPR_ERROR, "Error in resolving zookeeper address %s", r->name);
    return;
  }

  r->resolved_addrs = gpr_malloc(sizeof(grpc_resolved_addresses));
  r->resolved_addrs->addrs = NULL;
  r->resolved_addrs->naddrs = 0;
  r->resolved_total = children->count;

  /** TODO: Replace expensive heap allocation with stack
      if we can get maximum length of zookeeper path */
  for (i = 0; i < children->count; i++) {
    gpr_asprintf(&path, "%s/%s", r->name, children->data[i]);
    status = zoo_awget(r->zookeeper_handle, path, zookeeper_watcher, r,
                       zookeeper_get_children_node_completion, r);
    gpr_free(path);
    if (status != 0) {
      gpr_log(GPR_ERROR, "Error in getting zookeeper node %s", path);
    }
  }
}

static void zookeeper_get_node_completion(int rc, const char *value,
                                          int value_len,
                                          const struct Stat *stat,
                                          const void *arg) {
  int status;
  char *address = NULL;
  zookeeper_resolver *r = (zookeeper_resolver *)arg;
  r->resolved_addrs = NULL;
  r->resolved_total = 0;
  r->resolved_num = 0;

  if (rc != 0) {
    gpr_log(GPR_ERROR, "Error in getting zookeeper node %s", r->name);
    return;
  }

  /** If zookeeper node of path r->name does not have address
      (i.e. service node), get its children */
  address = zookeeper_parse_address(value, (size_t)value_len);
  if (address != NULL) {
    r->resolved_addrs = gpr_malloc(sizeof(grpc_resolved_addresses));
    r->resolved_addrs->addrs = NULL;
    r->resolved_addrs->naddrs = 0;
    r->resolved_total = 1;
    /** Further resolves address by DNS */
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_resolve_address(&exec_ctx, address, NULL, zookeeper_dns_resolved, r);
    gpr_free(address);
    grpc_exec_ctx_finish(&exec_ctx);
    return;
  }

  status = zoo_awget_children(r->zookeeper_handle, r->name, zookeeper_watcher,
                              r, zookeeper_get_children_completion, r);
  if (status != 0) {
    gpr_log(GPR_ERROR, "Error in getting zookeeper children of %s", r->name);
  }
}

static void zookeeper_resolve_address(zookeeper_resolver *r) {
  int status;
  status = zoo_awget(r->zookeeper_handle, r->name, zookeeper_watcher, r,
                     zookeeper_get_node_completion, r);
  if (status != 0) {
    gpr_log(GPR_ERROR, "Error in getting zookeeper node %s", r->name);
  }
}

static void zookeeper_start_resolving_locked(zookeeper_resolver *r) {
  GRPC_RESOLVER_REF(&r->base, "zookeeper-resolving");
  GPR_ASSERT(r->resolving == 0);
  r->resolving = 1;
  zookeeper_resolve_address(r);
}

static void zookeeper_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                               zookeeper_resolver *r) {
  if (r->next_completion != NULL &&
      r->resolved_version != r->published_version) {
    *r->target_config = r->resolved_config;
    if (r->resolved_config != NULL) {
      grpc_client_config_ref(r->resolved_config);
    }
    grpc_exec_ctx_enqueue(exec_ctx, r->next_completion, true, NULL);
    r->next_completion = NULL;
    r->published_version = r->resolved_version;
  }
}

static void zookeeper_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *gr) {
  zookeeper_resolver *r = (zookeeper_resolver *)gr;
  gpr_mu_destroy(&r->mu);
  if (r->resolved_config != NULL) {
    grpc_client_config_unref(exec_ctx, r->resolved_config);
  }
  grpc_client_channel_factory_unref(exec_ctx, r->client_channel_factory);
  gpr_free(r->name);
  gpr_free(r->lb_policy_name);
  gpr_free(r);
}

static grpc_resolver *zookeeper_create(grpc_resolver_args *args,
                                       const char *lb_policy_name) {
  zookeeper_resolver *r;
  size_t length;
  char *path = args->uri->path;

  if (0 == strcmp(args->uri->authority, "")) {
    gpr_log(GPR_ERROR, "No authority specified in zookeeper uri");
    return NULL;
  }

  /** Removes the trailing slash if exists */
  length = strlen(path);
  if (length > 1 && path[length - 1] == '/') {
    path[length - 1] = 0;
  }

  r = gpr_malloc(sizeof(zookeeper_resolver));
  memset(r, 0, sizeof(*r));
  gpr_ref_init(&r->refs, 1);
  gpr_mu_init(&r->mu);
  grpc_resolver_init(&r->base, &zookeeper_resolver_vtable);
  r->name = gpr_strdup(path);

  r->client_channel_factory = args->client_channel_factory;
  grpc_client_channel_factory_ref(r->client_channel_factory);

  r->lb_policy_name = gpr_strdup(lb_policy_name);

  /** Initializes zookeeper client */
  zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
  r->zookeeper_handle =
      zookeeper_init(args->uri->authority, zookeeper_global_watcher,
                     GRPC_ZOOKEEPER_SESSION_TIMEOUT, 0, 0, 0);
  if (r->zookeeper_handle == NULL) {
    gpr_log(GPR_ERROR, "Unable to connect to zookeeper server");
    return NULL;
  }

  return &r->base;
}

/*
 * FACTORY
 */

static void zookeeper_factory_ref(grpc_resolver_factory *factory) {}

static void zookeeper_factory_unref(grpc_resolver_factory *factory) {}

static char *zookeeper_factory_get_default_hostname(
    grpc_resolver_factory *factory, grpc_uri *uri) {
  return NULL;
}

static grpc_resolver *zookeeper_factory_create_resolver(
    grpc_resolver_factory *factory, grpc_resolver_args *args) {
  return zookeeper_create(args, "pick_first");
}

static const grpc_resolver_factory_vtable zookeeper_factory_vtable = {
    zookeeper_factory_ref, zookeeper_factory_unref,
    zookeeper_factory_create_resolver, zookeeper_factory_get_default_hostname,
    "zookeeper"};

static grpc_resolver_factory zookeeper_resolver_factory = {
    &zookeeper_factory_vtable};

static grpc_resolver_factory *zookeeper_resolver_factory_create() {
  return &zookeeper_resolver_factory;
}

static void zookeeper_plugin_init() {
  grpc_register_resolver_type(zookeeper_resolver_factory_create());
}

void grpc_zookeeper_register() {
  GRPC_API_TRACE("grpc_zookeeper_register(void)", 0, ());
  grpc_register_plugin(zookeeper_plugin_init, NULL);
}
