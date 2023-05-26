// Copyright 2022 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// IWYU pragma: no_include <ratio>
// IWYU pragma: no_include <arpa/inet.h>

#include <stdint.h>
#include <stdio.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <address_sorting/address_sorting.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "test/core/event_engine/test_suite/event_engine_test_framework.h"
#include "test/core/util/fake_udp_and_tcp_server.h"
#include "test/core/util/port.h"
#include "test/cpp/util/subprocess.h"

namespace grpc_event_engine {
namespace experimental {

void InitDNSTests() {}

}  // namespace experimental
}  // namespace grpc_event_engine

#ifdef GPR_WINDOWS
class EventEngineDNSTest : public EventEngineTest {};

// TODO(yijiem): make the test run on Windows
TEST_F(EventEngineDNSTest, TODO) { grpc_core::ExecCtx exec_ctx; }
#else

namespace {

using grpc_event_engine::experimental::EventEngine;
using SRVRecord = EventEngine::DNSResolver::SRVRecord;
using testing::ElementsAre;
using testing::Pointwise;
using testing::UnorderedPointwise;

// TODO(yijiem): make this portable for Windows
constexpr char kDNSTestRecordGroupsYamlPath[] =
    "test/core/event_engine/test_suite/tests/dns_test_record_groups.yaml";
constexpr char kHealthCheckRecordName[] =
    "health-check-local-dns-server-is-alive.resolver-tests.grpctestingexp";

MATCHER(ResolvedAddressEq, "") {
  const auto& addr0 = std::get<0>(arg);
  const auto& addr1 = std::get<1>(arg);
  return addr0.size() == addr1.size() &&
         memcmp(addr0.address(), addr1.address(), addr0.size()) == 0;
}

MATCHER(SRVRecordEq, "") {
  const auto& arg0 = std::get<0>(arg);
  const auto& arg1 = std::get<1>(arg);
  return arg0.host == arg1.host && arg0.port == arg1.port &&
         arg0.priority == arg1.priority && arg0.weight == arg1.weight;
}

MATCHER(StatusCodeEq, "") {
  return std::get<0>(arg).code() == std::get<1>(arg);
}

// Copied from tcp_socket_utils_test.cc
// TODO(yijiem): maybe move those into common test util
EventEngine::ResolvedAddress MakeAddr4(const uint8_t* data, size_t data_len,
                                       int port) {
  EventEngine::ResolvedAddress resolved_addr4;
  sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(
      const_cast<sockaddr*>(resolved_addr4.address()));
  memset(&resolved_addr4, 0, sizeof(resolved_addr4));
  addr4->sin_family = AF_INET;
  GPR_ASSERT(data_len == sizeof(addr4->sin_addr.s_addr));
  memcpy(&addr4->sin_addr.s_addr, data, data_len);
  addr4->sin_port = htons(port);
  return EventEngine::ResolvedAddress(
      reinterpret_cast<sockaddr*>(addr4),
      static_cast<socklen_t>(sizeof(sockaddr_in)));
}

EventEngine::ResolvedAddress MakeAddr6(const uint8_t* data, size_t data_len,
                                       int port) {
  EventEngine::ResolvedAddress resolved_addr6;
  sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(
      const_cast<sockaddr*>(resolved_addr6.address()));
  memset(&resolved_addr6, 0, sizeof(resolved_addr6));
  addr6->sin6_family = AF_INET6;
  GPR_ASSERT(data_len == sizeof(addr6->sin6_addr.s6_addr));
  memcpy(&addr6->sin6_addr.s6_addr, data, data_len);
  addr6->sin6_port = htons(port);
  return EventEngine::ResolvedAddress(
      reinterpret_cast<sockaddr*>(addr6),
      static_cast<socklen_t>(sizeof(sockaddr_in6)));
}

#define EXPECT_STATUS(result, status_code) \
  EXPECT_EQ((result).status().code(), absl::StatusCode::status_code)

}  // namespace

