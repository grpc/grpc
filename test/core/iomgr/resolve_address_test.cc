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
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/functional/bind_front.h"
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
#include "test/cpp/util/test_config.h"

namespace {

grpc_millis NSecDeadline(int seconds) {
  return grpc_timespec_to_millis_round_up(
      grpc_timeout_seconds_to_deadline(seconds));
}

const char* g_resolver_type = "";

class ResolveAddressTest : public ::testing::Test {
 public:
  ResolveAddressTest() {
    grpc_init();
    grpc_core::ExecCtx exec_ctx;
    pollset_ = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollset_, &mu_);
    pollset_set_ = grpc_pollset_set_create();
    grpc_pollset_set_add_pollset(pollset_set_, pollset_);
    default_inject_config_ = grpc_ares_test_only_inject_config;
  }

  ~ResolveAddressTest() override {
    {
      grpc_core::ExecCtx exec_ctx;
      grpc_pollset_set_del_pollset(pollset_set_, pollset_);
      grpc_pollset_set_destroy(pollset_set_);
      grpc_closure do_nothing_cb;
      GRPC_CLOSURE_INIT(&do_nothing_cb, DoNothing, nullptr,
                        grpc_schedule_on_exec_ctx);
      gpr_mu_lock(mu_);
      grpc_pollset_shutdown(pollset_, &do_nothing_cb);
      gpr_mu_unlock(mu_);
      // exec_ctx needs to be flushed before calling grpc_pollset_destroy()
      grpc_core::ExecCtx::Get()->Flush();
      grpc_pollset_destroy(pollset_);
      gpr_free(pollset_);
      // reset this since it might have been altered
      grpc_ares_test_only_inject_config = default_inject_config_;
    }
    grpc_shutdown();
  }

  void PollPollsetUntilRequestDone() {
    // Try to give enough time for c-ares to run through its retries
    // a few times if needed.
    grpc_millis deadline = NSecDeadline(90);
    while (true) {
      grpc_core::ExecCtx exec_ctx;
      {
        grpc_core::MutexLockForGprMu lock(mu_);
        if (done_) {
          break;
        }
        grpc_millis time_left = deadline - grpc_core::ExecCtx::Get()->Now();
        gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRId64, done_, time_left);
        ASSERT_GE(time_left, 0);
        grpc_pollset_worker* worker = nullptr;
        GRPC_LOG_IF_ERROR("pollset_work", grpc_pollset_work(pollset_, &worker,
                                                            NSecDeadline(1)));
      }
    }
  }

  void MustSucceed(absl::StatusOr<std::vector<grpc_resolved_address>> result) {
    EXPECT_EQ(result.status(), absl::OkStatus());
    EXPECT_FALSE(result->empty());
    Finish();
  }

  void MustFail(absl::StatusOr<std::vector<grpc_resolved_address>> result) {
    EXPECT_NE(result.status(), absl::OkStatus());
    Finish();
  }

  void MustFailExpectCancelledErrorMessage(
      absl::StatusOr<std::vector<grpc_resolved_address>> result) {
    EXPECT_NE(result.status(), absl::OkStatus());
    EXPECT_THAT(result.status().ToString(),
                testing::HasSubstr("DNS query cancelled"));
    Finish();
  }

  void DontCare(
      absl::StatusOr<std::vector<grpc_resolved_address>> /* result */) {
    Finish();
  }

  // This test assumes the environment has an ipv6 loopback
  void MustSucceedWithIPv6First(
      absl::StatusOr<std::vector<grpc_resolved_address>> result) {
    EXPECT_EQ(result.status(), absl::OkStatus());
    EXPECT_TRUE(!result->empty() &&
                reinterpret_cast<const struct sockaddr*>((*result)[0].addr)
                        ->sa_family == AF_INET6);
    Finish();
  }

  void MustSucceedWithIPv4First(
      absl::StatusOr<std::vector<grpc_resolved_address>> result) {
    EXPECT_EQ(result.status(), absl::OkStatus());
    EXPECT_TRUE(!result->empty() &&
                reinterpret_cast<const struct sockaddr*>((*result)[0].addr)
                        ->sa_family == AF_INET);
    Finish();
  }

  grpc_pollset_set* pollset_set() const { return pollset_set_; }

 private:
  static void DoNothing(void* /*arg*/, grpc_error_handle /*error*/) {}

  void Finish() {
    grpc_core::MutexLockForGprMu lock(mu_);
    done_ = true;
    GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(pollset_, nullptr));
  }

  gpr_mu* mu_;
  bool done_ = false;      // guarded by mu
  grpc_pollset* pollset_;  // guarded by mu
  grpc_pollset_set* pollset_set_;
  // the default value of grpc_ares_test_only_inject_config, which might
  // be modified during a test
  void (*default_inject_config_)(ares_channel channel) = nullptr;
};

}  // namespace

