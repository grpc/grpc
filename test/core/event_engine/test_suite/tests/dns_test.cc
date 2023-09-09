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

#include <grpc/support/port_platform.h>

#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "test/core/event_engine/test_suite/event_engine_test_framework.h"
#include "test/core/util/fake_udp_and_tcp_server.h"
#include "test/core/util/port.h"
#include "test/cpp/util/get_grpc_test_runfile_dir.h"
#include "test/cpp/util/subprocess.h"

#ifdef GPR_WINDOWS
#include "test/cpp/util/windows/manifest_file.h"
#endif  // GPR_WINDOWS

namespace grpc_event_engine {
namespace experimental {

void InitDNSTests() {}

}  // namespace experimental
}  // namespace grpc_event_engine

namespace {

using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::URIToResolvedAddress;
using SRVRecord = EventEngine::DNSResolver::SRVRecord;
using testing::ElementsAre;
using testing::Pointwise;
using testing::SizeIs;
using testing::UnorderedPointwise;

// TODO(yijiem): make this portable for Windows
constexpr char kDNSTestRecordGroupsYamlPath[] =
    "test/core/event_engine/test_suite/tests/dns_test_record_groups.yaml";
// Invoke bazel's executable links to the .sh and .py scripts (don't use
// the .sh and .py suffixes) to make sure that we're using bazel's test
// environment.
constexpr char kDNSServerRelPath[] = "test/cpp/naming/utils/dns_server";
constexpr char kDNSResolverRelPath[] = "test/cpp/naming/utils/dns_resolver";
constexpr char kTCPConnectRelPath[] = "test/cpp/naming/utils/tcp_connect";
constexpr char kHealthCheckRelPath[] = "test/cpp/naming/utils/health_check";

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

}  // namespace

class EventEngineDNSTest : public EventEngineTest {
 protected:
  static void SetUpTestSuite() {
#ifndef GRPC_IOS_EVENT_ENGINE_CLIENT
    std::string test_records_path = kDNSTestRecordGroupsYamlPath;
    std::string dns_server_path = kDNSServerRelPath;
    std::string dns_resolver_path = kDNSResolverRelPath;
    std::string tcp_connect_path = kTCPConnectRelPath;
    std::string health_check_path = kHealthCheckRelPath;
    absl::optional<std::string> runfile_dir = grpc::GetGrpcTestRunFileDir();
    if (runfile_dir.has_value()) {
      test_records_path = absl::StrJoin({*runfile_dir, test_records_path}, "/");
      dns_server_path = absl::StrJoin({*runfile_dir, dns_server_path}, "/");
      dns_resolver_path = absl::StrJoin({*runfile_dir, dns_resolver_path}, "/");
      tcp_connect_path = absl::StrJoin({*runfile_dir, tcp_connect_path}, "/");
      health_check_path = absl::StrJoin({*runfile_dir, health_check_path}, "/");
#ifdef GPR_WINDOWS
// TODO(yijiem): Misusing the GRPC_PORT_ISOLATED_RUNTIME preprocessor symbol as
// an indication whether the test is running on RBE or not. Find a better way of
// doing this.
#ifndef GRPC_PORT_ISOLATED_RUNTIME
      gpr_log(GPR_ERROR,
              "You are invoking the test locally with Bazel, you may need to "
              "invoke Bazel with --enable_runfiles=yes.");
#endif  // GRPC_PORT_ISOLATED_RUNTIME
      test_records_path = grpc::testing::NormalizeFilePath(test_records_path);
      dns_server_path =
          grpc::testing::NormalizeFilePath(dns_server_path + ".exe");
      dns_resolver_path =
          grpc::testing::NormalizeFilePath(dns_resolver_path + ".exe");
      tcp_connect_path =
          grpc::testing::NormalizeFilePath(tcp_connect_path + ".exe");
      health_check_path =
          grpc::testing::NormalizeFilePath(health_check_path + ".exe");
      std::cout << test_records_path << std::endl;
      std::cout << dns_server_path << std::endl;
      std::cout << dns_resolver_path << std::endl;
      std::cout << tcp_connect_path << std::endl;
      std::cout << health_check_path << std::endl;
#endif  // GPR_WINDOWS
    } else {
#ifdef GPR_WINDOWS
      grpc_core::Crash(
          "The EventEngineDNSTest does not support running without Bazel on "
          "Windows for now.");
#endif  // GPR_WINDOWS
      // Invoke the .py scripts directly where they are in source code if we are
      // not running with bazel.
      dns_server_path += ".py";
      dns_resolver_path += ".py";
      tcp_connect_path += ".py";
      health_check_path += ".py";
    }
    // 1. launch dns_server
    int port = grpc_pick_unused_port_or_die();
    // <path to dns_server.py> -p <port> -r <path to records config>
    dns_server_.server_process = new grpc::SubProcess(
        {dns_server_path, "-p", std::to_string(port), "-r", test_records_path});
    dns_server_.port = port;

    // 2. wait until dns_server is up (health check)
    grpc::SubProcess health_check({
        health_check_path,
        "-p",
        std::to_string(port),
        "--dns_resolver_bin_path",
        dns_resolver_path,
        "--tcp_connect_bin_path",
        tcp_connect_path,
    });
    int status = health_check.Join();
#ifdef GPR_WINDOWS
    ASSERT_EQ(status, 0);
#else
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif  // GPR_WINDOWS
#endif  // GRPC_IOS_EVENT_ENGINE_CLIENT
  }

