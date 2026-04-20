// Copyright 2023 gRPC authors.
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

#include <grpc/support/port_platform.h>

#ifdef GPR_APPLE

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

#include <thread>

#include "src/core/lib/event_engine/cf_engine/cf_engine.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/grpc_check.h"
#include "test/core/test_util/port.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

using namespace std::chrono_literals;

namespace grpc_event_engine {
namespace experimental {

TEST(CFEventEngineTest, TestConnectionTimeout) {
  // use a non-routable IP so connection will timeout
  auto resolved_addr = URIToResolvedAddress("ipv4:8.8.8.8:1234");
  GRPC_CHECK_OK(resolved_addr);

  grpc_core::MemoryQuota memory_quota(
      grpc_core::MakeRefCounted<grpc_core::channelz::ResourceQuotaNode>(
          "cf_engine_test"));
  grpc_core::Notification client_signal;
  auto cf_engine = std::make_shared<CFEventEngine>();

  ChannelArgsEndpointConfig config(grpc_core::ChannelArgs().Set(
      GRPC_ARG_RESOURCE_QUOTA, grpc_core::ResourceQuota::Default()));
  cf_engine->Connect(
      [&client_signal](auto endpoint) {
        // EXPECT_EQ(endpoint.status().code(),
        //           absl::StatusCode::kDeadlineExceeded);
        LOG(INFO) << "Connection status: " << endpoint.status().ToString();
        client_signal.Notify();
      },
      *resolved_addr, config, memory_quota.CreateMemoryAllocator("conn1"), 1ms);

  client_signal.WaitForNotification();
}

TEST(CFEventEngineTest, TestConnectionCancelled) {
  // use a non-routable IP so to cancel connection before timeout
  auto resolved_addr = URIToResolvedAddress("ipv4:8.8.8.8:1234");
  GRPC_CHECK_OK(resolved_addr);

  grpc_core::MemoryQuota memory_quota(
      grpc_core::MakeRefCounted<grpc_core::channelz::ResourceQuotaNode>(
          "cf_engine_test"));
  grpc_core::Notification client_signal;
  auto cf_engine = std::make_shared<CFEventEngine>();

  ChannelArgsEndpointConfig config(grpc_core::ChannelArgs().Set(
      GRPC_ARG_RESOURCE_QUOTA, grpc_core::ResourceQuota::Default()));
  auto conn_handle = cf_engine->Connect(
      [&client_signal](auto endpoint) {
        // EXPECT_EQ(endpoint.status().code(), absl::StatusCode::kCancelled);
        LOG(INFO) << "Connection status: " << endpoint.status().ToString();
        client_signal.Notify();
      },
      *resolved_addr, config, memory_quota.CreateMemoryAllocator("conn1"), 1h);

  cf_engine->CancelConnect(conn_handle);
  client_signal.WaitForNotification();
}

namespace {
std::vector<std::string> ResolvedAddressesToStrings(
    const std::vector<EventEngine::ResolvedAddress> addresses) {
  std::vector<std::string> ip_strings;
  std::transform(addresses.cbegin(), addresses.cend(),
                 std::back_inserter(ip_strings), [](auto const& address) {
                   return ResolvedAddressToString(address).value_or("ERROR");
                 });
  return ip_strings;
}

// Performs a DNS lookup with retries for transient kNotFound failures.
// kNotFound is returned by DNSServiceResolverImpl::ResolveCallback when the
// DNS server was reachable but both A and AAAA queries returned
// kDNSServiceErr_NoSuchRecord. This can happen transiently when the Mac test
// machine's upstream resolver cannot reach the authoritative DNS servers for
// external services like sslip.io or nip.io (e.g. due to network restrictions
// in the Mac CI pool). It is safe to retry only on kNotFound because that code
// is never produced by a bug in the resolver implementation — any parameter or
// internal errors map to kUnknown instead.
absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> LookupWithRetry(
    std::shared_ptr<CFEventEngine> engine, absl::string_view name,
    absl::string_view default_port, int max_attempts = 3) {
  absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> result;
  for (int i = 0; i < max_attempts; ++i) {
    grpc_core::Notification signal;
    engine->GetDNSResolver({}).value()->LookupHostname(
        [&signal, &result](auto r) {
          result = std::move(r);
          signal.Notify();
        },
        name, default_port);
    signal.WaitForNotification();
    if (result.ok() ||
        result.status().code() != absl::StatusCode::kNotFound) {
      break;
    }
  }
  return result;
}
}  // namespace

TEST(CFEventEngineTest, TestCreateDNSResolver) {
  grpc_core::MemoryQuota memory_quota(
      grpc_core::MakeRefCounted<grpc_core::channelz::ResourceQuotaNode>(
          "cf_engine_test"));
  auto cf_engine = std::make_shared<CFEventEngine>();

  EXPECT_TRUE(cf_engine->GetDNSResolver({}).status().ok());
  EXPECT_TRUE(cf_engine->GetDNSResolver({.dns_server = ""}).status().ok());
  EXPECT_EQ(
      cf_engine->GetDNSResolver({.dns_server = "8.8.8.8"}).status().code(),
      absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      cf_engine->GetDNSResolver({.dns_server = "8.8.8.8:53"}).status().code(),
      absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      cf_engine->GetDNSResolver({.dns_server = "invalid"}).status().code(),
      absl::StatusCode::kInvalidArgument);
}

TEST(CFEventEngineTest, TestResolveLocalhost) {
  grpc_core::Notification resolve_signal;

  auto cf_engine = std::make_shared<CFEventEngine>();
  auto dns_resolver = cf_engine->GetDNSResolver({});

  dns_resolver.value()->LookupHostname(
      [&resolve_signal](auto result) {
        EXPECT_TRUE(result.status().ok());
        EXPECT_THAT(ResolvedAddressesToStrings(result.value()),
                    testing::UnorderedElementsAre("127.0.0.1:80", "[::1]:80"));

        resolve_signal.Notify();
      },
      "localhost", "80");

  resolve_signal.WaitForNotification();
}

TEST(CFEventEngineTest, TestResolveRemote) {
  auto cf_engine = std::make_shared<CFEventEngine>();
  auto result = LookupWithRetry(cf_engine, "localtest.me:80", "443");
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(ResolvedAddressesToStrings(result.value()),
              testing::UnorderedElementsAre("127.0.0.1:80", "[::1]:80"));
}

TEST(CFEventEngineTest, TestResolveIPv4Remote) {
  auto cf_engine = std::make_shared<CFEventEngine>();
  auto result = LookupWithRetry(cf_engine, "1.2.3.4.nip.io:80", "");
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(ResolvedAddressesToStrings(result.value()),
              testing::IsSubsetOf({"1.2.3.4:80", "[64:ff9b::102:304]:80" /*NAT64*/}));
}

TEST(CFEventEngineTest, TestResolveIPv6Remote) {
  auto cf_engine = std::make_shared<CFEventEngine>();
  auto result = LookupWithRetry(cf_engine, "2607-f8b0-400a-801--1002.sslip.io.", "80");
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(ResolvedAddressesToStrings(result.value()),
              testing::UnorderedElementsAre("[2607:f8b0:400a:801::1002]:80"));
}

TEST(CFEventEngineTest, TestResolveIPv4Literal) {
  grpc_core::Notification resolve_signal;

  auto cf_engine = std::make_shared<CFEventEngine>();
  auto dns_resolver = cf_engine->GetDNSResolver({});

  dns_resolver.value()->LookupHostname(
      [&resolve_signal](auto result) {
        EXPECT_TRUE(result.status().ok());
        EXPECT_THAT(ResolvedAddressesToStrings(result.value()),
                    testing::UnorderedElementsAre("1.2.3.4:443"));

        resolve_signal.Notify();
      },
      "1.2.3.4", "https");

  resolve_signal.WaitForNotification();
}

TEST(CFEventEngineTest, TestResolveIPv6Literal) {
  grpc_core::Notification resolve_signal;

  auto cf_engine = std::make_shared<CFEventEngine>();
  auto dns_resolver = cf_engine->GetDNSResolver({});

  dns_resolver.value()->LookupHostname(
      [&resolve_signal](auto result) {
        EXPECT_TRUE(result.status().ok());
        EXPECT_THAT(
            ResolvedAddressesToStrings(result.value()),
            testing::UnorderedElementsAre("[2607:f8b0:400a:801::1002]:443"));

        resolve_signal.Notify();
      },
      "[2607:f8b0:400a:801::1002]", "443");

  resolve_signal.WaitForNotification();
}

TEST(CFEventEngineTest, TestResolveNoRecord) {
  grpc_core::Notification resolve_signal;
  auto cf_engine = std::make_shared<CFEventEngine>();
  auto dns_resolver = std::move(cf_engine->GetDNSResolver({})).value();

  dns_resolver->LookupHostname(
      [&resolve_signal](auto result) {
        EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);

        resolve_signal.Notify();
      },
      "nonexisting-target.dns-test.event-engine.", "443");

