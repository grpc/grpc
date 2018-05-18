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

extern grpc_address_resolver_vtable* grpc_resolve_address_impl;
static grpc_address_resolver_vtable* default_resolve_address;

static grpc_combiner* g_combiner;

grpc_ares_request* (*g_default_dns_lookup_ares)(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_lb_addresses** addrs, bool check_grpclb, char** service_config_json);

// Counter incremented by test_resolve_address_impl indicating the number of
// times a system-level resolution has happened.
static int g_resolution_count;

struct iomgr_args {
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
}

static grpc_error* test_blocking_resolve_address_impl(
    const char* name, const char* default_port,
    grpc_resolved_addresses** addresses) {
  return default_resolve_address->blocking_resolve_address(name, default_port,
                                                           addresses);
}

static grpc_address_resolver_vtable test_resolver = {
    test_resolve_address_impl, test_blocking_resolve_address_impl};

grpc_ares_request* test_dns_lookup_ares(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_lb_addresses** addrs, bool check_grpclb, char** service_config_json) {
  grpc_ares_request* result = g_default_dns_lookup_ares(
      dns_server, name, default_port, g_iomgr_args.pollset_set, on_done, addrs,
      check_grpclb, service_config_json);
  ++g_resolution_count;
  return result;
}

static gpr_timespec test_deadline(void) {
  return grpc_timeout_seconds_to_deadline(100);
}

static void do_nothing(void* arg, grpc_error* error) {}

void iomgr_args_init(iomgr_args* args) {
  gpr_event_init(&args->ev);
  args->pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(args->pollset, &args->mu);
  args->pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(args->pollset_set, args->pollset);
  gpr_atm_rel_store(&args->done_atm, 0);
}

void iomgr_args_finish(iomgr_args* args) {
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
    gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRIdPTR, done, time_left);
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
  grpc_millis delay_before_second_resolution = 0;
};

// Counter for the number of times a resolution notification callback has been
// invoked.
static int g_on_resolution_invocations_count;

// Set to true by the last callback in the resolution chain.
bool g_all_callbacks_invoked;

void on_fourth_resolution(void* arg, grpc_error* error) {
  OnResolutionCallbackArg* cb_arg = static_cast<OnResolutionCallbackArg*>(arg);
  grpc_channel_args_destroy(cb_arg->result);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  ++g_on_resolution_invocations_count;
  gpr_log(GPR_INFO,
          "4th: g_on_resolution_invocations_count: %d, g_resolution_count: %d",
          g_on_resolution_invocations_count, g_resolution_count);
  // In this case we expect to have incurred in another system-level resolution
  // because on_third_resolution slept for longer than the min resolution
  // period.
  GPR_ASSERT(g_on_resolution_invocations_count == 4);
  GPR_ASSERT(g_resolution_count == 3);
  cb_arg->resolver.reset();
  gpr_atm_rel_store(&g_iomgr_args.done_atm, 1);
  gpr_mu_lock(g_iomgr_args.mu);
  GRPC_LOG_IF_ERROR("pollset_kick",
                    grpc_pollset_kick(g_iomgr_args.pollset, nullptr));
  gpr_mu_unlock(g_iomgr_args.mu);
  grpc_core::Delete(cb_arg);
  g_all_callbacks_invoked = true;
}

void on_third_resolution(void* arg, grpc_error* error) {
  OnResolutionCallbackArg* cb_arg = static_cast<OnResolutionCallbackArg*>(arg);
  grpc_channel_args_destroy(cb_arg->result);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  ++g_on_resolution_invocations_count;
  gpr_log(GPR_INFO,
          "3rd: g_on_resolution_invocations_count: %d, g_resolution_count: %d",
          g_on_resolution_invocations_count, g_resolution_count);
  // The timer set because of the previous re-resolution request fires, so a new
  // system-level resolution happened.
  GPR_ASSERT(g_on_resolution_invocations_count == 3);
  GPR_ASSERT(g_resolution_count == 2);
  grpc_core::ExecCtx::Get()->TestOnlySetNow(
      cb_arg->delay_before_second_resolution * 2);
  cb_arg->resolver->NextLocked(
      &cb_arg->result,
      GRPC_CLOSURE_CREATE(on_fourth_resolution, arg,
                          grpc_combiner_scheduler(g_combiner)));
  cb_arg->resolver->RequestReresolutionLocked();
  gpr_mu_lock(g_iomgr_args.mu);
  GRPC_LOG_IF_ERROR("pollset_kick",
                    grpc_pollset_kick(g_iomgr_args.pollset, nullptr));
  gpr_mu_unlock(g_iomgr_args.mu);
}

void on_second_resolution(void* arg, grpc_error* error) {
  OnResolutionCallbackArg* cb_arg = static_cast<OnResolutionCallbackArg*>(arg);
  grpc_channel_args_destroy(cb_arg->result);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  ++g_on_resolution_invocations_count;
  gpr_log(GPR_INFO,
          "2nd: g_on_resolution_invocations_count: %d, g_resolution_count: %d",
          g_on_resolution_invocations_count, g_resolution_count);
  // The resolution request for which this function is the callback happened
  // before the min resolution period. Therefore, no new system-level
  // resolutions happened, as indicated by g_resolution_count. But a resolution
  // timer was set to fire when the cooldown finishes.
  GPR_ASSERT(g_on_resolution_invocations_count == 2);
  GPR_ASSERT(g_resolution_count == 1);
  // Register a new callback to capture the timer firing.
  cb_arg->resolver->NextLocked(
      &cb_arg->result,
      GRPC_CLOSURE_CREATE(on_third_resolution, arg,
                          grpc_combiner_scheduler(g_combiner)));
  gpr_mu_lock(g_iomgr_args.mu);
  GRPC_LOG_IF_ERROR("pollset_kick",
                    grpc_pollset_kick(g_iomgr_args.pollset, nullptr));
  gpr_mu_unlock(g_iomgr_args.mu);
}

void on_first_resolution(void* arg, grpc_error* error) {
  OnResolutionCallbackArg* cb_arg = static_cast<OnResolutionCallbackArg*>(arg);
  grpc_channel_args_destroy(cb_arg->result);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  ++g_on_resolution_invocations_count;
  gpr_log(GPR_INFO,
          "1st: g_on_resolution_invocations_count: %d, g_resolution_count: %d",
          g_on_resolution_invocations_count, g_resolution_count);
  // There's one initial system-level resolution and one invocation of a
  // notification callback (the current function).
  GPR_ASSERT(g_on_resolution_invocations_count == 1);
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
  g_on_resolution_invocations_count = 0;
  g_resolution_count = 0;
  constexpr int kMinResolutionPeriodMs = 1000;

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
  res_cb_arg->delay_before_second_resolution = kMinResolutionPeriodMs;
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

  g_default_dns_lookup_ares = grpc_dns_lookup_ares;
  grpc_dns_lookup_ares = test_dns_lookup_ares;
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
