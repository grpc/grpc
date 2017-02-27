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
#include <grpc/support/host_port.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/client_channel/lb_policy_registry.h"
#include "src/core/ext/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/support/backoff.h"
#include "src/core/lib/support/string.h"

#define GRPC_DNS_MIN_CONNECT_TIMEOUT_SECONDS 1
#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2

typedef struct {
  /** base class: must be first */
  grpc_resolver base;
  /** name to resolve */
  char *name_to_resolve;
  /** default port to use */
  char *default_port;
  /** channel args. */
  grpc_channel_args *channel_args;
  /** pollset_set to drive the name resolution process */
  grpc_pollset_set *interested_parties;

  /** are we currently resolving? */
  bool resolving;
  /** which version of the result have we published? */
  int published_version;
  /** which version of the result is current? */
  int resolved_version;
  /** pending next completion, or NULL */
  grpc_closure *next_completion;
  /** target result address for next completion */
  grpc_channel_args **target_result;
  /** current (fully resolved) result */
  grpc_channel_args *resolved_result;
  /** retry timer */
  bool have_retry_timer;
  grpc_timer retry_timer;
  grpc_closure on_retry;
  /** retry backoff state */
  gpr_backoff backoff_state;

  /** currently resolving addresses */
  grpc_resolved_addresses *addresses;
} dns_resolver;

static void dns_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *r);

static void dns_start_resolving_locked(grpc_exec_ctx *exec_ctx,
                                       dns_resolver *r);
static void dns_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                         dns_resolver *r);

static void dns_shutdown_locked(grpc_exec_ctx *exec_ctx, grpc_resolver *r);
static void dns_channel_saw_error_locked(grpc_exec_ctx *exec_ctx,
                                         grpc_resolver *r);
static void dns_next_locked(grpc_exec_ctx *exec_ctx, grpc_resolver *r,
                            grpc_channel_args **target_result,
                            grpc_closure *on_complete);

static const grpc_resolver_vtable dns_resolver_vtable = {
    dns_destroy, dns_shutdown_locked, dns_channel_saw_error_locked,
    dns_next_locked};

static void dns_shutdown_locked(grpc_exec_ctx *exec_ctx,
                                grpc_resolver *resolver) {
  dns_resolver *r = (dns_resolver *)resolver;
  if (r->have_retry_timer) {
    grpc_timer_cancel(exec_ctx, &r->retry_timer);
  }
  if (r->next_completion != NULL) {
    *r->target_result = NULL;
    grpc_closure_sched(exec_ctx, r->next_completion,
                       GRPC_ERROR_CREATE("Resolver Shutdown"));
    r->next_completion = NULL;
  }
}

static void dns_channel_saw_error_locked(grpc_exec_ctx *exec_ctx,
                                         grpc_resolver *resolver) {
  dns_resolver *r = (dns_resolver *)resolver;
  if (!r->resolving) {
    gpr_backoff_reset(&r->backoff_state);
    dns_start_resolving_locked(exec_ctx, r);
  }
}

static void dns_next_locked(grpc_exec_ctx *exec_ctx, grpc_resolver *resolver,
                            grpc_channel_args **target_result,
                            grpc_closure *on_complete) {
  dns_resolver *r = (dns_resolver *)resolver;
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_result = target_result;
  if (r->resolved_version == 0 && !r->resolving) {
    gpr_backoff_reset(&r->backoff_state);
    dns_start_resolving_locked(exec_ctx, r);
  } else {
    dns_maybe_finish_next_locked(exec_ctx, r);
  }
}

static void dns_on_retry_timer_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                      grpc_error *error) {
  dns_resolver *r = arg;

  r->have_retry_timer = false;
  if (error == GRPC_ERROR_NONE) {
    if (!r->resolving) {
      dns_start_resolving_locked(exec_ctx, r);
    }
  }

  GRPC_RESOLVER_UNREF(exec_ctx, &r->base, "retry-timer");
}

static void dns_on_resolved_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                   grpc_error *error) {
  dns_resolver *r = arg;
  grpc_channel_args *result = NULL;
  GPR_ASSERT(r->resolving);
  r->resolving = false;
  if (r->addresses != NULL) {
    grpc_lb_addresses *addresses = grpc_lb_addresses_create(
        r->addresses->naddrs, NULL /* user_data_vtable */);
    for (size_t i = 0; i < r->addresses->naddrs; ++i) {
      grpc_lb_addresses_set_address(
          addresses, i, &r->addresses->addrs[i].addr,
          r->addresses->addrs[i].len, false /* is_balancer */,
          NULL /* balancer_name */, NULL /* user_data */);
    }
    grpc_arg new_arg = grpc_lb_addresses_create_channel_arg(addresses);
    result = grpc_channel_args_copy_and_add(r->channel_args, &new_arg, 1);
    grpc_resolved_addresses_destroy(r->addresses);
    grpc_lb_addresses_destroy(exec_ctx, addresses);
  } else {
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    gpr_timespec next_try = gpr_backoff_step(&r->backoff_state, now);
    gpr_timespec timeout = gpr_time_sub(next_try, now);
    gpr_log(GPR_INFO, "dns resolution failed (will retry): %s",
            grpc_error_string(error));
    GPR_ASSERT(!r->have_retry_timer);
    r->have_retry_timer = true;
    GRPC_RESOLVER_REF(&r->base, "retry-timer");
    if (gpr_time_cmp(timeout, gpr_time_0(timeout.clock_type)) > 0) {
      gpr_log(GPR_DEBUG, "retrying in %" PRId64 ".%09d seconds", timeout.tv_sec,
              timeout.tv_nsec);
    } else {
      gpr_log(GPR_DEBUG, "retrying immediately");
    }
    grpc_closure_init(&r->on_retry, dns_on_retry_timer_locked, r,
                      grpc_combiner_scheduler(r->base.combiner, false));
    grpc_timer_init(exec_ctx, &r->retry_timer, next_try, &r->on_retry, now);
  }
  if (r->resolved_result != NULL) {
    grpc_channel_args_destroy(exec_ctx, r->resolved_result);
  }
  r->resolved_result = result;
  r->resolved_version++;
  dns_maybe_finish_next_locked(exec_ctx, r);

  GRPC_RESOLVER_UNREF(exec_ctx, &r->base, "dns-resolving");
}

