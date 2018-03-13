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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/timer.h"
#include "test/core/util/test_config.h"

static gpr_mu g_mu;
static bool g_fail_resolution = true;
static grpc_combiner* g_combiner;

static void my_resolve_address(const char* addr, const char* default_port,
                               grpc_pollset_set* interested_parties,
                               grpc_closure* on_done,
                               grpc_resolved_addresses** addrs) {
  gpr_mu_lock(&g_mu);
  GPR_ASSERT(0 == strcmp("test", addr));
  grpc_error* error = GRPC_ERROR_NONE;
  if (g_fail_resolution) {
    g_fail_resolution = false;
    gpr_mu_unlock(&g_mu);
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Forced Failure");
  } else {
    gpr_mu_unlock(&g_mu);
    *addrs = static_cast<grpc_resolved_addresses*>(gpr_malloc(sizeof(**addrs)));
    (*addrs)->naddrs = 1;
    (*addrs)->addrs = static_cast<grpc_resolved_address*>(
        gpr_malloc(sizeof(*(*addrs)->addrs)));
    (*addrs)->addrs[0].len = 123;
  }
  GRPC_CLOSURE_SCHED(on_done, error);
}

static grpc_address_resolver_vtable test_resolver = {my_resolve_address,
                                                     nullptr};

static grpc_ares_request* my_dns_lookup_ares(
    const char* dns_server, const char* addr, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_lb_addresses** lb_addrs, bool check_grpclb,
    char** service_config_json) {
  gpr_mu_lock(&g_mu);
  GPR_ASSERT(0 == strcmp("test", addr));
  grpc_error* error = GRPC_ERROR_NONE;
  if (g_fail_resolution) {
    g_fail_resolution = false;
    gpr_mu_unlock(&g_mu);
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Forced Failure");
  } else {
    gpr_mu_unlock(&g_mu);
    *lb_addrs = grpc_lb_addresses_create(1, nullptr);
    grpc_lb_addresses_set_address(*lb_addrs, 0, nullptr, 0, false, nullptr,
                                  nullptr);
  }
  GRPC_CLOSURE_SCHED(on_done, error);
  return nullptr;
}

static grpc_core::OrphanablePtr<grpc_core::Resolver> create_resolver(
    const char* name) {
  grpc_core::ResolverFactory* factory =
      grpc_core::ResolverRegistry::LookupResolverFactory("dns");
  grpc_uri* uri = grpc_uri_parse(name, 0);
  GPR_ASSERT(uri);
  grpc_core::ResolverArgs args;
  args.uri = uri;
  args.combiner = g_combiner;
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(args);
  grpc_uri_destroy(uri);
  return resolver;
}

static void on_done(void* ev, grpc_error* error) {
  gpr_event_set(static_cast<gpr_event*>(ev), (void*)1);
}

// interleave waiting for an event with a timer check
static bool wait_loop(int deadline_seconds, gpr_event* ev) {
  while (deadline_seconds) {
    gpr_log(GPR_DEBUG, "Test: waiting for %d more seconds", deadline_seconds);
    if (gpr_event_wait(ev, grpc_timeout_seconds_to_deadline(1))) return true;
    deadline_seconds--;

    grpc_core::ExecCtx exec_ctx;
    grpc_timer_check(nullptr);
  }
  return false;
}

typedef struct next_args {
  grpc_core::Resolver* resolver;
  grpc_channel_args** result;
  grpc_closure* on_complete;
} next_args;

static void call_resolver_next_now_lock_taken(void* arg,
                                              grpc_error* error_unused) {
  next_args* a = static_cast<next_args*>(arg);
  a->resolver->NextLocked(a->result, a->on_complete);
  gpr_free(a);
}

static void call_resolver_next_after_locking(grpc_core::Resolver* resolver,
                                             grpc_channel_args** result,
                                             grpc_closure* on_complete,
                                             grpc_combiner* combiner) {
  next_args* a = static_cast<next_args*>(gpr_malloc(sizeof(*a)));
  a->resolver = resolver;
  a->result = result;
  a->on_complete = on_complete;
  GRPC_CLOSURE_SCHED(GRPC_CLOSURE_CREATE(call_resolver_next_now_lock_taken, a,
                                         grpc_combiner_scheduler(combiner)),
                     GRPC_ERROR_NONE);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);

  grpc_init();
  gpr_mu_init(&g_mu);
  g_combiner = grpc_combiner_create();
  grpc_set_resolver_impl(&test_resolver);
  grpc_dns_lookup_ares = my_dns_lookup_ares;
  grpc_channel_args* result = (grpc_channel_args*)1;

  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
        create_resolver("dns:test");
    gpr_event ev1;
    gpr_event_init(&ev1);
    call_resolver_next_after_locking(
        resolver.get(), &result,
        GRPC_CLOSURE_CREATE(on_done, &ev1, grpc_schedule_on_exec_ctx),
        g_combiner);
    grpc_core::ExecCtx::Get()->Flush();
    GPR_ASSERT(wait_loop(5, &ev1));
    GPR_ASSERT(result == nullptr);

    gpr_event ev2;
    gpr_event_init(&ev2);
    call_resolver_next_after_locking(
        resolver.get(), &result,
        GRPC_CLOSURE_CREATE(on_done, &ev2, grpc_schedule_on_exec_ctx),
        g_combiner);
    grpc_core::ExecCtx::Get()->Flush();
    GPR_ASSERT(wait_loop(30, &ev2));
    GPR_ASSERT(result != nullptr);

    grpc_channel_args_destroy(result);
    GRPC_COMBINER_UNREF(g_combiner, "test");
  }

  grpc_shutdown();
  gpr_mu_destroy(&g_mu);
}
