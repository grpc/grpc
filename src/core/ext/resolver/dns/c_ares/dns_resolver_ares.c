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
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/client_config/http_connect_handshaker.h"
#include "src/core/ext/client_config/lb_policy_registry.h"
#include "src/core/ext/client_config/resolver_registry.h"
#include "src/core/ext/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/support/backoff.h"
#include "src/core/lib/support/string.h"

#define BACKOFF_MULTIPLIER 1.6
#define BACKOFF_JITTER 0.2
#define BACKOFF_MIN_SECONDS 1
#define BACKOFF_MAX_SECONDS 120

typedef struct {
  /** base class: must be first */
  grpc_resolver base;
  /** target name */
  char *target_name;
  /** name to resolve (usually the same as target_name) */
  char *name_to_resolve;
  /** default port to use */
  char *default_port;

  /** mutex guarding the rest of the state */
  gpr_mu mu;
  /** are we currently resolving? */
  bool resolving;
  /** which version of the result have we published? */
  int published_version;
  /** which version of the result is current? */
  int resolved_version;
  /** pending next completion, or NULL */
  grpc_closure *next_completion;
  /** target result address for next completion */
  grpc_resolver_result **target_result;
  /** current (fully resolved) result */
  grpc_resolver_result *resolved_result;
  /** retry timer */
  bool have_retry_timer;
  grpc_timer retry_timer;
  /** retry backoff state */
  gpr_backoff backoff_state;

  /** currently resolving addresses */
  grpc_resolved_addresses *addresses;

  grpc_polling_entity *pollent;
} dns_resolver;

static void dns_ares_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *r);

static void dns_ares_start_resolving_locked(grpc_exec_ctx *exec_ctx,
                                            dns_resolver *r);
static void dns_ares_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                              dns_resolver *r);

static void dns_ares_shutdown(grpc_exec_ctx *exec_ctx, grpc_resolver *r);
static void dns_ares_channel_saw_error(grpc_exec_ctx *exec_ctx,
                                       grpc_resolver *r);
static void dns_ares_next(grpc_exec_ctx *exec_ctx, grpc_resolver *r,
                          grpc_polling_entity *pollent,
                          grpc_resolver_result **target_result,
                          grpc_closure *on_complete);

static const grpc_resolver_vtable dns_ares_resolver_vtable = {
    dns_ares_destroy, dns_ares_shutdown, dns_ares_channel_saw_error,
    dns_ares_next};