TEST_F(ResolveAddressTest, Localhost) {
  grpc_core::ExecCtx exec_ctx;
  auto r = grpc_core::GetDNSResolver()->ResolveName(
      "localhost:1", "", pollset_set(),
      absl::bind_front(&ResolveAddressTest::MustSucceed, this));
  r->Start();
  grpc_core::ExecCtx::Get()->Flush();
  PollPollsetUntilRequestDone();
}

TEST_F(ResolveAddressTest, DefaultPort) {
  grpc_core::ExecCtx exec_ctx;
  auto r = grpc_core::GetDNSResolver()->ResolveName(
      "localhost", "1", pollset_set(),
      absl::bind_front(&ResolveAddressTest::MustSucceed, this));
  r->Start();
  grpc_core::ExecCtx::Get()->Flush();
  PollPollsetUntilRequestDone();
}

TEST_F(ResolveAddressTest, LocalhostResultHasIPv6First) {
  if (std::string(g_resolver_type) != "ares") {
    GTEST_SKIP() << "this test is only valid with the c-ares resolver";
  }
  grpc_core::ExecCtx exec_ctx;
  auto r = grpc_core::GetDNSResolver()->ResolveName(
      "localhost:1", "", pollset_set(),
      absl::bind_front(&ResolveAddressTest::MustSucceedWithIPv6First, this));
  r->Start();
  grpc_core::ExecCtx::Get()->Flush();
  PollPollsetUntilRequestDone();
}