  resolve_signal.WaitForNotification();
}

TEST(CFEventEngineTest, TestResolveCanceled) {
  grpc_core::Notification resolve_signal;
  auto cf_engine = std::make_shared<CFEventEngine>();
  auto dns_resolver = std::move(cf_engine->GetDNSResolver({})).value();

  dns_resolver->LookupHostname(
      [&resolve_signal](auto result) {
        // query may have already finished before canceling, only verity the
        // code if status is not ok
        if (!result.status().ok()) {
          EXPECT_EQ(result.status().code(), absl::StatusCode::kCancelled);
        }

        resolve_signal.Notify();
      },
      "dont-care-since-wont-be-resolved.localtest.me", "443");

  dns_resolver.reset();
  resolve_signal.WaitForNotification();
}

TEST(CFEventEngineTest, TestResolveAgainInCallback) {
  std::atomic<int> times{2};
  grpc_core::Notification resolve_signal;

  auto cf_engine = std::make_shared<CFEventEngine>();
  auto dns_resolver = std::move(cf_engine->GetDNSResolver({})).value();

  dns_resolver->LookupHostname(
      [&resolve_signal, &times, &dns_resolver](auto result) {
        EXPECT_TRUE(result.status().ok());
        EXPECT_THAT(ResolvedAddressesToStrings(result.value()),
                    testing::UnorderedElementsAre("127.0.0.1:80", "[::1]:80"));

        dns_resolver->LookupHostname(
            [&resolve_signal, &times](auto result) {
              EXPECT_TRUE(result.status().ok());
              EXPECT_THAT(
                  ResolvedAddressesToStrings(result.value()),
                  testing::UnorderedElementsAre("127.0.0.1:443", "[::1]:443"));

              if (--times == 0) {
                resolve_signal.Notify();
              }
            },
            "localhost", "443");

        if (--times == 0) {
          resolve_signal.Notify();
        }
      },
      "localhost", "80");

  resolve_signal.WaitForNotification();
}

TEST(CFEventEngineTest, TestLockOrder) {
  auto cf_engine = std::make_shared<CFEventEngine>();
  auto dns_resolver = std::move(cf_engine->GetDNSResolver({})).value();
  grpc_core::Mutex mutex;

  {
    grpc_core::MutexLock lock(&mutex);
    dns_resolver->LookupHostname(
        [&mutex](auto result) { grpc_core::MutexLock lock2(&mutex); },
        "google.com", "80");
  }

  dns_resolver.reset();

  sleep(1);
}

TEST(CFEventEngineTest, TestResolveMany) {
  std::atomic<int> times{10};
  grpc_core::Notification resolve_signal;
  auto cf_engine = std::make_shared<CFEventEngine>();
  auto dns_resolver = std::move(cf_engine->GetDNSResolver({})).value();

  for (int i = times; i >= 1; --i) {
    dns_resolver->LookupHostname(
        [&resolve_signal, &times, i](auto result) {
          if (result.status().ok()) {
            EXPECT_THAT(
                ResolvedAddressesToStrings(result.value()),
                testing::IsSubsetOf(
                    {absl::StrFormat("100.0.0.%d:443", i),
                     absl::StrFormat("[64:ff9b::6400:%x]:443", i) /*NAT64*/}));
          } else {
            // Sometimes due to transient network issues, the test may
            // not be able to reach the DNS server that resolves nip.io
            // ip addresses. In those cases a NotFound error should be returned.
            // The correct fix would be to add these ip addresses to /etc/hosts
            // for deterministic resolution but the tests do not have permission
            // to edit this file.
            EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
          }

          if (--times == 0) {
            resolve_signal.Notify();
          }
        },
        absl::StrFormat("100.0.0.%d.nip.io", i), "443");
  }

  resolve_signal.WaitForNotification();
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int status = RUN_ALL_TESTS();
  grpc_shutdown();
  return status;
}

#else  // not GPR_APPLE
int main(int /* argc */, char** /* argv */) { return 0; }
#endif
