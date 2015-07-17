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

#include "src/core/client_config/resolvers/zookeeper_resolver.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include <grpc/grpc_zookeeper.h>
#include <zookeeper/zookeeper.h>

#include "src/core/client_config/lb_policies/pick_first.h"
#include "src/core/client_config/resolver_registry.h"
#include "src/core/iomgr/resolve_address.h"
#include "src/core/support/string.h"
#include "src/core/json/json.h"

#define GRPC_MAX_ZOOKEEPER_BUFFER_SIZE 1024
#define GRPC_ZOOKEEPER_TIMEOUT 15000
#define GRPC_ZOOKEEPER_WATCH 0

typedef struct {
  /** base class: must be first */
  grpc_resolver base;
  /** refcount */
  gpr_refcount refs;
  /** name to resolve */
  char *name;
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

  /** zookeeper handle */
  zhandle_t *zookeeper_handle;
  /** zookeeper resolved addresses */
  grpc_resolved_addresses * resolved_addrs;
  /** total number of addresses to be resolved */
  int resolved_total;
  /** number of addresses resolved */
  int resolved_num;
} zookeeper_resolver;

static void zookeeper_destroy(grpc_resolver *r);

static void zookeeper_start_resolving_locked(zookeeper_resolver *r);
static void zookeeper_maybe_finish_next_locked(zookeeper_resolver *r);

static void zookeeper_shutdown(grpc_resolver *r);
static void zookeeper_channel_saw_error(grpc_resolver *r,
                                  struct sockaddr *failing_address,
                                  int failing_address_len);
static void zookeeper_next(grpc_resolver *r, grpc_client_config **target_config,
                     grpc_iomgr_closure *on_complete);

static const grpc_resolver_vtable zookeeper_resolver_vtable = {
    zookeeper_destroy, zookeeper_shutdown, zookeeper_channel_saw_error, zookeeper_next};