static void dns_start_resolving_locked(grpc_exec_ctx *exec_ctx,
                                       dns_resolver *r) {
  GRPC_RESOLVER_REF(&r->base, "dns-resolving");
  GPR_ASSERT(!r->resolving);
  r->resolving = true;
  r->addresses = NULL;
  grpc_resolve_address(
      exec_ctx, r->name_to_resolve, r->default_port, r->interested_parties,
      grpc_closure_create(dns_on_resolved_locked, r,
                          grpc_combiner_scheduler(r->base.combiner, false)),
      &r->addresses);
}

static void dns_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                         dns_resolver *r) {
  if (r->next_completion != NULL &&
      r->resolved_version != r->published_version) {
    *r->target_result = r->resolved_result == NULL
                            ? NULL
                            : grpc_channel_args_copy(r->resolved_result);
    grpc_closure_sched(exec_ctx, r->next_completion, GRPC_ERROR_NONE);
    r->next_completion = NULL;
    r->published_version = r->resolved_version;
  }
}

static void dns_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *gr) {
  dns_resolver *r = (dns_resolver *)gr;
  if (r->resolved_result != NULL) {
    grpc_channel_args_destroy(exec_ctx, r->resolved_result);
  }
  grpc_pollset_set_destroy(exec_ctx, r->interested_parties);
  gpr_free(r->name_to_resolve);
  gpr_free(r->default_port);
  grpc_channel_args_destroy(exec_ctx, r->channel_args);
  gpr_free(r);
}

static grpc_resolver *dns_create(grpc_exec_ctx *exec_ctx,
                                 grpc_resolver_args *args,
                                 const char *default_port) {
  if (0 != strcmp(args->uri->authority, "")) {
    gpr_log(GPR_ERROR, "authority based dns uri's not supported");
    return NULL;
  }
  // Get name from args.
  char *path = args->uri->path;
  if (path[0] == '/') ++path;
  // Create resolver.
  dns_resolver *r = gpr_zalloc(sizeof(dns_resolver));
  grpc_resolver_init(&r->base, &dns_resolver_vtable, args->combiner);
  r->name_to_resolve = gpr_strdup(path);
  r->default_port = gpr_strdup(default_port);
  r->channel_args = grpc_channel_args_copy(args->args);
  r->interested_parties = grpc_pollset_set_create();
  if (args->pollset_set != NULL) {
    grpc_pollset_set_add_pollset_set(exec_ctx, r->interested_parties,
                                     args->pollset_set);
  }
  gpr_backoff_init(&r->backoff_state, GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS,
                   GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER,
                   GRPC_DNS_RECONNECT_JITTER,
                   GRPC_DNS_MIN_CONNECT_TIMEOUT_SECONDS * 1000,
                   GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000);
  return &r->base;
}

/*
 * FACTORY
 */

static void dns_factory_ref(grpc_resolver_factory *factory) {}

static void dns_factory_unref(grpc_resolver_factory *factory) {}

static grpc_resolver *dns_factory_create_resolver(
    grpc_exec_ctx *exec_ctx, grpc_resolver_factory *factory,
    grpc_resolver_args *args) {
  return dns_create(exec_ctx, args, "https");
}

static char *dns_factory_get_default_host_name(grpc_resolver_factory *factory,
                                               grpc_uri *uri) {
  const char *path = uri->path;
  if (path[0] == '/') ++path;
  return gpr_strdup(path);
}

static const grpc_resolver_factory_vtable dns_factory_vtable = {
    dns_factory_ref, dns_factory_unref, dns_factory_create_resolver,
    dns_factory_get_default_host_name, "dns"};
static grpc_resolver_factory dns_resolver_factory = {&dns_factory_vtable};

static grpc_resolver_factory *dns_resolver_factory_create() {
  return &dns_resolver_factory;
}

void grpc_resolver_dns_native_init(void) {
  grpc_register_resolver_type(dns_resolver_factory_create());
}

void grpc_resolver_dns_native_shutdown(void) {}