class EventEngineDNSTest : public EventEngineTest {
 protected:
  static void SetUpTestSuite() {
    // Invoke bazel's executeable links to the .sh and .py scripts (don't use
    // the .sh and .py suffixes) to make sure that we're using bazel's test
    // environment.
    std::string kPythonWrapper = "tools/distrib/python_wrapper";
    std::string kDNSServerPath = "test/cpp/naming/utils/dns_server";
    std::string kDNSResolverPath = "test/cpp/naming/utils/dns_resolver";
    std::string kTCPConnectPath = "test/cpp/naming/utils/tcp_connect";
    // HACK: Hyrum's law.
    if (!grpc_core::GetEnv("TEST_SRCDIR").has_value()) {
      // Invoke the .sh and .py scripts directly where they are in source code.
      kPythonWrapper += ".sh";
      kDNSServerPath += ".py";
      kDNSResolverPath += ".py";
      kTCPConnectPath += ".py";
    }
    // 1. launch dns_server
    int port = grpc_pick_unused_port_or_die();
    // <path to python wrapper> <path to dns_server.py> -p <port> -r <path to
    // records config>
    _dns_server.server_process = new grpc::SubProcess(
        {kPythonWrapper, kDNSServerPath, "-p", std::to_string(port), "-r",
         kDNSTestRecordGroupsYamlPath});
    _dns_server.port = port;

    // 2. wait until dns_server is up (health check)
    bool health_check_succeed = false;
    for (int i = 0; i < 10; i++) {
      // 2.1 tcp connect succeeds
      // <path to python wrapper> <path to tcp_connect.py> -s <hostname> -p
      // <port>
      grpc::SubProcess tcp_connect({kPythonWrapper, kTCPConnectPath, "-s",
                                    "localhost", "-p", std::to_string(port)});
      int status = tcp_connect.Join();
      // TODO(yijiem): make this portable for Windows
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        // 2.2 make an A-record query to dns_server
        // <path to python wrapper> <path to dns_resolver.py> -s <hostname> -p
        // <port> -n <domain name to query>
        std::string command = absl::StrJoin<absl::string_view>(
            {kPythonWrapper, kDNSResolverPath, "-s", "127.0.0.1", "-p",
             std::to_string(port), "-n", kHealthCheckRecordName},
            " ");
        // TODO(yijiem): make this portable for Windows
        FILE* f = popen(command.c_str(), "r");
        GPR_ASSERT(f != nullptr);
        char buf[128] = {};
        size_t res =
            fread(buf, sizeof(buf[0]), sizeof(buf) / sizeof(buf[0]) - 1, f);
        if (pclose(f) == 0 && res > 0) {
          absl::string_view sv(buf);
          if (sv.find("123.123.123.123") != sv.npos) {
            // finally
            gpr_log(
                GPR_INFO,
                "DNS server is up! Successfully reached it over UDP and TCP.");
            health_check_succeed = true;
            break;
          }
        }
      }
      absl::SleepFor(absl::Seconds(1));
    }
    ASSERT_TRUE(health_check_succeed);
  }

  static void TearDownTestSuite() {
    _dns_server.server_process->Interrupt();
    _dns_server.server_process->Join();
    delete _dns_server.server_process;
  }

 protected:
  class NotifyOnDestroy {
   public:
    explicit NotifyOnDestroy(grpc_core::Notification& dns_resolver_signal)
        : dns_resolver_signal_(dns_resolver_signal) {}
    ~NotifyOnDestroy() { dns_resolver_signal_.Notify(); }

   private:
    grpc_core::Notification& dns_resolver_signal_;
  };

  std::unique_ptr<EventEngine::DNSResolver> CreateDefaultDNSResolver() {
    std::shared_ptr<EventEngine> test_ee(this->NewEventEngine());
    EventEngine::DNSResolver::ResolverOptions options;
    options.dns_server = _dns_server.address();
    return test_ee->GetDNSResolver(options);
  }

  std::unique_ptr<EventEngine::DNSResolver>
  CreateDNSResolverWithNonResponsiveServer() {
    using FakeUdpAndTcpServer = grpc_core::testing::FakeUdpAndTcpServer;
    // Start up fake non responsive DNS server
    fake_dns_server_ = std::make_unique<FakeUdpAndTcpServer>(
        FakeUdpAndTcpServer::AcceptMode::kWaitForClientToSendFirstBytes,
        FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer);
    const std::string dns_server =
        absl::StrFormat("[::1]:%d", fake_dns_server_->port());
    std::shared_ptr<EventEngine> test_ee(this->NewEventEngine());
    EventEngine::DNSResolver::ResolverOptions options;
    options.dns_server = dns_server;
    return test_ee->GetDNSResolver(options);
  }

  std::unique_ptr<EventEngine::DNSResolver>
  CreateDNSResolverWithoutSpecifyingServer() {
    std::shared_ptr<EventEngine> test_ee(this->NewEventEngine());
    EventEngine::DNSResolver::ResolverOptions options;
    return test_ee->GetDNSResolver(options);
  }

  struct DNSServer {
    std::string address() { return "127.0.0.1:" + std::to_string(port); }
    int port;
    grpc::SubProcess* server_process;
  };
  grpc_core::Notification dns_resolver_signal_;

 private:
  static DNSServer _dns_server;
  std::unique_ptr<grpc_core::testing::FakeUdpAndTcpServer> fake_dns_server_;
};

