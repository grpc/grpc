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

#include <grpc/support/port_platform.h>

#include "src/core/client_config/resolvers/sockaddr_resolver.h"

#include <stdio.h>
#include <string.h>
#ifdef GPR_POSIX_SOCKET
#include <sys/un.h>
#endif

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/string_util.h>

#include "src/core/client_config/lb_policy_registry.h"
#include "src/core/iomgr/resolve_address.h"
#include "src/core/support/string.h"

typedef struct {
  /** base class: must be first */
  grpc_resolver base;
  /** refcount */
  gpr_refcount refs;
  /** subchannel factory */
  grpc_subchannel_factory *subchannel_factory;
  /** load balancing policy name */
  char *lb_policy_name;

  /** the addresses that we've 'resolved' */
  struct sockaddr_storage *addrs;
  /** the corresponding length of the addresses */
  size_t *addrs_len;
  /** how many elements in \a addrs */
  size_t num_addrs;

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
                                       grpc_resolver *r,
                                       struct sockaddr *failing_address,
                                       int failing_address_len);
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
    grpc_exec_ctx_enqueue(exec_ctx, r->next_completion, 1);
    r->next_completion = NULL;
  }
  gpr_mu_unlock(&r->mu);
}

static void sockaddr_channel_saw_error(grpc_exec_ctx *exec_ctx,
                                       grpc_resolver *resolver,
                                       struct sockaddr *sa, int len) {}

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
  grpc_client_config *cfg;
  grpc_lb_policy *lb_policy;
  grpc_lb_policy_args lb_policy_args;
  grpc_subchannel **subchannels;
  grpc_subchannel_args args;

  if (r->next_completion != NULL && !r->published) {
    size_t i;
    cfg = grpc_client_config_create();
    subchannels = gpr_malloc(sizeof(grpc_subchannel *) * r->num_addrs);
    for (i = 0; i < r->num_addrs; i++) {
      memset(&args, 0, sizeof(args));
      args.addr = (struct sockaddr *)&r->addrs[i];
      args.addr_len = r->addrs_len[i];
      subchannels[i] = grpc_subchannel_factory_create_subchannel(
          exec_ctx, r->subchannel_factory, &args);
    }
    memset(&lb_policy_args, 0, sizeof(lb_policy_args));
    lb_policy_args.subchannels = subchannels;
    lb_policy_args.num_subchannels = r->num_addrs;
    lb_policy = grpc_lb_policy_create(r->lb_policy_name, &lb_policy_args);
    gpr_free(subchannels);
    grpc_client_config_set_lb_policy(cfg, lb_policy);
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "sockaddr");
    r->published = 1;
    *r->target_config = cfg;
    grpc_exec_ctx_enqueue(exec_ctx, r->next_completion, 1);
    r->next_completion = NULL;
  }
}

static void sockaddr_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *gr) {
  sockaddr_resolver *r = (sockaddr_resolver *)gr;
  gpr_mu_destroy(&r->mu);
  grpc_subchannel_factory_unref(exec_ctx, r->subchannel_factory);
  gpr_free(r->addrs);
  gpr_free(r->addrs_len);
  gpr_free(r->lb_policy_name);
  gpr_free(r);
}

#ifdef GPR_POSIX_SOCKET
static int parse_unix(grpc_uri *uri, struct sockaddr_storage *addr,
                      size_t *len) {
  struct sockaddr_un *un = (struct sockaddr_un *)addr;

  un->sun_family = AF_UNIX;
  strcpy(un->sun_path, uri->path);
  *len = strlen(un->sun_path) + sizeof(un->sun_family) + 1;

  return 1;
}

static char *unix_get_default_authority(grpc_resolver_factory *factory,
                                        grpc_uri *uri) {
  return gpr_strdup("localhost");
}
#endif

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

static int parse_ipv4(grpc_uri *uri, struct sockaddr_storage *addr,
                      size_t *len) {
  const char *host_port = uri->path;
  char *host;
  char *port;
  int port_num;
  int result = 0;
  struct sockaddr_in *in = (struct sockaddr_in *)addr;

  if (*host_port == '/') ++host_port;
  if (!gpr_split_host_port(host_port, &host, &port)) {
    return 0;
  }

  memset(in, 0, sizeof(*in));
  *len = sizeof(*in);
  in->sin_family = AF_INET;
  if (inet_pton(AF_INET, host, &in->sin_addr) == 0) {
    gpr_log(GPR_ERROR, "invalid ipv4 address: '%s'", host);
    goto done;
  }

  if (port != NULL) {
    if (sscanf(port, "%d", &port_num) != 1 || port_num < 0 ||
        port_num > 65535) {
      gpr_log(GPR_ERROR, "invalid ipv4 port: '%s'", port);
      goto done;
    }
    in->sin_port = htons((gpr_uint16)port_num);
  } else {
    gpr_log(GPR_ERROR, "no port given for ipv4 scheme");
    goto done;
  }

  result = 1;
done:
  gpr_free(host);
  gpr_free(port);
  return result;
}