namespace {

bool IPv6DisabledGetSourceAddr(address_sorting_source_addr_factory* /*factory*/,
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

void DeleteSourceAddrFactory(address_sorting_source_addr_factory* factory) {
  delete factory;
}

const address_sorting_source_addr_factory_vtable
    kMockIpv6DisabledSourceAddrFactoryVtable = {
        IPv6DisabledGetSourceAddr,
        DeleteSourceAddrFactory,
};

}  // namespace

TEST_F(ResolveAddressTest, LocalhostResultHasIPv4FirstWhenIPv6IsntAvalailable) {
  if (std::string(g_resolver_type) != "ares") {
    GTEST_SKIP() << "this test is only valid with the c-ares resolver";
  }
  // Mock the kernel source address selection. Note that source addr factory
  // is reset to its default value during grpc initialization for each test.
  address_sorting_source_addr_factory* mock =
      new address_sorting_source_addr_factory();
  mock->vtable = &kMockIpv6DisabledSourceAddrFactoryVtable;
  address_sorting_override_source_addr_factory_for_testing(mock);
  // run the test
  grpc_core::ExecCtx exec_ctx;
  auto r = grpc_core::GetDNSResolver()->ResolveName(
      "localhost:1", "", pollset_set(),
      absl::bind_front(&ResolveAddressTest::MustSucceedWithIPv4First, this));
  r->Start();
  grpc_core::ExecCtx::Get()->Flush();
  PollPollsetUntilRequestDone();
}

TEST_F(ResolveAddressTest, NonNumericDefaultPort) {
  grpc_core::ExecCtx exec_ctx;
  auto r = grpc_core::GetDNSResolver()->ResolveName(
      "localhost", "http", pollset_set(),
      absl::bind_front(&ResolveAddressTest::MustSucceed, this));
  r->Start();
  grpc_core::ExecCtx::Get()->Flush();
  PollPollsetUntilRequestDone();
}

TEST_F(ResolveAddressTest, MissingDefaultPort) {
  grpc_core::ExecCtx exec_ctx;
  auto r = grpc_core::GetDNSResolver()->ResolveName(
      "localhost", "", pollset_set(),
      absl::bind_front(&ResolveAddressTest::MustFail, this));
  r->Start();
  grpc_core::ExecCtx::Get()->Flush();
  PollPollsetUntilRequestDone();
}

TEST_F(ResolveAddressTest, IPv6WithPort) {
  grpc_core::ExecCtx exec_ctx;
  auto r = grpc_core::GetDNSResolver()->ResolveName(
      "[2001:db8::1]:1", "", pollset_set(),
      absl::bind_front(&ResolveAddressTest::MustSucceed, this));
  r->Start();
  grpc_core::ExecCtx::Get()->Flush();
  PollPollsetUntilRequestDone();
}

void TestIPv6WithoutPort(ResolveAddressTest* test, const char* target) {
  grpc_core::ExecCtx exec_ctx;
  auto r = grpc_core::GetDNSResolver()->ResolveName(
      target, "80", test->pollset_set(),
      absl::bind_front(&ResolveAddressTest::MustSucceed, test));
  r->Start();
  grpc_core::ExecCtx::Get()->Flush();
  test->PollPollsetUntilRequestDone();
}

TEST_F(ResolveAddressTest, IPv6WithoutPortNoBrackets) {
  TestIPv6WithoutPort(this, "2001:db8::1");
}

TEST_F(ResolveAddressTest, IPv6WithoutPortWithBrackets) {
  TestIPv6WithoutPort(this, "[2001:db8::1]");
}

TEST_F(ResolveAddressTest, IPv6WithoutPortV4MappedV6) {
  TestIPv6WithoutPort(this, "2001:db8::1.2.3.4");
}

void TestInvalidIPAddress(ResolveAddressTest* test, const char* target) {
  grpc_core::ExecCtx exec_ctx;
  auto r = grpc_core::GetDNSResolver()->ResolveName(
      target, "", test->pollset_set(),
      absl::bind_front(&ResolveAddressTest::MustFail, test));
  r->Start();
  grpc_core::ExecCtx::Get()->Flush();
  test->PollPollsetUntilRequestDone();
}

TEST_F(ResolveAddressTest, InvalidIPv4Addresses) {
  TestInvalidIPAddress(this, "293.283.1238.3:1");
}

TEST_F(ResolveAddressTest, InvalidIPv6Addresses) {
  TestInvalidIPAddress(this, "[2001:db8::11111]:1");
}

void TestUnparseableHostPort(ResolveAddressTest* test, const char* target) {
  grpc_core::ExecCtx exec_ctx;
  auto r = grpc_core::GetDNSResolver()->ResolveName(
      target, "1", test->pollset_set(),
      absl::bind_front(&ResolveAddressTest::MustFail, test));
  r->Start();
  grpc_core::ExecCtx::Get()->Flush();
  test->PollPollsetUntilRequestDone();
}

TEST_F(ResolveAddressTest, UnparseableHostPortsOnlyBracket) {
  TestUnparseableHostPort(this, "[");
}

TEST_F(ResolveAddressTest, UnparseableHostPortsMissingRightBracket) {
  TestUnparseableHostPort(this, "[::1");
}

TEST_F(ResolveAddressTest, UnparseableHostPortsBadPort) {
  TestUnparseableHostPort(this, "[::1]bad");
}

TEST_F(ResolveAddressTest, UnparseableHostPortsBadIPv6) {
  TestUnparseableHostPort(this, "[1.2.3.4]");
}

TEST_F(ResolveAddressTest, UnparseableHostPortsBadLocalhost) {
  TestUnparseableHostPort(this, "[localhost]");
}

TEST_F(ResolveAddressTest, UnparseableHostPortsBadLocalhostWithPort) {
  TestUnparseableHostPort(this, "[localhost]:1");
}

// Kick off a simple DNS resolution and then immediately cancel. This
// test doesn't care what the result is, just that we don't crash etc.
TEST_F(ResolveAddressTest, ImmediateCancel) {
  grpc_core::ExecCtx exec_ctx;
  auto r = grpc_core::GetDNSResolver()->ResolveName(
      "localhost:1", "1", pollset_set(),
      absl::bind_front(&ResolveAddressTest::DontCare, this));
  r->Start();
  r.reset();  // cancel the resolution
  grpc_core::ExecCtx::Get()->Flush();
  PollPollsetUntilRequestDone();
}

namespace {

int g_fake_non_responsive_dns_server_port;

void InjectNonResponsiveDNSServer(ares_channel channel) {
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
  ASSERT_EQ(ares_set_servers_ports(channel, dns_server_addrs), ARES_SUCCESS);
}

}  // namespace

TEST_F(ResolveAddressTest, CancelWithNonResponsiveDNSServer) {
  if (std::string(g_resolver_type) != "ares") {
    GTEST_SKIP() << "the native resolver doesn't support cancellation, so we "
                    "can only test this with c-ares";
  }
  // Inject an unresponsive DNS server into the resolver's DNS server config
  grpc_core::testing::FakeUdpAndTcpServer fake_dns_server(
      grpc_core::testing::FakeUdpAndTcpServer::AcceptMode::
          kWaitForClientToSendFirstBytes,
      grpc_core::testing::FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer);
  g_fake_non_responsive_dns_server_port = fake_dns_server.port();
  grpc_ares_test_only_inject_config = InjectNonResponsiveDNSServer;
  // Run the test
  grpc_core::ExecCtx exec_ctx;
  auto r = grpc_core::GetDNSResolver()->ResolveName(
      "foo.bar.com:1", "1", pollset_set(),
      absl::bind_front(&ResolveAddressTest::MustFailExpectCancelledErrorMessage,
                       this));
  r->Start();
  grpc_core::ExecCtx::Get()->Flush();  // initiate DNS requests
  r.reset();                           // cancel the resolution
  grpc_core::ExecCtx::Get()->Flush();  // let cancellation work finish
  PollPollsetUntilRequestDone();
}

int main(int argc, char** argv) {
  // Configure the DNS resolver (c-ares vs. native) based on the
  // name of the binary. TODO(apolcyn): is there a way to pass command
  // line flags to a gtest that it works in all of our test environments?
  if (absl::StrContains(std::string(argv[0]), "using_native_resolver")) {
    g_resolver_type = "native";
  } else if (absl::StrContains(std::string(argv[0]), "using_ares_resolver")) {
    g_resolver_type = "ares";
  } else {
    GPR_ASSERT(0);
  }
  GPR_GLOBAL_CONFIG_SET(grpc_dns_resolver, g_resolver_type);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  const auto result = RUN_ALL_TESTS();
  return result;
}
