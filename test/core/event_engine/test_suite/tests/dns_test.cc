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

#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <ratio>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "test/core/event_engine/test_suite/event_engine_test_framework.h"
#include "test/core/util/port.h"
#include "test/core/util/subprocess.h"

namespace grpc_event_engine {
namespace experimental {

void InitDNSTests() {}

}  // namespace experimental
}  // namespace grpc_event_engine

namespace {

constexpr char kDNSTestRecordGroupsYamlPath[] =
    "test/core/event_engine/test_suite/tests/dns_test_record_groups.yaml";
constexpr char kPythonWrapper[] =
    "test/core/event_engine/test_suite/tests/python_wrapper.sh";
constexpr char kDNSServerPath[] = "test/cpp/naming/utils/dns_server.py";
constexpr char kDNSResolverPath[] = "test/cpp/naming/utils/dns_resolver.py";
constexpr char kTCPConnectPath[] = "test/cpp/naming/utils/tcp_connect.py";
constexpr char kHealthCheckRecordName[] =
    "health-check-local-dns-server-is-alive.resolver-tests.grpctestingexp";

}  // namespace

class EventEngineDNSTest : public EventEngineTest {
 public:
  static void SetUpTestSuite() {
    // 1. launch dns_server
    int port = grpc_pick_unused_port();
    ASSERT_NE(port, 0)
        << "pick unused port failed, maybe the port server is not running?";
    // <path to python wrapper> <path to dns_server.py> -p <port> -r <path to
    // records config>
    const char* argv[6];
    argv[0] = kPythonWrapper;
    argv[1] = kDNSServerPath;
    argv[2] = "-p";
    char* server_port_str;
    gpr_asprintf(&server_port_str, "%d", port);
    argv[3] = server_port_str;
    argv[4] = "-r";
    argv[5] = kDNSTestRecordGroupsYamlPath;
    // TODO(yijiem):  use test/cpp/util/subprocess.h
    _dns_server.server_process =
        gpr_subprocess_create(sizeof(argv) / sizeof(argv[0]), argv);
    GPR_ASSERT(_dns_server.server_process);
    _dns_server.port = port;

    // 2. wait until dns_server is up (health check)
    bool health_check_succeed = false;
    for (int i = 0; i < 10; i++) {
      // 2.1 tcp connect succeeds
      // <path to python wrapper> <path to tcp_connect.py> -s <hostname> -p
      // <port>
      const char* argv[6];
      argv[0] = kPythonWrapper;
      argv[1] = kTCPConnectPath;
      argv[2] = "-s";
      argv[3] = "localhost";
      argv[4] = "-p";
      argv[5] = server_port_str;
      gpr_subprocess* tcp_connect =
          gpr_subprocess_create(sizeof(argv) / sizeof(argv[0]), argv);
      GPR_ASSERT(tcp_connect);
      if (gpr_subprocess_join(tcp_connect) == 0) {
        // 2.2 make an A-record query to dns_server
        // <path to python wrapper> <path to dns_resolver.py> -s <hostname> -p
        // <port> -n <domain name to query>
        std::string command =
            absl::StrJoin({kPythonWrapper, kDNSResolverPath, "-s", "127.0.0.1",
                           "-p", const_cast<const char*>(server_port_str), "-n",
                           kHealthCheckRecordName},
                          " ");
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
    gpr_free(server_port_str);
    ASSERT_TRUE(health_check_succeed);
  }

  static void TearDownTestSuite() {
    gpr_subprocess_interrupt(_dns_server.server_process);
    gpr_subprocess_join(_dns_server.server_process);
  }

 protected:
  struct DNSServer {
    std::string address() { return "127.0.0.1:" + std::to_string(port); }
    int port;
    gpr_subprocess* server_process;
  };
  static DNSServer _dns_server;
};

EventEngineDNSTest::DNSServer EventEngineDNSTest::_dns_server;

namespace {

using grpc_event_engine::experimental::EventEngine;
using testing::Pointwise;

MATCHER(ResolvedAddressEq, "") {
  const auto& addr0 = std::get<0>(arg);
  const auto& addr1 = std::get<1>(arg);
  return addr0.size() == addr1.size() &&
         memcmp(addr0.address(), addr1.address(), addr0.size()) == 0;
}

// Copied from tcp_socket_utils_test.cc
// TODO(yijiem): maybe make common test util
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

}  // namespace

TEST_F(EventEngineDNSTest, QueryARecord) {
  constexpr uint8_t kExpectedAddresses[][4] = {
      {1, 2, 3, 4}, {1, 2, 3, 5}, {1, 2, 3, 6}};

  grpc_core::ExecCtx exec_ctx;
  std::shared_ptr<EventEngine> test_ee(this->NewEventEngine());
  std::unique_ptr<EventEngine::DNSResolver> dns_resolver =
      test_ee->GetDNSResolver({.dns_server = _dns_server.address()});
  grpc_core::Notification dns_resolver_signal;
  dns_resolver->LookupHostname(
      [&dns_resolver_signal, kExpectedAddresses](
          absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> result) {
        ASSERT_TRUE(result.ok());
        EXPECT_THAT(*result,
                    Pointwise(ResolvedAddressEq(),
                              {MakeAddr4(kExpectedAddresses[0],
                                         sizeof(kExpectedAddresses[0]), 443),
                               MakeAddr4(kExpectedAddresses[1],
                                         sizeof(kExpectedAddresses[1]), 443),
                               MakeAddr4(kExpectedAddresses[2],
                                         sizeof(kExpectedAddresses[2]), 443)}));
        // TODO(yijiem): unblock
        dns_resolver_signal.Notify();
      },
      "ipv4-only-multi-target.dns-test.event-engine.",
      /*default_port=*/"443", std::chrono::seconds(1));
  dns_resolver_signal.WaitForNotification();
}
TEST_F(EventEngineDNSTest, QueryAAAARecord) { grpc_core::ExecCtx exec_ctx; }
TEST_F(EventEngineDNSTest, QueryHostnameRecord) { grpc_core::ExecCtx exec_ctx; }
TEST_F(EventEngineDNSTest, QuerySRVRecord) { grpc_core::ExecCtx exec_ctx; }
TEST_F(EventEngineDNSTest, QueryTXTRecord) { grpc_core::ExecCtx exec_ctx; }
