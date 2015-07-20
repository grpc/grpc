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
#include <sys/un.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/string_util.h>

#include "src/core/client_config/lb_policies/pick_first.h"
#include "src/core/iomgr/resolve_address.h"
#include "src/core/support/string.h"

typedef struct {
  /** base class: must be first */
  grpc_resolver base;
  /** refcount */
  gpr_refcount refs;
  /** subchannel factory */
  grpc_subchannel_factory *subchannel_factory;
  /** load balancing policy factory */
  grpc_lb_policy *(*lb_policy_factory)(grpc_subchannel **subchannels,
                                       size_t num_subchannels);

  /** the address that we've 'resolved' */
  struct sockaddr_storage addr;
  int addr_len;

  /** mutex guarding the rest of the state */
  gpr_mu mu;
  /** have we published? */
  int published;
  /** pending next completion, or NULL */
  grpc_iomgr_closure *next_completion;
  /** target config address for next completion */
  grpc_client_config **target_config;
} sockaddr_resolver;

static void sockaddr_destroy(grpc_resolver *r);

static void sockaddr_maybe_finish_next_locked(sockaddr_resolver *r);

static void sockaddr_shutdown(grpc_resolver *r);
static void sockaddr_channel_saw_error(grpc_resolver *r,
                                       struct sockaddr *failing_address,
                                       int failing_address_len);
static void sockaddr_next(grpc_resolver *r, grpc_client_config **target_config,
                          grpc_iomgr_closure *on_complete);

static const grpc_resolver_vtable sockaddr_resolver_vtable = {
    sockaddr_destroy, sockaddr_shutdown, sockaddr_channel_saw_error,
    sockaddr_next};

static void sockaddr_shutdown(grpc_resolver *resolver) {
  sockaddr_resolver *r = (sockaddr_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  if (r->next_completion != NULL) {
    *r->target_config = NULL;
    /* TODO(ctiller): add delayed callback */
    grpc_iomgr_add_callback(r->next_completion);
    r->next_completion = NULL;
  }
  gpr_mu_unlock(&r->mu);
}

static void sockaddr_channel_saw_error(grpc_resolver *resolver,
                                       struct sockaddr *sa, int len) {}

static void sockaddr_next(grpc_resolver *resolver,
                          grpc_client_config **target_config,
                          grpc_iomgr_closure *on_complete) {
  sockaddr_resolver *r = (sockaddr_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_config = target_config;
  sockaddr_maybe_finish_next_locked(r);
  gpr_mu_unlock(&r->mu);
}

static void sockaddr_maybe_finish_next_locked(sockaddr_resolver *r) {
  grpc_client_config *cfg;
  grpc_lb_policy *lb_policy;
  grpc_subchannel *subchannel;
  grpc_subchannel_args args;

  if (r->next_completion != NULL && !r->published) {
    cfg = grpc_client_config_create();
    memset(&args, 0, sizeof(args));
    args.addr = (struct sockaddr *)&r->addr;
    args.addr_len = r->addr_len;
    subchannel =
        grpc_subchannel_factory_create_subchannel(r->subchannel_factory, &args);
    lb_policy = r->lb_policy_factory(&subchannel, 1);
    grpc_client_config_set_lb_policy(cfg, lb_policy);
    GRPC_LB_POLICY_UNREF(lb_policy, "unix");
    r->published = 1;
    *r->target_config = cfg;
    grpc_iomgr_add_callback(r->next_completion);
    r->next_completion = NULL;
  }
}

static void sockaddr_destroy(grpc_resolver *gr) {
  sockaddr_resolver *r = (sockaddr_resolver *)gr;
  gpr_mu_destroy(&r->mu);
  grpc_subchannel_factory_unref(r->subchannel_factory);
  gpr_free(r);
}

#ifdef GPR_POSIX_SOCKET
static int parse_unix(grpc_uri *uri, struct sockaddr_storage *addr, int *len) {
  struct sockaddr_un *un = (struct sockaddr_un *)addr;

  un->sun_family = AF_UNIX;
  strcpy(un->sun_path, uri->path);
  *len = strlen(un->sun_path) + sizeof(un->sun_family) + 1;

  return 1;
}
#endif

static int parse_ipv4(grpc_uri *uri, struct sockaddr_storage *addr, int *len) {
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
  if (inet_aton(host, &in->sin_addr) == 0) {
    gpr_log(GPR_ERROR, "invalid ipv4 address: '%s'", host);
    goto done;
  }

  if (port != NULL) {
    if (sscanf(port, "%d", &port_num) != 1 || port_num < 0 ||
        port_num > 65535) {
      gpr_log(GPR_ERROR, "invalid ipv4 port: '%s'", port);
      goto done;
    }
    in->sin_port = htons(port_num);
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

static int parse_ipv6(grpc_uri *uri, struct sockaddr_storage *addr, int *len) {
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
    in6->sin6_port = htons(port_num);
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

static grpc_resolver *sockaddr_create(
    grpc_uri *uri,
    grpc_lb_policy *(*lb_policy_factory)(grpc_subchannel **subchannels,
                                         size_t num_subchannels),
    grpc_subchannel_factory *subchannel_factory,
    int parse(grpc_uri *uri, struct sockaddr_storage *dst, int *len)) {
  sockaddr_resolver *r;

  if (0 != strcmp(uri->authority, "")) {
    gpr_log(GPR_ERROR, "authority based uri's not supported");
    return NULL;
  }

  r = gpr_malloc(sizeof(sockaddr_resolver));
  memset(r, 0, sizeof(*r));
  if (!parse(uri, &r->addr, &r->addr_len)) {
    gpr_free(r);
    return NULL;
  }

  gpr_ref_init(&r->refs, 1);
  gpr_mu_init(&r->mu);
  grpc_resolver_init(&r->base, &sockaddr_resolver_vtable);
  r->subchannel_factory = subchannel_factory;
  r->lb_policy_factory = lb_policy_factory;

  grpc_subchannel_factory_ref(subchannel_factory);
  return &r->base;
}

/*
 * FACTORY
 */

static void sockaddr_factory_ref(grpc_resolver_factory *factory) {}

static void sockaddr_factory_unref(grpc_resolver_factory *factory) {}

#define DECL_FACTORY(name)                                            \
  static grpc_resolver *name##_factory_create_resolver(               \
      grpc_resolver_factory *factory, grpc_uri *uri,                  \
      grpc_subchannel_factory *subchannel_factory) {                  \
    return sockaddr_create(uri, grpc_create_pick_first_lb_policy,     \
                           subchannel_factory, parse_##name);         \
  }                                                                   \
  static const grpc_resolver_factory_vtable name##_factory_vtable = { \
      sockaddr_factory_ref, sockaddr_factory_unref,                   \
      name##_factory_create_resolver};                                \
  static grpc_resolver_factory name##_resolver_factory = {            \
      &name##_factory_vtable};                                        \
  grpc_resolver_factory *grpc_##name##_resolver_factory_create() {    \
    return &name##_resolver_factory;                                  \
  }

#ifdef GPR_POSIX_SOCKET
DECL_FACTORY(unix)
#endif
DECL_FACTORY(ipv4)
DECL_FACTORY(ipv6)