static void dns_ares_shutdown(grpc_exec_ctx *exec_ctx,
                              grpc_resolver *resolver) {
  dns_resolver *r = (dns_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  if (r->have_retry_timer) {
    grpc_timer_cancel(exec_ctx, &r->retry_timer);
  }
  if (r->next_completion != NULL) {
    *r->target_result = NULL;
    grpc_exec_ctx_sched(exec_ctx, r->next_completion,
                        GRPC_ERROR_CREATE("Resolver Shutdown"), NULL);
    r->next_completion = NULL;
  }
  gpr_mu_unlock(&r->mu);
}

static void dns_ares_channel_saw_error(grpc_exec_ctx *exec_ctx,
                                       grpc_resolver *resolver) {
  dns_resolver *r = (dns_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  if (!r->resolving) {
    gpr_backoff_reset(&r->backoff_state);
    dns_ares_start_resolving_locked(exec_ctx, r);
  }
  gpr_mu_unlock(&r->mu);
}

static void dns_ares_on_retry_timer(grpc_exec_ctx *exec_ctx, void *arg,
                                    grpc_error *error) {
  dns_resolver *r = arg;
  gpr_mu_lock(&r->mu);
  r->have_retry_timer = false;
  if (error == GRPC_ERROR_NONE) {
    if (!r->resolving) {
      dns_ares_start_resolving_locked(exec_ctx, r);
    }
  }
  gpr_mu_unlock(&r->mu);
  GRPC_RESOLVER_UNREF(exec_ctx, &r->base, "retry-timer");
}

static void dns_ares_on_resolved(grpc_exec_ctx *exec_ctx, void *arg,
                                 grpc_error *error) {
  dns_resolver *r = arg;
  grpc_resolver_result *result = NULL;
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(r->resolving);
  r->resolving = false;
  if (r->addresses != NULL) {
    grpc_lb_addresses *addresses =
        grpc_lb_addresses_create(r->addresses->naddrs);
    for (size_t i = 0; i < r->addresses->naddrs; ++i) {
      grpc_lb_addresses_set_address(
          addresses, i, &r->addresses->addrs[i].addr,
          r->addresses->addrs[i].len, false /* is_balancer */,
          NULL /* balancer_name */, NULL /* user_data */);
    }
    grpc_resolved_addresses_destroy(r->addresses);
    result = grpc_resolver_result_create(r->target_name, addresses,
                                         NULL /* lb_policy_name */, NULL);
    if (r->pollent) {
      grpc_polling_entity_del_from_pollset_set(exec_ctx, r->pollent,
                                               r->base.pollset_set);
      r->pollent = NULL;
    }
  } else {
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    gpr_timespec next_try = gpr_backoff_step(&r->backoff_state, now);
    gpr_timespec timeout = gpr_time_sub(next_try, now);
    const char *msg = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "dns resolution failed: %s", msg);
    grpc_error_free_string(msg);
    GPR_ASSERT(!r->have_retry_timer);
    r->have_retry_timer = true;
    GRPC_RESOLVER_REF(&r->base, "retry-timer");
    if (gpr_time_cmp(timeout, gpr_time_0(timeout.clock_type)) <= 0) {
      gpr_log(GPR_DEBUG, "retrying in %" PRId64 ".%09d seconds", timeout.tv_sec,
              timeout.tv_nsec);
    } else {
      gpr_log(GPR_DEBUG, "retrying immediately");
    }
    grpc_timer_init(exec_ctx, &r->retry_timer, next_try,
                    dns_ares_on_retry_timer, r, now);
  }
  if (r->resolved_result) {
    grpc_resolver_result_unref(exec_ctx, r->resolved_result);
  }
  r->resolved_result = result;
  r->resolved_version++;
  dns_ares_maybe_finish_next_locked(exec_ctx, r);
  gpr_mu_unlock(&r->mu);
  GRPC_RESOLVER_UNREF(exec_ctx, &r->base, "dns-resolving");
}

static void dns_ares_next(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver,
                          grpc_polling_entity *pollent,
                          grpc_resolver_result **target_result,
                          grpc_closure *on_complete) {
  dns_resolver *r = (dns_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_result = target_result;
  if (r->resolved_version == 0 && !r->resolving) {
    gpr_backoff_reset(&r->backoff_state);
    GRPC_RESOLVER_REF(&r->base, "dns-resolving");
    GPR_ASSERT(!r->resolving);
    r->resolving = true;
    r->addresses = NULL;
    r->pollent = NULL;
    if (grpc_ares_need_poll_entity() && pollent) {
      r->pollent = pollent;
      grpc_polling_entity_add_to_pollset_set(exec_ctx, pollent,
                                             r->base.pollset_set);
    } else {
      gpr_log(GPR_DEBUG, "dns_ares_next is called without giving a pollent");
    }
    grpc_resolve_address_ares(
        exec_ctx, r->name_to_resolve, r->default_port, r->base.pollset_set,
        grpc_closure_create(dns_ares_on_resolved, r), &r->addresses);
  } else {
    dns_ares_maybe_finish_next_locked(exec_ctx, r);
  }
  gpr_mu_unlock(&r->mu);
}

static void dns_ares_start_resolving_locked(grpc_exec_ctx *exec_ctx,
                                            dns_resolver *r) {
  GRPC_RESOLVER_REF(&r->base, "dns-resolving");
  GPR_ASSERT(!r->resolving);
  r->resolving = true;
  r->addresses = NULL;
  grpc_resolve_address_ares(
      exec_ctx, r->name_to_resolve, r->default_port, r->base.pollset_set,
      grpc_closure_create(dns_ares_on_resolved, r), &r->addresses);
}

static void dns_ares_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                              dns_resolver *r) {
  if (r->next_completion != NULL &&
      r->resolved_version != r->published_version) {
    *r->target_result = r->resolved_result;
    if (r->resolved_result) {
      grpc_resolver_result_ref(r->resolved_result);
    }
    grpc_exec_ctx_sched(exec_ctx, r->next_completion, GRPC_ERROR_NONE, NULL);
    r->next_completion = NULL;
    r->published_version = r->resolved_version;
  }
}

static void dns_ares_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *gr) {
  dns_resolver *r = (dns_resolver *)gr;
  gpr_mu_destroy(&r->mu);
  grpc_ares_cleanup();
  if (r->resolved_result) {
    grpc_resolver_result_unref(exec_ctx, r->resolved_result);
  }
  gpr_free(r->target_name);
  gpr_free(r->name_to_resolve);
  gpr_free(r->default_port);
  gpr_free(r);
}

