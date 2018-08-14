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

#include <cstring>

#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "test/core/util/test_config.h"

constexpr int kMinResolutionPeriodMs = 1000;

extern grpc_address_resolver_vtable* grpc_resolve_address_impl;
static grpc_address_resolver_vtable* default_resolve_address;

static grpc_combiner* g_combiner;

static grpc_ares_request* (*g_default_dns_lookup_ares_locked)(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_lb_addresses** addrs, bool check_grpclb, char** service_config_json,
    grpc_combiner* combiner);

// Counter incremented by test_resolve_address_impl indicating the number of
// times a system-level resolution has happened.
static int g_resolution_count;

static struct iomgr_args {
  gpr_event ev;
  gpr_atm done_atm;
  gpr_mu* mu;
  grpc_pollset* pollset;
  grpc_pollset_set* pollset_set;
} g_iomgr_args;

// Wrapper around default resolve_address in order to count the number of
// times we incur in a system-level name resolution.
static void test_resolve_address_impl(const char* name,
                                      const char* default_port,
                                      grpc_pollset_set* interested_parties,
                                      grpc_closure* on_done,
                                      grpc_resolved_addresses** addrs) {
  default_resolve_address->resolve_address(
      name, default_port, g_iomgr_args.pollset_set, on_done, addrs);
  ++g_resolution_count;
  static grpc_millis last_resolution_time = 0;
  if (last_resolution_time == 0) {
    last_resolution_time =
        grpc_timespec_to_millis_round_up(gpr_now(GPR_CLOCK_MONOTONIC));
  } else {
    grpc_millis now =
        grpc_timespec_to_millis_round_up(gpr_now(GPR_CLOCK_MONOTONIC));
    GPR_ASSERT(now - last_resolution_time >= kMinResolutionPeriodMs);
    last_resolution_time = now;
  }
}

static grpc_error* test_blocking_resolve_address_impl(
    const char* name, const char* default_port,
    grpc_resolved_addresses** addresses) {
  return default_resolve_address->blocking_resolve_address(name, default_port,
                                                           addresses);
}

static grpc_address_resolver_vtable test_resolver = {
    test_resolve_address_impl, test_blocking_resolve_address_impl};

static grpc_ares_request* test_dns_lookup_ares_locked(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_lb_addresses** addrs, bool check_grpclb, char** service_config_json,
    grpc_combiner* combiner) {
  grpc_ares_request* result = g_default_dns_lookup_ares_locked(
      dns_server, name, default_port, g_iomgr_args.pollset_set, on_done, addrs,
      check_grpclb, service_config_json, combiner);
  ++g_resolution_count;
  static grpc_millis last_resolution_time = 0;
  if (last_resolution_time == 0) {
        grpc_timespec_to_millis_round_up(gpr_now(GPR_CLOCK_MONOTONIC));
  } else {
    grpc_millis now =
        grpc_timespec_to_millis_round_up(gpr_now(GPR_CLOCK_MONOTONIC));
    GPR_ASSERT(now - last_resolution_time >= kMinResolutionPeriodMs);
    last_resolution_time = now;
  }
  return result;
}

static gpr_timespec test_deadline(void) {
  return grpc_timeout_seconds_to_deadline(100);
}

static void do_nothing(void* arg, grpc_error* error) {}

static void iomgr_args_init(iomgr_args* args) {
  gpr_event_init(&args->ev);
  args->pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(args->pollset, &args->mu);
  args->pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(args->pollset_set, args->pollset);
  gpr_atm_rel_store(&args->done_atm, 0);
}

static void iomgr_args_finish(iomgr_args* args) {
  GPR_ASSERT(gpr_event_wait(&args->ev, test_deadline()));
  grpc_pollset_set_del_pollset(args->pollset_set, args->pollset);
  grpc_pollset_set_destroy(args->pollset_set);
  grpc_closure do_nothing_cb;
  GRPC_CLOSURE_INIT(&do_nothing_cb, do_nothing, nullptr,
                    grpc_schedule_on_exec_ctx);
  gpr_mu_lock(args->mu);
  grpc_pollset_shutdown(args->pollset, &do_nothing_cb);
  gpr_mu_unlock(args->mu);
  // exec_ctx needs to be flushed before calling grpc_pollset_destroy()
  grpc_core::ExecCtx::Get()->Flush();
  grpc_pollset_destroy(args->pollset);
  gpr_free(args->pollset);
}

static grpc_millis n_sec_deadline(int seconds) {
  return grpc_timespec_to_millis_round_up(
      grpc_timeout_seconds_to_deadline(seconds));
}

static void poll_pollset_until_request_done(iomgr_args* args) {
  grpc_core::ExecCtx exec_ctx;
  grpc_millis deadline = n_sec_deadline(10);
  while (true) {
    bool done = gpr_atm_acq_load(&args->done_atm) != 0;
    if (done) {
      break;
    }
    grpc_millis time_left = deadline - grpc_core::ExecCtx::Get()->Now();
    gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRId64, done, time_left);
    GPR_ASSERT(time_left >= 0);
    grpc_pollset_worker* worker = nullptr;
    gpr_mu_lock(args->mu);
    GRPC_LOG_IF_ERROR("pollset_work", grpc_pollset_work(args->pollset, &worker,
                                                        n_sec_deadline(1)));
    gpr_mu_unlock(args->mu);
    grpc_core::ExecCtx::Get()->Flush();
  }
  gpr_event_set(&args->ev, (void*)1);
}