static void zookeeper_shutdown(grpc_resolver *resolver) {
  zookeeper_resolver *r = (zookeeper_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  if (r->next_completion != NULL) {
    *r->target_config = NULL;
    grpc_iomgr_add_callback(r->next_completion);
    r->next_completion = NULL;
  }
  zookeeper_close(r->zookeeper_handle);
  gpr_mu_unlock(&r->mu);
}

static void zookeeper_channel_saw_error(grpc_resolver *resolver, struct sockaddr *sa,
                                  int len) {
  zookeeper_resolver *r = (zookeeper_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  if (!r->resolving) {
    zookeeper_start_resolving_locked(r);
  }
  gpr_mu_unlock(&r->mu);
}

static void zookeeper_next(grpc_resolver *resolver,
                     grpc_client_config **target_config,
                     grpc_iomgr_closure *on_complete) {
  zookeeper_resolver *r = (zookeeper_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_config = target_config;
  if (r->resolved_version == 0 && !r->resolving) {
    zookeeper_start_resolving_locked(r);
  } else {
    zookeeper_maybe_finish_next_locked(r);
  }
  gpr_mu_unlock(&r->mu);
}

static void zookeeper_on_resolved(void *arg, grpc_resolved_addresses *addresses) {
  zookeeper_resolver *r = arg;
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
  zookeeper_maybe_finish_next_locked(r);
  gpr_mu_unlock(&r->mu);

  GRPC_RESOLVER_UNREF(&r->base, "zookeeper-resolving");
}

static void zookeeper_dns_resolved(void *arg, grpc_resolved_addresses *addresses) {
  size_t i;
  zookeeper_resolver *r = arg;
  r->resolved_num++;
  r->resolved_addrs->addrs = gpr_realloc(r->resolved_addrs->addrs, 
                            sizeof(grpc_resolved_address) * (r->resolved_addrs->naddrs + addresses->naddrs));
  for (i = 0; i < addresses->naddrs; i++) {
    memcpy(r->resolved_addrs->addrs[i + r->resolved_addrs->naddrs].addr, 
          addresses->addrs[i].addr, addresses->addrs[i].len);
    r->resolved_addrs->addrs[i + r->resolved_addrs->naddrs].len = addresses->addrs[i].len;
  }

  r->resolved_addrs->naddrs += addresses->naddrs;
  grpc_resolved_addresses_destroy(addresses);

  /* Wait for all addresses to be resolved */
  if (r->resolved_num == r->resolved_total)
    zookeeper_on_resolved(r, r->resolved_addrs);
}

/* Parse json format address of a zookeeper node */
static char *zookeeper_parse_address(char *buffer, int buffer_len) {
  char *host;
  char *port;
  char *address;
  grpc_json *json;
  grpc_json *cur;  

  gpr_log(GPR_INFO, buffer);
  address = NULL;
  json = grpc_json_parse_string_with_len(buffer, buffer_len);
  if (json != NULL) {
    host = NULL;
    port = NULL;
    for (cur = json->child; cur != NULL; cur = cur->next) {
      if (!strcmp(cur->key, "host")) {
        host = (char *)cur->value;
        if (port != NULL)
          break;
      }
      else if (!strcmp(cur->key, "port")) {
        port = (char *)cur->value;
        if (host != NULL)
          break;
      }
    }
    if (host != NULL && port != NULL) {
      address = gpr_malloc(GRPC_MAX_SOCKADDR_SIZE);
      memset(address, 0, GRPC_MAX_SOCKADDR_SIZE);
      strcat(address, host);
      strcat(address, ":");
      strcat(address, port);
      gpr_log(GPR_INFO, address);
    }
    grpc_json_destroy(json);
  } 

  return address;
}

static void zookeeper_resolve_address(zookeeper_resolver *r) {
  struct String_vector children;
  int status;
  int i;

  char path[GRPC_MAX_ZOOKEEPER_BUFFER_SIZE];
  char buffer[GRPC_MAX_ZOOKEEPER_BUFFER_SIZE];
  char *address;
  int buffer_len;
  
  r->resolved_addrs = NULL;
  r->resolved_total = 0;
  r->resolved_num = 0;
  address = NULL;
  buffer_len = GRPC_MAX_ZOOKEEPER_BUFFER_SIZE;
  memset(path, 0, buffer_len);
  memset(buffer, 0, buffer_len);

  /* Get zookeeper node of given path r->name 
     If not containing address(i.e. service node), get its children */
  gpr_log(GPR_INFO, r->name);
  status = zoo_get(r->zookeeper_handle, r->name, GRPC_ZOOKEEPER_WATCH, 
                  buffer, &buffer_len, NULL);
  if (!status) {
    if (buffer_len > 0) {
      address = zookeeper_parse_address(buffer, buffer_len);
      if (address != NULL) {
        r->resolved_addrs = gpr_malloc(sizeof(grpc_resolved_addresses));
        r->resolved_addrs->addrs = NULL;
        r->resolved_addrs->naddrs = 0;
        r->resolved_total = 1;
        /* Further resolve address by DNS */
        grpc_resolve_address(address, NULL, zookeeper_dns_resolved, r);
        gpr_free(address);
        return;
      }
    }

    buffer_len = GRPC_MAX_ZOOKEEPER_BUFFER_SIZE;
    status = zoo_get_children(r->zookeeper_handle, r->name, GRPC_ZOOKEEPER_WATCH, 
                              &children);
    if (!status) {
      r->resolved_addrs = gpr_malloc(sizeof(grpc_resolved_addresses));
      r->resolved_addrs->addrs = NULL;
      r->resolved_addrs->naddrs = 0;
      r->resolved_total = children.count;

      for (i = 0; i < children.count; i++) {
        memset(path, 0, GRPC_MAX_ZOOKEEPER_BUFFER_SIZE);
        strcat(path, r->name);
        strcat(path, "/");
        strcat(path, children.data[i]);
        gpr_log(GPR_INFO, path);
        memset(buffer, 0, GRPC_MAX_ZOOKEEPER_BUFFER_SIZE);
        status = zoo_get(r->zookeeper_handle, path, GRPC_ZOOKEEPER_WATCH, 
                        buffer, &buffer_len, NULL);

        if (!status) {
          if (buffer_len > 0) {
            address = zookeeper_parse_address(buffer, buffer_len);
            if (address != NULL) {
              /* Further resolve address by DNS */
              grpc_resolve_address(address, NULL, zookeeper_dns_resolved, r); 
            }
            else {
              gpr_log(GPR_ERROR, "Cannot parse zookeeper address %s", buffer);
            }
          }
        } else {
          gpr_log(GPR_ERROR, "Cannot get zookeeper node %s", path);
        }

        if (address == NULL) {
          r->resolved_total--;
          if (r->resolved_num == r->resolved_total) {
            zookeeper_on_resolved(r, r->resolved_addrs);
          }
        }
      }
    } else {
      gpr_log(GPR_ERROR, "Cannot get children of zookeeper node %s", r->name);
    }
  } else {
    gpr_log(GPR_ERROR, "Cannot read zookeeper node %s", r->name);
  }
  
  gpr_free(address);
}

static void zookeeper_start_resolving_locked(zookeeper_resolver *r) {
  GRPC_RESOLVER_REF(&r->base, "zookeeper-resolving");
  GPR_ASSERT(!r->resolving);
  r->resolving = 1;

  zookeeper_resolve_address(r);
}

static void zookeeper_maybe_finish_next_locked(zookeeper_resolver *r) {
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

static void zookeeper_destroy(grpc_resolver *gr) {
  zookeeper_resolver *r = (zookeeper_resolver *)gr;
  gpr_mu_destroy(&r->mu);
  if (r->resolved_config) {
    grpc_client_config_unref(r->resolved_config);
  }
  grpc_subchannel_factory_unref(r->subchannel_factory);
  gpr_free(r->name);
  gpr_free(r);
}

/* Zookeeper watcher function - handle updates to any watched nodes */
static void zookeeper_watcher(zhandle_t *zookeeper_handle, int type, int state, 
                              const char* path, void* watcher_ctx) {}

static grpc_resolver *zookeeper_create(
    grpc_uri *uri,
    grpc_lb_policy *(*lb_policy_factory)(grpc_subchannel **subchannels,
                                         size_t num_subchannels),
    grpc_subchannel_factory *subchannel_factory) {
  zookeeper_resolver *r;
  const char *path = uri->path;
  if (0 == strcmp(uri->authority, "")) {
    gpr_log(GPR_ERROR, "No authority specified in zookeeper uri");
    return NULL;
  }

  r = gpr_malloc(sizeof(zookeeper_resolver));
  memset(r, 0, sizeof(*r));
  gpr_ref_init(&r->refs, 1);
  gpr_mu_init(&r->mu);
  grpc_resolver_init(&r->base, &zookeeper_resolver_vtable);
  r->name = gpr_strdup(path);
  if (r->name[strlen(r->name)-1] == '/') {
    r->name[strlen(r->name)-1] = 0;
  } 
  r->subchannel_factory = subchannel_factory;
  r->lb_policy_factory = lb_policy_factory;
  grpc_subchannel_factory_ref(subchannel_factory);

  /* Initialize zookeeper client */
  zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
  r->zookeeper_handle = zookeeper_init(uri->authority, zookeeper_watcher, 
                                      GRPC_ZOOKEEPER_TIMEOUT, 0, 0, 0);
  if (r->zookeeper_handle  == NULL) {
    gpr_log(GPR_ERROR, "Cannot connect to zookeeper server");
    return NULL;
  }

  return &r->base;
}

static void zookeeper_plugin_init() {
  grpc_register_resolver_type("zookeeper", grpc_zookeeper_resolver_factory_create());
}

void grpc_zookeeper_register() {
  grpc_register_plugin(zookeeper_plugin_init, NULL);
}

/*
 * FACTORY
 */

static void zookeeper_factory_ref(grpc_resolver_factory *factory) {}

static void zookeeper_factory_unref(grpc_resolver_factory *factory) {}

static grpc_resolver *zookeeper_factory_create_resolver(
    grpc_resolver_factory *factory, grpc_uri *uri,
    grpc_subchannel_factory *subchannel_factory) {
  return zookeeper_create(uri, grpc_create_pick_first_lb_policy,
                    subchannel_factory);
}

static const grpc_resolver_factory_vtable zookeeper_factory_vtable = {
    zookeeper_factory_ref, zookeeper_factory_unref, zookeeper_factory_create_resolver};
static grpc_resolver_factory zookeeper_resolver_factory = {&zookeeper_factory_vtable};

grpc_resolver_factory *grpc_zookeeper_resolver_factory_create() {
  return &zookeeper_resolver_factory;
}