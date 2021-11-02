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

#include "src/core/lib/iomgr/resolve_address.h"

#include <string.h>

#include <address_sorting/address_sorting.h>

#include "absl/strings/match.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.h"
#include "src/core/lib/event_engine/sockaddr.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/util/cmdline.h"
#include "test/core/util/fake_udp_and_tcp_server.h"
#include "test/core/util/test_config.h"

static gpr_timespec test_deadline(void) {
  return grpc_timeout_seconds_to_deadline(100);
}

typedef struct args_struct {
  gpr_event ev;
  grpc_resolved_addresses* addrs;
  gpr_mu* mu;
  bool done;              // guarded by mu
  grpc_pollset* pollset;  // guarded by mu
  grpc_pollset_set* pollset_set;
} args_struct;

static void do_nothing(void* /*arg*/, grpc_error_handle /*error*/) {}

void args_init(args_struct* args) {
  gpr_event_init(&args->ev);
  args->pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(args->pollset, &args->mu);
  args->pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(args->pollset_set, args->pollset);
  args->addrs = nullptr;
  args->done = false;
}

void args_finish(args_struct* args) {
  GPR_ASSERT(gpr_event_wait(&args->ev, test_deadline()));
  grpc_resolved_addresses_destroy(args->addrs);
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

static void poll_pollset_until_request_done(args_struct* args) {
  // Try to give enough time for c-ares to run through its retries
  // a few times if needed.
  grpc_millis deadline = n_sec_deadline(90);
  while (true) {
    grpc_core::ExecCtx exec_ctx;
    {
      grpc_core::MutexLockForGprMu lock(args->mu);
      if (args->done) {
        break;
      }
      grpc_millis time_left = deadline - grpc_core::ExecCtx::Get()->Now();
      gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRId64, args->done, time_left);
      GPR_ASSERT(time_left >= 0);
      grpc_pollset_worker* worker = nullptr;
      GRPC_LOG_IF_ERROR(
          "pollset_work",
          grpc_pollset_work(args->pollset, &worker, n_sec_deadline(1)));
    }
  }
  gpr_event_set(&args->ev, reinterpret_cast<void*>(1));
}

static void must_succeed(void* argsp, grpc_error_handle err) {
  args_struct* args = static_cast<args_struct*>(argsp);
  GPR_ASSERT(err == GRPC_ERROR_NONE);
  GPR_ASSERT(args->addrs != nullptr);
  GPR_ASSERT(args->addrs->naddrs > 0);
  grpc_core::MutexLockForGprMu lock(args->mu);
  args->done = true;
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
}

static void must_fail(void* argsp, grpc_error_handle err) {
  args_struct* args = static_cast<args_struct*>(argsp);
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  grpc_core::MutexLockForGprMu lock(args->mu);
  args->done = true;
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
}

static void must_fail_expect_cancelled_error_message(void* argsp,
                                                     grpc_error_handle err) {
  args_struct* args = static_cast<args_struct*>(argsp);
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  std::string error_msg = grpc_error_std_string(err);
  GPR_ASSERT(absl::StrContains(error_msg, "DNS query cancelled"));
  grpc_core::MutexLockForGprMu lock(args->mu);
  args->done = true;
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
}

static void dont_care(void* argsp, grpc_error_handle /* err */) {
  args_struct* args = static_cast<args_struct*>(argsp);
  grpc_core::MutexLockForGprMu lock(args->mu);
  args->done = true;
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
}

// This test assumes the environment has an ipv6 loopback
static void must_succeed_with_ipv6_first(void* argsp, grpc_error_handle err) {
  args_struct* args = static_cast<args_struct*>(argsp);
  GPR_ASSERT(err == GRPC_ERROR_NONE);
  GPR_ASSERT(args->addrs != nullptr);
  GPR_ASSERT(args->addrs->naddrs > 0);
  const struct sockaddr* first_address =
      reinterpret_cast<const struct sockaddr*>(args->addrs->addrs[0].addr);
  GPR_ASSERT(first_address->sa_family == AF_INET6);
  grpc_core::MutexLockForGprMu lock(args->mu);
  args->done = true;
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
}