EventEngineDNSTest::DNSServer EventEngineDNSTest::_dns_server;

TEST_F(EventEngineDNSTest, QueryNXHostname) {
  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupHostname(
      [this](auto result) {
        ASSERT_FALSE(result.ok());
        EXPECT_STATUS(result, kNotFound);
        dns_resolver_signal_.Notify();
      },
      "nonexisting-target.dns-test.event-engine.", /*default_port=*/"443",
      std::chrono::seconds(5));
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QueryWithIPLiteral) {
  constexpr uint8_t kExpectedAddresses[] = {4, 3, 2, 1};

  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupHostname(
      [&kExpectedAddresses, this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(*result,
                    Pointwise(ResolvedAddressEq(),
                              {MakeAddr4(kExpectedAddresses,
                                         sizeof(kExpectedAddresses), 1234)}));
        dns_resolver_signal_.Notify();
      },
      "4.3.2.1:1234",
      /*default_port=*/"", std::chrono::seconds(5));
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QueryARecord) {
  constexpr uint8_t kExpectedAddresses[][4] = {
      {1, 2, 3, 4}, {1, 2, 3, 5}, {1, 2, 3, 6}};

  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupHostname(
      [&kExpectedAddresses, this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(*result,
                    UnorderedPointwise(
                        ResolvedAddressEq(),
                        {MakeAddr4(kExpectedAddresses[0],
                                   sizeof(kExpectedAddresses[0]), 443),
                         MakeAddr4(kExpectedAddresses[1],
                                   sizeof(kExpectedAddresses[1]), 443),
                         MakeAddr4(kExpectedAddresses[2],
                                   sizeof(kExpectedAddresses[2]), 443)}));
        dns_resolver_signal_.Notify();
      },
      "ipv4-only-multi-target.dns-test.event-engine.",
      /*default_port=*/"443", std::chrono::seconds(5));
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QueryAAAARecord) {
  constexpr uint8_t kExpectedAddresses[][16] = {
      {0x26, 0x07, 0xf8, 0xb0, 0x40, 0x0a, 0x08, 0x01, 0, 0, 0, 0, 0, 0, 0x10,
       0x02},
      {0x26, 0x07, 0xf8, 0xb0, 0x40, 0x0a, 0x08, 0x01, 0, 0, 0, 0, 0, 0, 0x10,
       0x03},
      {0x26, 0x07, 0xf8, 0xb0, 0x40, 0x0a, 0x08, 0x01, 0, 0, 0, 0, 0, 0, 0x10,
       0x04}};

  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupHostname(
      [&kExpectedAddresses, this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(*result,
                    UnorderedPointwise(
                        ResolvedAddressEq(),
                        {MakeAddr6(kExpectedAddresses[0],
                                   sizeof(kExpectedAddresses[0]), 443),
                         MakeAddr6(kExpectedAddresses[1],
                                   sizeof(kExpectedAddresses[1]), 443),
                         MakeAddr6(kExpectedAddresses[2],
                                   sizeof(kExpectedAddresses[2]), 443)}));
        dns_resolver_signal_.Notify();
      },
      "ipv6-only-multi-target.dns-test.event-engine.:443",
      /*default_port=*/"", std::chrono::seconds(5));
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, TestAddressSorting) {
  constexpr uint8_t kExpectedAddresses[][16] = {
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
      {0x20, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x11, 0x11}};

  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupHostname(
      [&kExpectedAddresses, this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(
            *result,
            Pointwise(ResolvedAddressEq(),
                      {MakeAddr6(kExpectedAddresses[0],
                                 sizeof(kExpectedAddresses[0]), 1234),
                       MakeAddr6(kExpectedAddresses[1],
                                 sizeof(kExpectedAddresses[1]), 1234)}));
        dns_resolver_signal_.Notify();
      },
      "ipv6-loopback-preferred-target.dns-test.event-engine.:1234",
      /*default_port=*/"", std::chrono::seconds(5));
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QuerySRVRecord) {
  SRVRecord kExpectedRecords[2];
  kExpectedRecords[0].host = "ipv4-only-multi-target.dns-test.event-engine";
  kExpectedRecords[0].port = 1234;
  kExpectedRecords[0].priority = 0;
  kExpectedRecords[0].weight = 0;

  kExpectedRecords[1].host = "ipv6-only-multi-target.dns-test.event-engine";
  kExpectedRecords[1].port = 1234;
  kExpectedRecords[1].priority = 0;
  kExpectedRecords[1].weight = 0;

  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupSRV(
      [&kExpectedRecords, this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(*result, Pointwise(SRVRecordEq(), kExpectedRecords));
        dns_resolver_signal_.Notify();
      },
      "_grpclb._tcp.srv-multi-target.dns-test.event-engine.",
      std::chrono::seconds(5));
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QuerySRVRecordWithLocalhost) {
  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupSRV(
      [this](auto result) {
        ASSERT_FALSE(result.ok());
        EXPECT_STATUS(result, kUnknown);
        dns_resolver_signal_.Notify();
      },
      "localhost:1000", std::chrono::seconds(5));
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QueryTXTRecord) {
  // clang-format off
  const std::string kExpectedRecord =
      "grpc_config=[{"
        "\"serviceConfig\":{"
          "\"loadBalancingPolicy\":\"round_robin\","
          "\"methodConfig\":[{"
            "\"name\":[{"
              "\"method\":\"Foo\","
              "\"service\":\"SimpleService\""
            "}],"
            "\"waitForReady\":true"
          "}]"
        "}"
      "}]";
  // clang-format on

  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupTXT(
      [&kExpectedRecord, this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(*result, ElementsAre(kExpectedRecord));
        dns_resolver_signal_.Notify();
      },
      "_grpc_config.simple-service.dns-test.event-engine.",
      std::chrono::seconds(5));
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QueryTXTRecordWithLocalhost) {
  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupTXT(
      [this](auto result) {
        ASSERT_FALSE(result.ok());
        EXPECT_STATUS(result, kUnknown);
        dns_resolver_signal_.Notify();
      },
      "localhost:1000", std::chrono::seconds(5));
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, TestCancelActiveDNSQuery) {
  const std::string name = "dont-care-since-wont-be-resolved.test.com:1234";
  auto dns_resolver = CreateDNSResolverWithNonResponsiveServer();
  EventEngine::DNSResolver::LookupTaskHandle task_handle =
      dns_resolver->LookupHostname(
          [notify_on_destroy =
               std::make_unique<NotifyOnDestroy>(dns_resolver_signal_)](auto) {
            // Cancel should not execute on_resolve
            FAIL() << "This should not be reached";
          },
          name, "1234", std::chrono::minutes(1));
  EXPECT_TRUE(dns_resolver->CancelLookup(task_handle));
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, TestQueryTimeout) {
  const std::string name = "dont-care-since-wont-be-resolved.test.com.";
  auto dns_resolver = CreateDNSResolverWithNonResponsiveServer();
  dns_resolver->LookupTXT(
      [this](auto result) {
        EXPECT_FALSE(result.ok());
        EXPECT_STATUS(result, kDeadlineExceeded);
        dns_resolver_signal_.Notify();
      },
      name, std::chrono::seconds(3));  // timeout in 3 seconds
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, MultithreadedCancel) {
  const std::string name = "dont-care-since-wont-be-resolved.test.com:1234";
  auto dns_resolver = CreateDNSResolverWithNonResponsiveServer();
  constexpr int kNumOfThreads = 10;
  constexpr int kNumOfIterationsPerThread = 100;
  std::vector<std::thread> yarn;
  yarn.reserve(kNumOfThreads);
  for (int i = 0; i < kNumOfThreads; i++) {
    yarn.emplace_back([&name, dns_resolver = dns_resolver.get()] {
      for (int i = 0; i < kNumOfIterationsPerThread; i++) {
        grpc_core::Notification dns_resolver_signal;
        EventEngine::DNSResolver::LookupTaskHandle task_handle =
            dns_resolver->LookupHostname(
                [notify_on_destroy = std::make_unique<NotifyOnDestroy>(
                     dns_resolver_signal)](auto) {
                  // Cancel should not execute on_resolve
                  FAIL() << "This should not be reached";
                },
                name, "1234", std::chrono::minutes(1));
        EXPECT_TRUE(dns_resolver->CancelLookup(task_handle));
        dns_resolver_signal.WaitForNotification();
      }
    });
  }
  for (int i = 0; i < kNumOfThreads; i++) {
    yarn[i].join();
  }
}

