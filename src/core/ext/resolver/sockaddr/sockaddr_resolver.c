/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/client_config/lb_policy_registry.h"
#include "src/core/ext/client_config/parse_address.h"
#include "src/core/ext/client_config/resolver_registry.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/support/string.h"

typedef struct {
  /** base class: must be first */
  grpc_resolver base;
  /** refcount */
  gpr_refcount refs;
  /** subchannel factory */
  grpc_client_channel_factory *client_channel_factory;
  /** load balancing policy name */
  char *lb_policy_name;

  /** the addresses that we've 'resolved' */
  grpc_resolved_addresses *addresses;

  /** mutex guarding the rest of the state */
  gpr_mu mu;
  /** have we published? */
  int published;
  /** pending next completion, or NULL */
  grpc_closure *next_completion;
  /** target config address for next completion */
  grpc_client_config **target_config;
} sockaddr_resolver;

static void sockaddr_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *r);

static void sockaddr_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                              sockaddr_resolver *r);

static void sockaddr_shutdown(grpc_exec_ctx *exec_ctx, grpc_resolver *r);
static void sockaddr_channel_saw_error(grpc_exec_ctx *exec_ctx,
                                       grpc_resolver *r);
static void sockaddr_next(grpc_exec_ctx *exec_ctx, grpc_resolver *r,
                          grpc_client_config **target_config,
                          grpc_closure *on_complete);

static const grpc_resolver_vtable sockaddr_resolver_vtable = {
    sockaddr_destroy, sockaddr_shutdown, sockaddr_channel_saw_error,
    sockaddr_next};

static void sockaddr_shutdown(grpc_exec_ctx *exec_ctx,
                              grpc_resolver *resolver) {
  sockaddr_resolver *r = (sockaddr_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  if (r->next_completion != NULL) {
    *r->target_config = NULL;
    grpc_exec_ctx_enqueue(exec_ctx, r->next_completion, true, NULL);
    r->next_completion = NULL;
  }
  gpr_mu_unlock(&r->mu);
}

static void sockaddr_channel_saw_error(grpc_exec_ctx *exec_ctx,
                                       grpc_resolver *resolver) {
  sockaddr_resolver *r = (sockaddr_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  r->published = 0;
  sockaddr_maybe_finish_next_locked(exec_ctx, r);
  gpr_mu_unlock(&r->mu);
}

static void sockaddr_next(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver,
                          grpc_client_config **target_config,
                          grpc_closure *on_complete) {
  sockaddr_resolver *r = (sockaddr_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_config = target_config;
  sockaddr_maybe_finish_next_locked(exec_ctx, r);
  gpr_mu_unlock(&r->mu);
}

static void sockaddr_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                              sockaddr_resolver *r) {
  if (r->next_completion != NULL && !r->published) {
    grpc_client_config *cfg = grpc_client_config_create();
    grpc_lb_policy_args lb_policy_args;
    memset(&lb_policy_args, 0, sizeof(lb_policy_args));
    lb_policy_args.addresses = r->addresses;
    lb_policy_args.client_channel_factory = r->client_channel_factory;
    grpc_lb_policy *lb_policy =
        grpc_lb_policy_create(exec_ctx, r->lb_policy_name, &lb_policy_args);
    grpc_client_config_set_lb_policy(cfg, lb_policy);
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "sockaddr");
    r->published = 1;
    *r->target_config = cfg;
    grpc_exec_ctx_enqueue(exec_ctx, r->next_completion, true, NULL);
    r->next_completion = NULL;
  }
}

static void sockaddr_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *gr) {
  sockaddr_resolver *r = (sockaddr_resolver *)gr;
  gpr_mu_destroy(&r->mu);
  grpc_client_channel_factory_unref(exec_ctx, r->client_channel_factory);
  grpc_resolved_addresses_destroy(r->addresses);
  gpr_free(r->lb_policy_name);
  gpr_free(r);
}

static char *ip_get_default_authority(grpc_uri *uri) {
  const char *path = uri->path;
  if (path[0] == '/') ++path;
  return gpr_strdup(path);
}

static char *ipv4_get_default_authority(grpc_resolver_factory *factory,
                                        grpc_uri *uri) {
  return ip_get_default_authority(uri);
}

static char *ipv6_get_default_authority(grpc_resolver_factory *factory,
                                        grpc_uri *uri) {
  return ip_get_default_authority(uri);
}

#ifdef GPR_HAVE_UNIX_SOCKET
char *unix_get_default_authority(grpc_resolver_factory *factory,
                                 grpc_uri *uri) {
  return gpr_strdup("localhost");
}
#endif

static void do_nothing(void *ignored) {}