static void must_succeed_with_ipv4_first(void* argsp, grpc_error_handle err) {
  args_struct* args = static_cast<args_struct*>(argsp);
  GPR_ASSERT(err == GRPC_ERROR_NONE);
  GPR_ASSERT(args->addrs != nullptr);
  GPR_ASSERT(args->addrs->naddrs > 0);
  const struct sockaddr* first_address =
      reinterpret_cast<const struct sockaddr*>(args->addrs->addrs[0].addr);
  GPR_ASSERT(first_address->sa_family == AF_INET);
  grpc_core::MutexLockForGprMu lock(args->mu);
  args->done = true;
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
}

static void test_localhost(void) {
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  auto r = grpc_resolve_address(
      "localhost:1", nullptr, args.pollset_set,
      GRPC_CLOSURE_CREATE(must_succeed, &args, grpc_schedule_on_exec_ctx),
      &args.addrs);
  grpc_core::ExecCtx::Get()->Flush();
  poll_pollset_until_request_done(&args);
  args_finish(&args);
}

static void test_default_port(void) {
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  auto r = grpc_resolve_address(
      "localhost", "1", args.pollset_set,
      GRPC_CLOSURE_CREATE(must_succeed, &args, grpc_schedule_on_exec_ctx),
      &args.addrs);
  grpc_core::ExecCtx::Get()->Flush();
  poll_pollset_until_request_done(&args);
  args_finish(&args);
}

static void test_localhost_result_has_ipv6_first(void) {
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  auto r = grpc_resolve_address(
      "localhost:1", nullptr, args.pollset_set,
      GRPC_CLOSURE_CREATE(must_succeed_with_ipv6_first, &args,
                          grpc_schedule_on_exec_ctx),
      &args.addrs);
  grpc_core::ExecCtx::Get()->Flush();
  poll_pollset_until_request_done(&args);
  args_finish(&args);
}

static void test_localhost_result_has_ipv4_first_when_ipv6_isnt_available(
    void) {
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  auto r = grpc_resolve_address(
      "localhost:1", nullptr, args.pollset_set,
      GRPC_CLOSURE_CREATE(must_succeed_with_ipv4_first, &args,
                          grpc_schedule_on_exec_ctx),
      &args.addrs);
  grpc_core::ExecCtx::Get()->Flush();
  poll_pollset_until_request_done(&args);
  args_finish(&args);
}

static void test_non_numeric_default_port(void) {
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  auto r = grpc_resolve_address(
      "localhost", "https", args.pollset_set,
      GRPC_CLOSURE_CREATE(must_succeed, &args, grpc_schedule_on_exec_ctx),
      &args.addrs);
  grpc_core::ExecCtx::Get()->Flush();
  poll_pollset_until_request_done(&args);
  args_finish(&args);
}

static void test_missing_default_port(void) {
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  auto r = grpc_resolve_address(
      "localhost", nullptr, args.pollset_set,
      GRPC_CLOSURE_CREATE(must_fail, &args, grpc_schedule_on_exec_ctx),
      &args.addrs);
  grpc_core::ExecCtx::Get()->Flush();
  poll_pollset_until_request_done(&args);
  args_finish(&args);
}

static void test_ipv6_with_port(void) {
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  auto r = grpc_resolve_address(
      "[2001:db8::1]:1", nullptr, args.pollset_set,
      GRPC_CLOSURE_CREATE(must_succeed, &args, grpc_schedule_on_exec_ctx),
      &args.addrs);
  grpc_core::ExecCtx::Get()->Flush();
  poll_pollset_until_request_done(&args);
  args_finish(&args);
}

static void test_ipv6_without_port(void) {
  const char* const kCases[] = {
      "2001:db8::1",
      "2001:db8::1.2.3.4",
      "[2001:db8::1]",
  };
  unsigned i;
  for (i = 0; i < sizeof(kCases) / sizeof(*kCases); i++) {
    grpc_core::ExecCtx exec_ctx;
    args_struct args;
    args_init(&args);
    auto r = grpc_resolve_address(
        kCases[i], "80", args.pollset_set,
        GRPC_CLOSURE_CREATE(must_succeed, &args, grpc_schedule_on_exec_ctx),
        &args.addrs);
    grpc_core::ExecCtx::Get()->Flush();
    poll_pollset_until_request_done(&args);
    args_finish(&args);
  }
}