constexpr EventEngine::Duration kDefaultDNSRequestTimeout =
    std::chrono::minutes(2);

#define EXPECT_SUCCESS()           \
  do {                             \
    EXPECT_TRUE(result.ok());      \
    EXPECT_FALSE(result->empty()); \
  } while (0)

// The following tests are almost 1-to-1 ported from
// test/core/iomgr/resolve_address_test.cc (except tests for the native DNS
// resolver and test that would race under the EventEngine semantics).

// START
TEST_F(EventEngineDNSTest, LocalHost) {
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_SUCCESS();
        dns_resolver_signal_.Notify();
      },
      "localhost:1", "", kDefaultDNSRequestTimeout);
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, DefaultPort) {
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_SUCCESS();
        dns_resolver_signal_.Notify();
      },
      "localhost", "1", kDefaultDNSRequestTimeout);
  dns_resolver_signal_.WaitForNotification();
}

// This test assumes the environment has an ipv6 loopback
TEST_F(EventEngineDNSTest, LocalhostResultHasIPv6First) {
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_TRUE(result.ok());
        EXPECT_TRUE(!result->empty() &&
                    (*result)[0].address()->sa_family == AF_INET6);
        dns_resolver_signal_.Notify();
      },
      "localhost:1", "", kDefaultDNSRequestTimeout);
  dns_resolver_signal_.WaitForNotification();
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

