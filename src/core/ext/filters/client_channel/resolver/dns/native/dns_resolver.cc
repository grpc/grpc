/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/support/env.h"
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
  char* name_to_resolve;
  /** default port to use */
  char* default_port;
  /** channel args. */
  grpc_channel_args* channel_args;
  /** pollset_set to drive the name resolution process */
  grpc_pollset_set* interested_parties;

  /** are we currently resolving? */
  bool resolving;
  /** which version of the result have we published? */
  int published_version;
  /** which version of the result is current? */
  int resolved_version;
  /** pending next completion, or NULL */
  grpc_closure* next_completion;
  /** target result address for next completion */
  grpc_channel_args** target_result;
  /** current (fully resolved) result */
  grpc_channel_args* resolved_result;
  /** retry timer */
  bool have_retry_timer;
  grpc_timer retry_timer;
  grpc_closure on_retry;
  /** retry backoff state */
  grpc_backoff backoff_state;

  /** currently resolving addresses */
  grpc_resolved_addresses* addresses;
} dns_resolver;

static void dns_destroy(grpc_exec_ctx* exec_ctx, grpc_resolver* r);

static void dns_start_resolving_locked(grpc_exec_ctx* exec_ctx,
                                       dns_resolver* r);
static void dns_maybe_finish_next_locked(grpc_exec_ctx* exec_ctx,
                                         dns_resolver* r);

static void dns_shutdown_locked(grpc_exec_ctx* exec_ctx, grpc_resolver* r);
static void dns_channel_saw_error_locked(grpc_exec_ctx* exec_ctx,
                                         grpc_resolver* r);
static void dns_next_locked(grpc_exec_ctx* exec_ctx, grpc_resolver* r,
                            grpc_channel_args** target_result,
                            grpc_closure* on_complete);

static const grpc_resolver_vtable dns_resolver_vtable = {
    dns_destroy, dns_shutdown_locked, dns_channel_saw_error_locked,
    dns_next_locked};

static void dns_shutdown_locked(grpc_exec_ctx* exec_ctx,
                                grpc_resolver* resolver) {
  dns_resolver* r = (dns_resolver*)resolver;
  if (r->have_retry_timer) {
    grpc_timer_cancel(exec_ctx, &r->retry_timer);
  }
  if (r->next_completion != nullptr) {
    *r->target_result = nullptr;
    GRPC_CLOSURE_SCHED(
        exec_ctx, r->next_completion,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resolver Shutdown"));
    r->next_completion = nullptr;
  }
}

static void dns_channel_saw_error_locked(grpc_exec_ctx* exec_ctx,
                                         grpc_resolver* resolver) {
  dns_resolver* r = (dns_resolver*)resolver;
  if (!r->resolving) {
    grpc_backoff_reset(&r->backoff_state);
    dns_start_resolving_locked(exec_ctx, r);
  }
}

static void dns_next_locked(grpc_exec_ctx* exec_ctx, grpc_resolver* resolver,
                            grpc_channel_args** target_result,
                            grpc_closure* on_complete) {
  dns_resolver* r = (dns_resolver*)resolver;
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_result = target_result;
  if (r->resolved_version == 0 && !r->resolving) {
    grpc_backoff_reset(&r->backoff_state);
    dns_start_resolving_locked(exec_ctx, r);
  } else {
    dns_maybe_finish_next_locked(exec_ctx, r);
  }
}

static void dns_on_retry_timer_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                      grpc_error* error) {
  dns_resolver* r = (dns_resolver*)arg;

  r->have_retry_timer = false;
  if (error == GRPC_ERROR_NONE) {
    if (!r->resolving) {
      dns_start_resolving_locked(exec_ctx, r);
    }
  }

  GRPC_RESOLVER_UNREF(exec_ctx, &r->base, "retry-timer");
}

static void dns_on_resolved_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                   grpc_error* error) {
  dns_resolver* r = (dns_resolver*)arg;
  grpc_channel_args* result = nullptr;
  GPR_ASSERT(r->resolving);
  r->resolving = false;
  GRPC_ERROR_REF(error);
  error = grpc_error_set_str(error, GRPC_ERROR_STR_TARGET_ADDRESS,
                             grpc_slice_from_copied_string(r->name_to_resolve));
  if (r->addresses != nullptr) {
    grpc_lb_addresses* addresses = grpc_lb_addresses_create(
        r->addresses->naddrs, nullptr /* user_data_vtable */);
    for (size_t i = 0; i < r->addresses->naddrs; ++i) {
      grpc_lb_addresses_set_address(
          addresses, i, &r->addresses->addrs[i].addr,
          r->addresses->addrs[i].len, false /* is_balancer */,
          nullptr /* balancer_name */, nullptr /* user_data */);
    }
    grpc_arg new_arg = grpc_lb_addresses_create_channel_arg(addresses);
    result = grpc_channel_args_copy_and_add(r->channel_args, &new_arg, 1);
    grpc_resolved_addresses_destroy(r->addresses);
    grpc_lb_addresses_destroy(exec_ctx, addresses);
  } else {
    grpc_millis next_try =
        grpc_backoff_step(exec_ctx, &r->backoff_state).next_attempt_start_time;
    grpc_millis timeout = next_try - grpc_exec_ctx_now(exec_ctx);
    gpr_log(GPR_INFO, "dns resolution failed (will retry): %s",
            grpc_error_string(error));
    GPR_ASSERT(!r->have_retry_timer);
    r->have_retry_timer = true;
    GRPC_RESOLVER_REF(&r->base, "retry-timer");
    if (timeout > 0) {
      gpr_log(GPR_DEBUG, "retrying in %" PRIdPTR " milliseconds", timeout);
    } else {
      gpr_log(GPR_DEBUG, "retrying immediately");
    }
    GRPC_CLOSURE_INIT(&r->on_retry, dns_on_retry_timer_locked, r,
                      grpc_combiner_scheduler(r->base.combiner));
    grpc_timer_init(exec_ctx, &r->retry_timer, next_try, &r->on_retry);
  }
  if (r->resolved_result != nullptr) {
    grpc_channel_args_destroy(exec_ctx, r->resolved_result);
  }
  r->resolved_result = result;
  r->resolved_version++;
  dns_maybe_finish_next_locked(exec_ctx, r);
  GRPC_ERROR_UNREF(error);

  GRPC_RESOLVER_UNREF(exec_ctx, &r->base, "dns-resolving");
}

