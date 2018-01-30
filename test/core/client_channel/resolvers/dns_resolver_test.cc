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
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "test/core/util/test_config.h"

static grpc_combiner* g_combiner;

static void (*g_default_grpc_resolve_address)(
    const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    grpc_resolved_addresses** addrs);

// Counter incremented by test_resolve_address_impl indicating the number of
// times a system-level resolution has happened.
static int g_resolution_count;

// Wrapper around g_default_grpc_resolve_address in order to count the number of
// times we incur in a system-level name resolution.
static void test_resolve_address_impl(const char* name,
                                      const char* default_port,
                                      grpc_pollset_set* interested_parties,
                                      grpc_closure* on_done,
                                      grpc_resolved_addresses** addrs) {
  g_default_grpc_resolve_address(name, default_port, interested_parties,
                                 on_done, addrs);
  ++g_resolution_count;
}

static void test_succeeds(grpc_resolver_factory* factory, const char* uri_str) {
  grpc_core::ExecCtx exec_ctx;
  grpc_uri* uri = grpc_uri_parse(uri_str, 0);
  grpc_resolver_args args;
  grpc_resolver* resolver;
  gpr_log(GPR_DEBUG, "test: '%s' should be valid for '%s'", uri_str,
          factory->vtable->scheme);
  GPR_ASSERT(uri);
  memset(&args, 0, sizeof(args));
  args.uri = uri;
  args.combiner = g_combiner;
  resolver = grpc_resolver_factory_create_resolver(factory, &args);
  GPR_ASSERT(resolver != nullptr);
  GRPC_RESOLVER_UNREF(resolver, "test_succeeds");
  grpc_uri_destroy(uri);
}

static void test_fails(grpc_resolver_factory* factory, const char* string) {
  grpc_core::ExecCtx exec_ctx;
  grpc_uri* uri = grpc_uri_parse(string, 0);
  grpc_resolver_args args;
  grpc_resolver* resolver;
  gpr_log(GPR_DEBUG, "test: '%s' should be invalid for '%s'", string,
          factory->vtable->scheme);
  GPR_ASSERT(uri);
  memset(&args, 0, sizeof(args));
  args.uri = uri;
  args.combiner = g_combiner;
  resolver = grpc_resolver_factory_create_resolver(factory, &args);
  GPR_ASSERT(resolver == nullptr);
  grpc_uri_destroy(uri);
}

typedef struct on_resolution_cb_arg {
  grpc_resolver* resolver;
  grpc_channel_args* result;
  grpc_millis delay_before_second_resolution;
} on_resolution_cb_arg;

// Counter for the number of times a resolution notification callback has been
// invoked.
static int g_on_resolution_invocations_count;

void on_third_resolution(void* arg, grpc_error* error) {
  on_resolution_cb_arg* cb_arg = static_cast<on_resolution_cb_arg*>(arg);
  ++g_on_resolution_invocations_count;
  grpc_channel_args_destroy(cb_arg->result);

  gpr_log(GPR_INFO,
          "3rd: g_on_resolution_invocations_count: %d, g_resolution_count: %d",
          g_on_resolution_invocations_count, g_resolution_count);
  // In this case we expect to have incurred in another system-level resolution
  // because on_second_resolution slept for longer than the min resolution
  // period.
  GPR_ASSERT(g_on_resolution_invocations_count == 3);
  GPR_ASSERT(g_resolution_count == 2);

  grpc_resolver_shutdown_locked(cb_arg->resolver);
  GRPC_RESOLVER_UNREF(cb_arg->resolver, "on_third_resolution");
  gpr_free(cb_arg);
}

void on_second_resolution(void* arg, grpc_error* error) {
  on_resolution_cb_arg* cb_arg = static_cast<on_resolution_cb_arg*>(arg);
  ++g_on_resolution_invocations_count;
  grpc_channel_args_destroy(cb_arg->result);

  gpr_log(GPR_INFO,
          "2nd: g_on_resolution_invocations_count: %d, g_resolution_count: %d",
          g_on_resolution_invocations_count, g_resolution_count);
  // The resolution request for which this function is the callback happened
  // before the min resolution period. Therefore, no new system-level
  // resolutions happened, as indicated by g_resolution_count.
  GPR_ASSERT(g_on_resolution_invocations_count == 2);
  GPR_ASSERT(g_resolution_count == 1);

  grpc_resolver_next_locked(
      cb_arg->resolver, &cb_arg->result,
      GRPC_CLOSURE_CREATE(on_third_resolution, arg, grpc_schedule_on_exec_ctx));
  grpc_resolver_channel_saw_error_locked(cb_arg->resolver);
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(
      cb_arg->delay_before_second_resolution));
}