static int parse_ipv6(grpc_uri *uri, struct sockaddr_storage *addr,
                      size_t *len) {
  const char *host_port = uri->path;
  char *host;
  char *port;
  int port_num;
  int result = 0;
  struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)addr;

  if (*host_port == '/') ++host_port;
  if (!gpr_split_host_port(host_port, &host, &port)) {
    return 0;
  }

  memset(in6, 0, sizeof(*in6));
  *len = sizeof(*in6);
  in6->sin6_family = AF_INET6;
  if (inet_pton(AF_INET6, host, &in6->sin6_addr) == 0) {
    gpr_log(GPR_ERROR, "invalid ipv6 address: '%s'", host);
    goto done;
  }

  if (port != NULL) {
    if (sscanf(port, "%d", &port_num) != 1 || port_num < 0 ||
        port_num > 65535) {
      gpr_log(GPR_ERROR, "invalid ipv6 port: '%s'", port);
      goto done;
    }
    in6->sin6_port = htons((gpr_uint16)port_num);
  } else {
    gpr_log(GPR_ERROR, "no port given for ipv6 scheme");
    goto done;
  }

  result = 1;
done:
  gpr_free(host);
  gpr_free(port);
  return result;
}

static void do_nothing(void *ignored) {}

static grpc_resolver *sockaddr_create(
    grpc_resolver_args *args, const char *default_lb_policy_name,
    int parse(grpc_uri *uri, struct sockaddr_storage *dst, size_t *len)) {
  size_t i;
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

  r->lb_policy_name = NULL;
  if (0 != strcmp(args->uri->query, "")) {
    gpr_slice query_slice;
    gpr_slice_buffer query_parts;

    query_slice =
        gpr_slice_new(args->uri->query, strlen(args->uri->query), do_nothing);
    gpr_slice_buffer_init(&query_parts);
    gpr_slice_split(query_slice, "=", &query_parts);
    GPR_ASSERT(query_parts.count == 2);
    if (0 == gpr_slice_str_cmp(query_parts.slices[0], "lb_policy")) {
      r->lb_policy_name = gpr_dump_slice(query_parts.slices[1], GPR_DUMP_ASCII);
    }
    gpr_slice_buffer_destroy(&query_parts);
    gpr_slice_unref(query_slice);
  }
  if (r->lb_policy_name == NULL) {
    r->lb_policy_name = gpr_strdup(default_lb_policy_name);
  }

  path_slice =
      gpr_slice_new(args->uri->path, strlen(args->uri->path), do_nothing);
  gpr_slice_buffer_init(&path_parts);

  gpr_slice_split(path_slice, ",", &path_parts);
  r->num_addrs = path_parts.count;
  r->addrs = gpr_malloc(sizeof(struct sockaddr_storage) * r->num_addrs);
  r->addrs_len = gpr_malloc(sizeof(*r->addrs_len) * r->num_addrs);

  for (i = 0; i < r->num_addrs; i++) {
    grpc_uri ith_uri = *args->uri;
    char *part_str = gpr_dump_slice(path_parts.slices[i], GPR_DUMP_ASCII);
    ith_uri.path = part_str;
    if (!parse(&ith_uri, &r->addrs[i], &r->addrs_len[i])) {
      errors_found = 1; /* GPR_TRUE */
    }
    gpr_free(part_str);
    if (errors_found) break;
  }

  gpr_slice_buffer_destroy(&path_parts);
  gpr_slice_unref(path_slice);
  if (errors_found) {
    gpr_free(r);
    return NULL;
  }

  gpr_ref_init(&r->refs, 1);
  gpr_mu_init(&r->mu);
  grpc_resolver_init(&r->base, &sockaddr_resolver_vtable);
  r->subchannel_factory = args->subchannel_factory;
  grpc_subchannel_factory_ref(r->subchannel_factory);

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
      &name##_factory_vtable};                                              \
  grpc_resolver_factory *grpc_##name##_resolver_factory_create() {          \
    return &name##_resolver_factory;                                        \
  }

#ifdef GPR_POSIX_SOCKET
DECL_FACTORY(unix)
#endif
DECL_FACTORY(ipv4) DECL_FACTORY(ipv6)