static void test_invalid_ip_addresses(void) {
  const char* const kCases[] = {
      "293.283.1238.3:1",
      "[2001:db8::11111]:1",
  };
  unsigned i;
  for (i = 0; i < sizeof(kCases) / sizeof(*kCases); i++) {
    grpc_core::ExecCtx exec_ctx;
    args_struct args;
    args_init(&args);
    auto r = grpc_resolve_address(
        kCases[i], nullptr, args.pollset_set,
        GRPC_CLOSURE_CREATE(must_fail, &args, grpc_schedule_on_exec_ctx),
        &args.addrs);
    grpc_core::ExecCtx::Get()->Flush();
    poll_pollset_until_request_done(&args);
    args_finish(&args);
  }
}

static void test_unparseable_hostports(void) {
  const char* const kCases[] = {
      "[", "[::1", "[::1]bad", "[1.2.3.4]", "[localhost]", "[localhost]:1",
  };
  unsigned i;
  for (i = 0; i < sizeof(kCases) / sizeof(*kCases); i++) {
    grpc_core::ExecCtx exec_ctx;
    args_struct args;
    args_init(&args);
    auto r = grpc_resolve_address(
        kCases[i], "1", args.pollset_set,
        GRPC_CLOSURE_CREATE(must_fail, &args, grpc_schedule_on_exec_ctx),
        &args.addrs);
    grpc_core::ExecCtx::Get()->Flush();
    poll_pollset_until_request_done(&args);
    args_finish(&args);
  }
}

// Kick off a simple DNS resolution and then immediately cancel. This
// test doesn't care what the result is, just that we don't crash etc.
static void test_immediate_cancel(void) {
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  auto r = grpc_resolve_address(
      "localhost:1", "1", args.pollset_set,
      GRPC_CLOSURE_CREATE(dont_care, &args, grpc_schedule_on_exec_ctx),
      &args.addrs);
  r.reset();  // cancel the resolution
  grpc_core::ExecCtx::Get()->Flush();
  poll_pollset_until_request_done(&args);
  args_finish(&args);
}

static int g_fake_non_responsive_dns_server_port;

static void inject_non_responsive_dns_server(ares_channel channel) {
  gpr_log(GPR_DEBUG,
          "Injecting broken nameserver list. Bad server address:|[::1]:%d|.",
          g_fake_non_responsive_dns_server_port);
  // Configure a non-responsive DNS server at the front of c-ares's nameserver
  // list.
  struct ares_addr_port_node dns_server_addrs[1];
  memset(dns_server_addrs, 0, sizeof(dns_server_addrs));
  dns_server_addrs[0].family = AF_INET6;
  (reinterpret_cast<char*>(&dns_server_addrs[0].addr.addr6))[15] = 0x1;
  dns_server_addrs[0].tcp_port = g_fake_non_responsive_dns_server_port;
  dns_server_addrs[0].udp_port = g_fake_non_responsive_dns_server_port;
  dns_server_addrs[0].next = nullptr;
  GPR_ASSERT(ares_set_servers_ports(channel, dns_server_addrs) == ARES_SUCCESS);
}

static void test_cancel_with_non_responsive_dns_server(void) {
  // Inject an unresponsive DNS server into the resolver's DNS server config
  grpc_core::testing::FakeUdpAndTcpServer fake_dns_server(
      grpc_core::testing::FakeUdpAndTcpServer::AcceptMode::
          kWaitForClientToSendFirstBytes,
      grpc_core::testing::FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer);
  g_fake_non_responsive_dns_server_port = fake_dns_server.port();
  void (*prev_test_only_inject_config)(ares_channel channel) =
      grpc_ares_test_only_inject_config;
  grpc_ares_test_only_inject_config = inject_non_responsive_dns_server;
  // Run the test
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  auto r = grpc_resolve_address(
      "foo.bar.com:1", "1", args.pollset_set,
      GRPC_CLOSURE_CREATE(must_fail_expect_cancelled_error_message, &args,
                          grpc_schedule_on_exec_ctx),
      &args.addrs);
  grpc_core::ExecCtx::Get()->Flush();  // initiate DNS requests
  r.reset();                           // cancel the resolution
  grpc_core::ExecCtx::Get()->Flush();  // let cancellation work finish
  poll_pollset_until_request_done(&args);
  args_finish(&args);
  // reset altered global state
  grpc_ares_test_only_inject_config = prev_test_only_inject_config;
}