struct OnResolutionCallbackArg {
  const char* uri_str = nullptr;
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver;
  grpc_channel_args* result = nullptr;
};

// Set to true by the last callback in the resolution chain.
static bool g_all_callbacks_invoked;

static void on_second_resolution(void* arg, grpc_error* error) {
  OnResolutionCallbackArg* cb_arg = static_cast<OnResolutionCallbackArg*>(arg);
  grpc_channel_args_destroy(cb_arg->result);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  gpr_log(GPR_INFO, "2nd: g_resolution_count: %d", g_resolution_count);
  // The resolution callback was not invoked until new data was
  // available, which was delayed until after the cooldown period.
  GPR_ASSERT(g_resolution_count == 2);
  cb_arg->resolver.reset();
  gpr_atm_rel_store(&g_iomgr_args.done_atm, 1);
  gpr_mu_lock(g_iomgr_args.mu);
  GRPC_LOG_IF_ERROR("pollset_kick",
                    grpc_pollset_kick(g_iomgr_args.pollset, nullptr));
  gpr_mu_unlock(g_iomgr_args.mu);
  grpc_core::Delete(cb_arg);
  g_all_callbacks_invoked = true;
}

static void on_first_resolution(void* arg, grpc_error* error) {
  OnResolutionCallbackArg* cb_arg = static_cast<OnResolutionCallbackArg*>(arg);
  grpc_channel_args_destroy(cb_arg->result);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  gpr_log(GPR_INFO, "1st: g_resolution_count: %d", g_resolution_count);
  // There's one initial system-level resolution and one invocation of a
  // notification callback (the current function).
  GPR_ASSERT(g_resolution_count == 1);
  cb_arg->resolver->NextLocked(
      &cb_arg->result,
      GRPC_CLOSURE_CREATE(on_second_resolution, arg,
                          grpc_combiner_scheduler(g_combiner)));
  cb_arg->resolver->RequestReresolutionLocked();
  gpr_mu_lock(g_iomgr_args.mu);
  GRPC_LOG_IF_ERROR("pollset_kick",
                    grpc_pollset_kick(g_iomgr_args.pollset, nullptr));
  gpr_mu_unlock(g_iomgr_args.mu);
}

static void start_test_under_combiner(void* arg, grpc_error* error) {
  OnResolutionCallbackArg* res_cb_arg =
      static_cast<OnResolutionCallbackArg*>(arg);

  grpc_core::ResolverFactory* factory =
      grpc_core::ResolverRegistry::LookupResolverFactory("dns");
  grpc_uri* uri = grpc_uri_parse(res_cb_arg->uri_str, 0);
  gpr_log(GPR_DEBUG, "test: '%s' should be valid for '%s'", res_cb_arg->uri_str,
          factory->scheme());
  GPR_ASSERT(uri != nullptr);
  grpc_core::ResolverArgs args;
  args.uri = uri;
  args.combiner = g_combiner;
  g_resolution_count = 0;

  grpc_arg cooldown_arg;
  cooldown_arg.key =
      const_cast<char*>(GRPC_ARG_DNS_MIN_TIME_BETWEEN_RESOLUTIONS_MS);
  cooldown_arg.type = GRPC_ARG_INTEGER;
  cooldown_arg.value.integer = kMinResolutionPeriodMs;
  auto* cooldown_channel_args =
      grpc_channel_args_copy_and_add(nullptr, &cooldown_arg, 1);
  args.args = cooldown_channel_args;
  res_cb_arg->resolver = factory->CreateResolver(args);
  grpc_channel_args_destroy(cooldown_channel_args);
  GPR_ASSERT(res_cb_arg->resolver != nullptr);
  // First resolution, would incur in system-level resolution.
  res_cb_arg->resolver->NextLocked(
      &res_cb_arg->result,
      GRPC_CLOSURE_CREATE(on_first_resolution, res_cb_arg,
                          grpc_combiner_scheduler(g_combiner)));
  grpc_uri_destroy(uri);
}

static void test_cooldown() {
  grpc_core::ExecCtx exec_ctx;
  iomgr_args_init(&g_iomgr_args);
  OnResolutionCallbackArg* res_cb_arg =
      grpc_core::New<OnResolutionCallbackArg>();
  res_cb_arg->uri_str = "dns:127.0.0.1";

  GRPC_CLOSURE_SCHED(GRPC_CLOSURE_CREATE(start_test_under_combiner, res_cb_arg,
                                         grpc_combiner_scheduler(g_combiner)),
                     GRPC_ERROR_NONE);
  grpc_core::ExecCtx::Get()->Flush();
  poll_pollset_until_request_done(&g_iomgr_args);
  iomgr_args_finish(&g_iomgr_args);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  g_combiner = grpc_combiner_create();

  g_default_dns_lookup_ares_locked = grpc_dns_lookup_ares_locked;
  grpc_dns_lookup_ares_locked = test_dns_lookup_ares_locked;
  default_resolve_address = grpc_resolve_address_impl;
  grpc_set_resolver_impl(&test_resolver);

  test_cooldown();

  {
    grpc_core::ExecCtx exec_ctx;
    GRPC_COMBINER_UNREF(g_combiner, "test");
  }
  grpc_shutdown();
  GPR_ASSERT(g_all_callbacks_invoked);
  return 0;
}