static grpc_resolver *sockaddr_create(
    grpc_resolver_args *args, const char *default_lb_policy_name,
    int parse(grpc_uri *uri, struct sockaddr_storage *dst, size_t *len)) {
  int errors_found = 0; /* GPR_FALSE */
  sockaddr_resolver *r;
  gpr_slice path_slice;
  gpr_slice_buffer path_parts;

  if (0 != strcmp(args->uri->authority, "")) {
    gpr_log(GPR_ERROR, "authority based uri's not supported by the %s scheme",
            args->uri->scheme);
    return NULL;
  }

  r = gpr_malloc(sizeof(sockaddr_resolver));
  memset(r, 0, sizeof(*r));

  r->lb_policy_name =
      gpr_strdup(grpc_uri_get_query_arg(args->uri, "lb_policy"));
  const char *lb_enabled_qpart =
      grpc_uri_get_query_arg(args->uri, "lb_enabled");
  /* anything other than "0" is interpreted as true */
  const bool lb_enabled =
      (lb_enabled_qpart != NULL && (strcmp("0", lb_enabled_qpart) != 0));

  if (r->lb_policy_name != NULL && strcmp("grpclb", r->lb_policy_name) == 0 &&
      !lb_enabled) {
    /* we want grpclb but the "resolved" addresses aren't LB enabled. Bail
     * out, as this is meant mostly for tests. */
    gpr_log(GPR_ERROR,
            "Requested 'grpclb' LB policy but resolved addresses don't "
            "support load balancing.");
    abort();
  }

  if (r->lb_policy_name == NULL) {
    r->lb_policy_name = gpr_strdup(default_lb_policy_name);
  }

  path_slice =
      gpr_slice_new(args->uri->path, strlen(args->uri->path), do_nothing);
  gpr_slice_buffer_init(&path_parts);

  gpr_slice_split(path_slice, ",", &path_parts);
  r->addresses = gpr_malloc(sizeof(grpc_resolved_addresses));
  r->addresses->naddrs = path_parts.count;
  r->addresses->addrs =
      gpr_malloc(sizeof(grpc_resolved_address) * r->addresses->naddrs);

  for (size_t i = 0; i < r->addresses->naddrs; i++) {
    grpc_uri ith_uri = *args->uri;
    char *part_str = gpr_dump_slice(path_parts.slices[i], GPR_DUMP_ASCII);
    ith_uri.path = part_str;
    if (!parse(&ith_uri,
               (struct sockaddr_storage *)(&r->addresses->addrs[i].addr),
               &r->addresses->addrs[i].len)) {
      errors_found = 1; /* GPR_TRUE */
    }
    gpr_free(part_str);
    if (errors_found) break;
  }

  gpr_slice_buffer_destroy(&path_parts);
  gpr_slice_unref(path_slice);
  if (errors_found) {
    gpr_free(r->lb_policy_name);
    grpc_resolved_addresses_destroy(r->addresses);
    gpr_free(r);
    return NULL;
  }

  gpr_ref_init(&r->refs, 1);
  gpr_mu_init(&r->mu);
  grpc_resolver_init(&r->base, &sockaddr_resolver_vtable);
  r->client_channel_factory = args->client_channel_factory;
  grpc_client_channel_factory_ref(r->client_channel_factory);

  return &r->base;
}

/*
 * FACTORY
 */

static void sockaddr_factory_ref(grpc_resolver_factory *factory) {}

static void sockaddr_factory_unref(grpc_resolver_factory *factory) {}

#define DECL_FACTORY(name)                                                  \
  static grpc_resolver *name##_factory_create_resolver(                     \
      grpc_resolver_factory *factory, grpc_resolver_args *args) {           \
    return sockaddr_create(args, "pick_first", parse_##name);               \
  }                                                                         \
  static const grpc_resolver_factory_vtable name##_factory_vtable = {       \
      sockaddr_factory_ref, sockaddr_factory_unref,                         \
      name##_factory_create_resolver, name##_get_default_authority, #name}; \
  static grpc_resolver_factory name##_resolver_factory = {                  \
      &name##_factory_vtable}

#ifdef GPR_HAVE_UNIX_SOCKET
DECL_FACTORY(unix);
#endif
DECL_FACTORY(ipv4);
DECL_FACTORY(ipv6);

void grpc_resolver_sockaddr_init(void) {
  grpc_register_resolver_type(&ipv4_resolver_factory);
  grpc_register_resolver_type(&ipv6_resolver_factory);
#ifdef GPR_HAVE_UNIX_SOCKET
  grpc_register_resolver_type(&unix_resolver_factory);
#endif
}

void grpc_resolver_sockaddr_shutdown(void) {}