  static void TearDownTestSuite() {
#ifndef GRPC_IOS_EVENT_ENGINE_CLIENT
    dns_server_.server_process->Interrupt();
    dns_server_.server_process->Join();
    delete dns_server_.server_process;
#endif  // GRPC_IOS_EVENT_ENGINE_CLIENT
  }

  std::unique_ptr<EventEngine::DNSResolver> CreateDefaultDNSResolver() {
    std::shared_ptr<EventEngine> test_ee(this->NewEventEngine());
    EventEngine::DNSResolver::ResolverOptions options;
    options.dns_server = dns_server_.address();
    return *test_ee->GetDNSResolver(options);
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
    return *test_ee->GetDNSResolver(options);
  }

  std::unique_ptr<EventEngine::DNSResolver>
  CreateDNSResolverWithoutSpecifyingServer() {
    std::shared_ptr<EventEngine> test_ee(this->NewEventEngine());
    EventEngine::DNSResolver::ResolverOptions options;
    return *test_ee->GetDNSResolver(options);
  }

  struct DNSServer {
    std::string address() { return "127.0.0.1:" + std::to_string(port); }
    int port;
    grpc::SubProcess* server_process;
  };
  grpc_core::Notification dns_resolver_signal_;

 private:
  static DNSServer dns_server_;
  std::unique_ptr<grpc_core::testing::FakeUdpAndTcpServer> fake_dns_server_;
};

EventEngineDNSTest::DNSServer EventEngineDNSTest::dns_server_;

// TODO(hork): implement XFAIL for resolvers that don't support TXT or SRV
#ifndef GRPC_IOS_EVENT_ENGINE_CLIENT