TEST_F(EventEngineDNSTest, LocalhostResultHasIPv4FirstWhenIPv6IsntAvalailable) {
  // Mock the kernel source address selection. Note that source addr factory
  // is reset to its default value during grpc initialization for each test.
  address_sorting_source_addr_factory* mock =
      new address_sorting_source_addr_factory();
  mock->vtable = &kMockIpv6DisabledSourceAddrFactoryVtable;
  address_sorting_override_source_addr_factory_for_testing(mock);
  // run the test
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_TRUE(result.ok());
        EXPECT_TRUE(!result->empty() &&
                    (*result)[0].address()->sa_family == AF_INET);
        dns_resolver_signal_.Notify();
      },
      "localhost:1", "", kDefaultDNSRequestTimeout);
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, NonNumericDefaultPort) {
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_SUCCESS();
        dns_resolver_signal_.Notify();
      },
      "localhost", "http", kDefaultDNSRequestTimeout);
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, MissingDefaultPort) {
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_FALSE(result.ok());
        dns_resolver_signal_.Notify();
      },
      "localhost", "", kDefaultDNSRequestTimeout);
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, IPv6WithPort) {
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_SUCCESS();
        dns_resolver_signal_.Notify();
      },
      "[2001:db8::1]:1", "", kDefaultDNSRequestTimeout);
  dns_resolver_signal_.WaitForNotification();
}

void TestIPv6WithoutPort(std::unique_ptr<EventEngine::DNSResolver> dns_resolver,
                         grpc_core::Notification* barrier,
                         absl::string_view target) {
  dns_resolver->LookupHostname(
      [barrier](auto result) {
        EXPECT_TRUE(result.ok());
        EXPECT_FALSE(result->empty());
        barrier->Notify();
      },
      target, "80", kDefaultDNSRequestTimeout);
  barrier->WaitForNotification();
}