static grpc_resolver *dns_ares_create(grpc_resolver_args *args,
                                      const char *default_port) {
  dns_resolver *r;
  grpc_error *error = GRPC_ERROR_NONE;
  char *proxy_name;
  // Get name from args.
  const char *path = args->uri->path;

  if (0 != strcmp(args->uri->authority, "")) {
    gpr_log(GPR_ERROR, "authority based dns uri's not supported");
    return NULL;
  }

  error = grpc_ares_init();
  if (error != GRPC_ERROR_NONE) {
    GRPC_LOG_IF_ERROR("ares_library_init() failed", error);
    return NULL;
  }

  if (path[0] == '/') ++path;

  // Get proxy name, if any.
  proxy_name = grpc_get_http_proxy_server();
  // Create resolver.
  r = gpr_malloc(sizeof(dns_resolver));
  memset(r, 0, sizeof(*r));
  gpr_mu_init(&r->mu);
  grpc_resolver_init(&r->base, &dns_ares_resolver_vtable);
  r->target_name = gpr_strdup(path);
  r->name_to_resolve = proxy_name == NULL ? gpr_strdup(path) : proxy_name;
  r->default_port = gpr_strdup(default_port);
  gpr_backoff_init(&r->backoff_state, BACKOFF_MULTIPLIER, BACKOFF_JITTER,
                   BACKOFF_MIN_SECONDS * 1000, BACKOFF_MAX_SECONDS * 1000);
  return &r->base;
}

/*
 * FACTORY
 */

static void dns_ares_factory_ref(grpc_resolver_factory *factory) {}

static void dns_ares_factory_unref(grpc_resolver_factory *factory) {}

static grpc_resolver *dns_factory_create_resolver(
    grpc_resolver_factory *factory, grpc_resolver_args *args) {
  return dns_ares_create(args, "https");
}

static char *dns_ares_factory_get_default_host_name(
    grpc_resolver_factory *factory, grpc_uri *uri) {
  const char *path = uri->path;
  if (path[0] == '/') ++path;
  return gpr_strdup(path);
}

static const grpc_resolver_factory_vtable dns_ares_factory_vtable = {
    dns_ares_factory_ref, dns_ares_factory_unref, dns_factory_create_resolver,
    dns_ares_factory_get_default_host_name, "dns"};
static grpc_resolver_factory dns_resolver_factory = {&dns_ares_factory_vtable};

static grpc_resolver_factory *dns_ares_resolver_factory_create() {
  return &dns_resolver_factory;
}

void grpc_resolver_dns_ares_init(void) {
  grpc_register_resolver_type(dns_ares_resolver_factory_create());
}

void grpc_resolver_dns_ares_shutdown(void) {}