TEST_F(EventEngineDNSTest, QueryNXHostname) {
  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupHostname(
      [this](auto result) {
        ASSERT_FALSE(result.ok());
        EXPECT_EQ(result.status(),
                  absl::NotFoundError("address lookup failed for "
                                      "nonexisting-target.dns-test.event-"
                                      "engine.: Domain name not found"));
        dns_resolver_signal_.Notify();
      },
      "nonexisting-target.dns-test.event-engine.", /*default_port=*/"443");
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QueryWithIPLiteral) {
  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupHostname(
      [this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(*result,
                    Pointwise(ResolvedAddressEq(),
                              {*URIToResolvedAddress("ipv4:4.3.2.1:1234")}));
        dns_resolver_signal_.Notify();
      },
      "4.3.2.1:1234",
      /*default_port=*/"");
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QueryARecord) {
  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupHostname(
      [this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(*result, UnorderedPointwise(
                                 ResolvedAddressEq(),
                                 {*URIToResolvedAddress("ipv4:1.2.3.4:443"),
                                  *URIToResolvedAddress("ipv4:1.2.3.5:443"),
                                  *URIToResolvedAddress("ipv4:1.2.3.6:443")}));
        dns_resolver_signal_.Notify();
      },
      "ipv4-only-multi-target.dns-test.event-engine.",
      /*default_port=*/"443");
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QueryAAAARecord) {
  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupHostname(
      [this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(
            *result,
            UnorderedPointwise(
                ResolvedAddressEq(),
                {*URIToResolvedAddress("ipv6:[2607:f8b0:400a:801::1002]:443"),
                 *URIToResolvedAddress("ipv6:[2607:f8b0:400a:801::1003]:443"),
                 *URIToResolvedAddress(
                     "ipv6:[2607:f8b0:400a:801::1004]:443")}));
        dns_resolver_signal_.Notify();
      },
      "ipv6-only-multi-target.dns-test.event-engine.:443",
      /*default_port=*/"");
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, TestAddressSorting) {
  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupHostname(
      [this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(
            *result,
            Pointwise(ResolvedAddressEq(),
                      {*URIToResolvedAddress("ipv6:[::1]:1234"),
                       *URIToResolvedAddress("ipv6:[2002::1111]:1234")}));
        dns_resolver_signal_.Notify();
      },
      "ipv6-loopback-preferred-target.dns-test.event-engine.:1234",
      /*default_port=*/"");
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QuerySRVRecord) {
  const SRVRecord kExpectedRecords[] = {
      {/*host=*/"ipv4-only-multi-target.dns-test.event-engine", /*port=*/1234,
       /*priority=*/0, /*weight=*/0},
      {"ipv6-only-multi-target.dns-test.event-engine", 1234, 0, 0},
  };

  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupSRV(
      [&kExpectedRecords, this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(*result, Pointwise(SRVRecordEq(), kExpectedRecords));
        dns_resolver_signal_.Notify();
      },
      "_grpclb._tcp.srv-multi-target.dns-test.event-engine.");
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QuerySRVRecordWithLocalhost) {
  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupSRV(
      [this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(*result, SizeIs(0));
        dns_resolver_signal_.Notify();
      },
      "localhost:1000");
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
        EXPECT_THAT(*result,
                    ElementsAre(kExpectedRecord, "other_config=other config"));
        dns_resolver_signal_.Notify();
      },
      "_grpc_config.simple-service.dns-test.event-engine.");
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, QueryTXTRecordWithLocalhost) {
  auto dns_resolver = CreateDefaultDNSResolver();
  dns_resolver->LookupTXT(
      [this](auto result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(*result, SizeIs(0));
        dns_resolver_signal_.Notify();
      },
      "localhost:1000");
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, TestCancelActiveDNSQuery) {
  const std::string name = "dont-care-since-wont-be-resolved.test.com:1234";
  auto dns_resolver = CreateDNSResolverWithNonResponsiveServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        ASSERT_FALSE(result.ok());
        EXPECT_EQ(result.status(),
                  absl::CancelledError("address lookup failed for "
                                       "dont-care-since-wont-be-resolved.test."
                                       "com:1234: DNS query cancelled"));
        dns_resolver_signal_.Notify();
      },
      name, "1234");
  dns_resolver.reset();
  dns_resolver_signal_.WaitForNotification();
}
#endif  // GRPC_IOS_EVENT_ENGINE_CLIENT

#define EXPECT_SUCCESS()           \
  do {                             \
    EXPECT_TRUE(result.ok());      \
    EXPECT_FALSE(result->empty()); \
  } while (0)

// The following tests are almost 1-to-1 ported from
// test/core/iomgr/resolve_address_test.cc (except tests for the native DNS
// resolver and tests that would not make sense using the
// EventEngine::DNSResolver API).

// START
TEST_F(EventEngineDNSTest, LocalHost) {
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_SUCCESS();
        dns_resolver_signal_.Notify();
      },
      "localhost:1", "");
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, DefaultPort) {
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_SUCCESS();
        dns_resolver_signal_.Notify();
      },
      "localhost", "1");
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
      "localhost:1", "");
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, NonNumericDefaultPort) {
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_SUCCESS();
        dns_resolver_signal_.Notify();
      },
      "localhost", "http");
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, MissingDefaultPort) {
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_FALSE(result.ok());
        dns_resolver_signal_.Notify();
      },
      "localhost", "");
  dns_resolver_signal_.WaitForNotification();
}

TEST_F(EventEngineDNSTest, IPv6WithPort) {
  auto dns_resolver = CreateDNSResolverWithoutSpecifyingServer();
  dns_resolver->LookupHostname(
      [this](auto result) {
        EXPECT_SUCCESS();
        dns_resolver_signal_.Notify();
      },
      "[2001:db8::1]:1", "");
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
      target, "80");
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
      target, "");
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
      target, "1");
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
// END