TEST_F(EventEngineDNSTest, IPv6WithoutPortNoBrackets) {
  TestIPv6WithoutPort(CreateDNSResolverWithoutSpecifyingServer(),
                      &dns_resolver_signal_, "2001:db8::1");
}

TEST_F(EventEngineDNSTest, IPv6WithoutPortWithBrackets) {
  TestIPv6WithoutPort(CreateDNSResolverWithoutSpecifyingServer(),
                      &dns_resolver_signal_, "[2001:db8::1]");
}

TEST_F(EventEngineDNSTest, IPv6WithoutPortV4MappedV6) {
  TestIPv6WithoutPort(CreateDNSResolverWithoutSpecifyingServer(),
                      &dns_resolver_signal_, "2001:db8::1.2.3.4");
}

void TestInvalidIPAddress(
    std::unique_ptr<EventEngine::DNSResolver> dns_resolver,
    grpc_core::Notification* barrier, absl::string_view target) {
  dns_resolver->LookupHostname(
      [barrier](auto result) {
        EXPECT_FALSE(result.ok());
        barrier->Notify();
      },
      target, "", kDefaultDNSRequestTimeout);
  barrier->WaitForNotification();
}

TEST_F(EventEngineDNSTest, InvalidIPv4Addresses) {
  TestInvalidIPAddress(CreateDNSResolverWithoutSpecifyingServer(),
                       &dns_resolver_signal_, "293.283.1238.3:1");
}

TEST_F(EventEngineDNSTest, InvalidIPv6Addresses) {
  TestInvalidIPAddress(CreateDNSResolverWithoutSpecifyingServer(),
                       &dns_resolver_signal_, "[2001:db8::11111]:1");
}

void TestUnparseableHostPort(
    std::unique_ptr<EventEngine::DNSResolver> dns_resolver,
    grpc_core::Notification* barrier, absl::string_view target) {
  dns_resolver->LookupHostname(
      [barrier](auto result) {
        EXPECT_FALSE(result.ok());
        barrier->Notify();
      },
      target, "1", kDefaultDNSRequestTimeout);
  barrier->WaitForNotification();
}

TEST_F(EventEngineDNSTest, UnparseableHostPortsOnlyBracket) {
  TestUnparseableHostPort(CreateDNSResolverWithoutSpecifyingServer(),
                          &dns_resolver_signal_, "[");
}

TEST_F(EventEngineDNSTest, UnparseableHostPortsMissingRightBracket) {
  TestUnparseableHostPort(CreateDNSResolverWithoutSpecifyingServer(),
                          &dns_resolver_signal_, "[::1");
}

TEST_F(EventEngineDNSTest, UnparseableHostPortsBadPort) {
  TestUnparseableHostPort(CreateDNSResolverWithoutSpecifyingServer(),
                          &dns_resolver_signal_, "[::1]bad");
}

TEST_F(EventEngineDNSTest, UnparseableHostPortsBadIPv6) {
  TestUnparseableHostPort(CreateDNSResolverWithoutSpecifyingServer(),
                          &dns_resolver_signal_, "[1.2.3.4]");
}

TEST_F(EventEngineDNSTest, UnparseableHostPortsBadLocalhost) {
  TestUnparseableHostPort(CreateDNSResolverWithoutSpecifyingServer(),
                          &dns_resolver_signal_, "[localhost]");
}

TEST_F(EventEngineDNSTest, UnparseableHostPortsBadLocalhostWithPort) {
  TestUnparseableHostPort(CreateDNSResolverWithoutSpecifyingServer(),
                          &dns_resolver_signal_, "[localhost]:1");
}

// Attempt to cancel a request after it has completed.
TEST_F(EventEngineDNSTest, CancelDoesNotSucceed) {
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  auto handle = dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_SUCCESS();
        dns_resolver_signal_.Notify();
      },
      "localhost:1", "", kDefaultDNSRequestTimeout);
  dns_resolver_signal_.WaitForNotification();
  ASSERT_FALSE(dns_resolver->CancelLookup(handle));
}
// END

#endif  // GPR_WINDOWS