void on_first_resolution(void* arg, grpc_error* error) {
  on_resolution_cb_arg* cb_arg = static_cast<on_resolution_cb_arg*>(arg);
  ++g_on_resolution_invocations_count;
  grpc_channel_args_destroy(cb_arg->result);
  grpc_resolver_next_locked(cb_arg->resolver, &cb_arg->result,
                            GRPC_CLOSURE_CREATE(on_second_resolution, arg,
                                                grpc_schedule_on_exec_ctx));
  grpc_resolver_channel_saw_error_locked(cb_arg->resolver);
  gpr_log(GPR_INFO,
          "1st: g_on_resolution_invocations_count: %d, g_resolution_count: %d",
          g_on_resolution_invocations_count, g_resolution_count);
  // Theres one initial system-level resolution and one invocation of a
  // notification callback (the current function).
  GPR_ASSERT(g_on_resolution_invocations_count == 1);
  GPR_ASSERT(g_resolution_count == 1);
}

static void test_cooldown(grpc_resolver_factory* factory, const char* uri_str) {
  grpc_core::ExecCtx exec_ctx;
  grpc_uri* uri = grpc_uri_parse(uri_str, 0);
  grpc_resolver_args args;
  grpc_resolver* resolver;
  gpr_log(GPR_DEBUG, "test: '%s' should be valid for '%s'", uri_str,
          factory->vtable->scheme);
  GPR_ASSERT(uri);
  memset(&args, 0, sizeof(args));
  args.uri = uri;
  args.combiner = g_combiner;
  g_on_resolution_invocations_count = 0;
  g_resolution_count = 0;
  constexpr int kMinResolutionPeriodMs = 1000;

  grpc_arg cooldown_arg;
  cooldown_arg.key = const_cast<char*>(GRPC_ARG_DNS_MIN_RESOLUTION_PERIOD_MS);
  cooldown_arg.type = GRPC_ARG_INTEGER;
  cooldown_arg.value.integer = kMinResolutionPeriodMs;
  auto* cooldown_channel_args =
      grpc_channel_args_copy_and_add(nullptr, &cooldown_arg, 1);
  args.args = cooldown_channel_args;

  resolver = grpc_resolver_factory_create_resolver(factory, &args);
  grpc_channel_args_destroy(cooldown_channel_args);
  GPR_ASSERT(resolver != nullptr);

  on_resolution_cb_arg* arg = (on_resolution_cb_arg*)gpr_zalloc(sizeof(*arg));
  arg->resolver = resolver;
  arg->delay_before_second_resolution = kMinResolutionPeriodMs * 1.10;
  // First resolution, would incur in system-level resolution.
  grpc_resolver_next_locked(
      resolver, &arg->result,
      GRPC_CLOSURE_CREATE(on_first_resolution, arg, grpc_schedule_on_exec_ctx));
  grpc_uri_destroy(uri);
}

int main(int argc, char** argv) {
  grpc_resolver_factory* dns;
  grpc_test_init(argc, argv);
  grpc_init();

  g_combiner = grpc_combiner_create();
  g_default_grpc_resolve_address = grpc_resolve_address;
  grpc_resolve_address = test_resolve_address_impl;

  dns = grpc_resolver_factory_lookup("dns");

  test_succeeds(dns, "dns:10.2.1.1");
  test_succeeds(dns, "dns:10.2.1.1:1234");
  test_succeeds(dns, "ipv4:www.google.com");
  if (grpc_resolve_address == grpc_resolve_address_ares) {
    test_succeeds(dns, "ipv4://8.8.8.8/8.8.8.8:8888");
  } else {
    test_fails(dns, "ipv4://8.8.8.8/8.8.8.8:8888");
  }

  test_cooldown(dns, "dns:127.0.0.1");

  grpc_resolver_factory_unref(dns);
  {
    grpc_core::ExecCtx exec_ctx;
    GRPC_COMBINER_UNREF(g_combiner, "test");
  }
  grpc_shutdown();

  return 0;
}