typedef struct mock_ipv6_disabled_source_addr_factory {
  address_sorting_source_addr_factory base;
} mock_ipv6_disabled_source_addr_factory;

static bool mock_ipv6_disabled_source_addr_factory_get_source_addr(
    address_sorting_source_addr_factory* /*factory*/,
    const address_sorting_address* dest_addr,
    address_sorting_address* source_addr) {
  // Mock lack of IPv6. For IPv4, set the source addr to be the same
  // as the destination; tests won't actually connect on the result anyways.
  if (address_sorting_abstract_get_family(dest_addr) ==
      ADDRESS_SORTING_AF_INET6) {
    return false;
  }
  memcpy(source_addr->addr, &dest_addr->addr, dest_addr->len);
  source_addr->len = dest_addr->len;
  return true;
}

void mock_ipv6_disabled_source_addr_factory_destroy(
    address_sorting_source_addr_factory* factory) {
  mock_ipv6_disabled_source_addr_factory* f =
      reinterpret_cast<mock_ipv6_disabled_source_addr_factory*>(factory);
  gpr_free(f);
}

const address_sorting_source_addr_factory_vtable
    kMockIpv6DisabledSourceAddrFactoryVtable = {
        mock_ipv6_disabled_source_addr_factory_get_source_addr,
        mock_ipv6_disabled_source_addr_factory_destroy,
};

int main(int argc, char** argv) {
  // First set the resolver type based off of --resolver
  const char* resolver_type = nullptr;
  gpr_cmdline* cl = gpr_cmdline_create("resolve address test");
  gpr_cmdline_add_string(cl, "resolver", "Resolver type (ares or native)",
                         &resolver_type);
  // In case that there are more than one argument on the command line,
  // --resolver will always be the first one, so only parse the first argument
  // (other arguments may be unknown to cl)
  gpr_cmdline_parse(cl, argc > 2 ? 2 : argc, argv);
  grpc_core::UniquePtr<char> resolver =
      GPR_GLOBAL_CONFIG_GET(grpc_dns_resolver);
  if (strlen(resolver.get()) != 0) {
    gpr_log(GPR_INFO, "Warning: overriding resolver setting of %s",
            resolver.get());
  }
  if (resolver_type != nullptr && gpr_stricmp(resolver_type, "native") == 0) {
    GPR_GLOBAL_CONFIG_SET(grpc_dns_resolver, "native");
  } else if (resolver_type != nullptr &&
             gpr_stricmp(resolver_type, "ares") == 0) {
    GPR_GLOBAL_CONFIG_SET(grpc_dns_resolver, "ares");
  } else {
    gpr_log(GPR_ERROR, "--resolver_type was not set to ares or native");
    abort();
  }
  // Run the test.
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;
    test_localhost();
    test_default_port();
    test_non_numeric_default_port();
    test_missing_default_port();
    test_ipv6_with_port();
    test_ipv6_without_port();
    test_invalid_ip_addresses();
    test_unparseable_hostports();
    test_immediate_cancel();
    if (gpr_stricmp(resolver_type, "ares") == 0) {
      // This behavior expectation is specific to c-ares.
      test_localhost_result_has_ipv6_first();
      // The native resolver doesn't support cancellation
      // of I/O related work, so we can only test with c-ares.
      test_cancel_with_non_responsive_dns_server();
    }
    grpc_core::Executor::ShutdownAll();
  }
  gpr_cmdline_destroy(cl);
  grpc_shutdown();
  // The following test uses
  // "address_sorting_override_source_addr_factory_for_testing", which works
  // on a per-grpc-init basis, and so it's simplest to run this next test
  // within a standalone grpc_init/grpc_shutdown pair.
  if (gpr_stricmp(resolver_type, "ares") == 0) {
    // Run a test case in which c-ares's address sorter
    // thinks that IPv4 is available and IPv6 isn't.
    grpc_init();
    mock_ipv6_disabled_source_addr_factory* factory =
        static_cast<mock_ipv6_disabled_source_addr_factory*>(
            gpr_malloc(sizeof(mock_ipv6_disabled_source_addr_factory)));
    factory->base.vtable = &kMockIpv6DisabledSourceAddrFactoryVtable;
    address_sorting_override_source_addr_factory_for_testing(&factory->base);
    test_localhost_result_has_ipv4_first_when_ipv6_isnt_available();
    grpc_shutdown();
  }
  return 0;
}