static void dns_start_resolving_locked(grpc_exec_ctx* exec_ctx,
                                       dns_resolver* r) {
  GRPC_RESOLVER_REF(&r->base, "dns-resolving");
  GPR_ASSERT(!r->resolving);
  r->resolving = true;
  r->addresses = nullptr;
  grpc_resolve_address(
      exec_ctx, r->name_to_resolve, r->default_port, r->interested_parties,
      GRPC_CLOSURE_CREATE(dns_on_resolved_locked, r,
                          grpc_combiner_scheduler(r->base.combiner)),
      &r->addresses);
}

static void dns_maybe_finish_next_locked(grpc_exec_ctx* exec_ctx,
                                         dns_resolver* r) {
  if (r->next_completion != nullptr &&
      r->resolved_version != r->published_version) {
    *r->target_result = r->resolved_result == nullptr
                            ? nullptr
                            : grpc_channel_args_copy(r->resolved_result);
    GRPC_CLOSURE_SCHED(exec_ctx, r->next_completion, GRPC_ERROR_NONE);
    r->next_completion = nullptr;
    r->published_version = r->resolved_version;
  }
}

static void dns_destroy(grpc_exec_ctx* exec_ctx, grpc_resolver* gr) {
  dns_resolver* r = (dns_resolver*)gr;
  if (r->resolved_result != nullptr) {
    grpc_channel_args_destroy(exec_ctx, r->resolved_result);
  }
  grpc_pollset_set_destroy(exec_ctx, r->interested_parties);
  gpr_free(r->name_to_resolve);
  gpr_free(r->default_port);
  grpc_channel_args_destroy(exec_ctx, r->channel_args);
  gpr_free(r);
}

static grpc_resolver* dns_create(grpc_exec_ctx* exec_ctx,
                                 grpc_resolver_args* args,
                                 const char* default_port) {
  if (0 != strcmp(args->uri->authority, "")) {
    gpr_log(GPR_ERROR, "authority based dns uri's not supported");
    return nullptr;
  }
  // Get name from args.
  char* path = args->uri->path;
  if (path[0] == '/') ++path;
  // Create resolver.
  dns_resolver* r = (dns_resolver*)gpr_zalloc(sizeof(dns_resolver));
  grpc_resolver_init(&r->base, &dns_resolver_vtable, args->combiner);
  r->name_to_resolve = gpr_strdup(path);
  r->default_port = gpr_strdup(default_port);
  r->channel_args = grpc_channel_args_copy(args->args);
  r->interested_parties = grpc_pollset_set_create();
  if (args->pollset_set != nullptr) {
    grpc_pollset_set_add_pollset_set(exec_ctx, r->interested_parties,
                                     args->pollset_set);
  }
  grpc_backoff_init(
      &r->backoff_state, GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS * 1000,
      GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER, GRPC_DNS_RECONNECT_JITTER,
      GRPC_DNS_MIN_CONNECT_TIMEOUT_SECONDS * 1000,
      GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS * 1000);
  return &r->base;
}

/*
 * FACTORY
 */

static void dns_factory_ref(grpc_resolver_factory* factory) {}

static void dns_factory_unref(grpc_resolver_factory* factory) {}

static grpc_resolver* dns_factory_create_resolver(
    grpc_exec_ctx* exec_ctx, grpc_resolver_factory* factory,
    grpc_resolver_args* args) {
  return dns_create(exec_ctx, args, "https");
}

static char* dns_factory_get_default_host_name(grpc_resolver_factory* factory,
                                               grpc_uri* uri) {
  const char* path = uri->path;
  if (path[0] == '/') ++path;
  return gpr_strdup(path);
}

static const grpc_resolver_factory_vtable dns_factory_vtable = {
    dns_factory_ref, dns_factory_unref, dns_factory_create_resolver,
    dns_factory_get_default_host_name, "dns"};
static grpc_resolver_factory dns_resolver_factory = {&dns_factory_vtable};

static grpc_resolver_factory* dns_resolver_factory_create() {
  return &dns_resolver_factory;
}

extern "C" void grpc_resolver_dns_native_init(void) {
  char* resolver = gpr_getenv("GRPC_DNS_RESOLVER");
  if (resolver != nullptr && gpr_stricmp(resolver, "native") == 0) {
    gpr_log(GPR_DEBUG, "Using native dns resolver");
    grpc_register_resolver_type(dns_resolver_factory_create());
  } else {
    grpc_resolver_factory* existing_factory =
        grpc_resolver_factory_lookup("dns");
    if (existing_factory == nullptr) {
      gpr_log(GPR_DEBUG, "Using native dns resolver");
      grpc_register_resolver_type(dns_resolver_factory_create());
    } else {
      grpc_resolver_factory_unref(existing_factory);
    }
  }
  gpr_free(resolver);
}

extern "C" void grpc_resolver_dns_native_shutdown(void) {}
