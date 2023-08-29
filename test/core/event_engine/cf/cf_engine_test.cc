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

#include <thread>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

#include "src/core/lib/event_engine/cf_engine/cf_engine.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/util/port.h"

using namespace std::chrono_literals;

namespace grpc_event_engine {
namespace experimental {

TEST(CFEventEngineTest, TestConnectionTimeout) {
  // use a non-routable IP so connection will timeout
  auto resolved_addr = URIToResolvedAddress("ipv4:10.255.255.255:1234");
  GPR_ASSERT(resolved_addr.ok());

  grpc_core::MemoryQuota memory_quota("cf_engine_test");
  grpc_core::Notification client_signal;
  auto cf_engine = std::make_shared<CFEventEngine>();

  ChannelArgsEndpointConfig config(grpc_core::ChannelArgs().Set(
      GRPC_ARG_RESOURCE_QUOTA, grpc_core::ResourceQuota::Default()));
  cf_engine->Connect(
      [&client_signal](auto endpoint) {
        EXPECT_EQ(endpoint.status().code(),
                  absl::StatusCode::kDeadlineExceeded);
        client_signal.Notify();
      },
      *resolved_addr, config, memory_quota.CreateMemoryAllocator("conn1"), 1ms);

  client_signal.WaitForNotification();
}

TEST(CFEventEngineTest, TestConnectionCancelled) {
  // use a non-routable IP so to cancel connection before timeout
  auto resolved_addr = URIToResolvedAddress("ipv4:10.255.255.255:1234");
  GPR_ASSERT(resolved_addr.ok());

  grpc_core::MemoryQuota memory_quota("cf_engine_test");
  grpc_core::Notification client_signal;
  auto cf_engine = std::make_shared<CFEventEngine>();

  ChannelArgsEndpointConfig config(grpc_core::ChannelArgs().Set(
      GRPC_ARG_RESOURCE_QUOTA, grpc_core::ResourceQuota::Default()));
  auto conn_handle = cf_engine->Connect(
      [&client_signal](auto endpoint) {
        EXPECT_EQ(endpoint.status().code(), absl::StatusCode::kCancelled);
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
}  // namespace

TEST(CFEventEngineTest, TestCreateDNSResolver) {
  grpc_core::MemoryQuota memory_quota("cf_engine_test");
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
      "localtest.me:80", "443");

  resolve_signal.WaitForNotification();
}

TEST(CFEventEngineTest, TestResolveIPv4Remote) {
  grpc_core::Notification resolve_signal;

  auto cf_engine = std::make_shared<CFEventEngine>();
  auto dns_resolver = cf_engine->GetDNSResolver({});

  dns_resolver.value()->LookupHostname(
      [&resolve_signal](auto result) {
        EXPECT_TRUE(result.status().ok());
        EXPECT_THAT(ResolvedAddressesToStrings(result.value()),
                    testing::IsSubsetOf(
                        {"1.2.3.4:80", "[64:ff9b::102:304]:80" /*NAT64*/}));

        resolve_signal.Notify();
      },
      "1.2.3.4.nip.io:80", "");

  resolve_signal.WaitForNotification();
}

TEST(CFEventEngineTest, TestResolveIPv6Remote) {
  grpc_core::Notification resolve_signal;

  auto cf_engine = std::make_shared<CFEventEngine>();
  auto dns_resolver = cf_engine->GetDNSResolver({});

  dns_resolver.value()->LookupHostname(
      [&resolve_signal](auto result) {
        EXPECT_TRUE(result.status().ok());
        EXPECT_THAT(
            ResolvedAddressesToStrings(result.value()),
            testing::UnorderedElementsAre("[2607:f8b0:400a:801::1002]:80"));

        resolve_signal.Notify();
      },
      "2607-f8b0-400a-801--1002.sslip.io.", "80");

  resolve_signal.WaitForNotification();
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

TEST(CFEventEngineTest, TestResolveMany) {
  std::atomic<int> times{10};
  grpc_core::Notification resolve_signal;
  auto cf_engine = std::make_shared<CFEventEngine>();
  auto dns_resolver = std::move(cf_engine->GetDNSResolver({})).value();

  for (int i = times; i >= 1; --i) {
    dns_resolver->LookupHostname(
        [&resolve_signal, &times, i](auto result) {
          EXPECT_TRUE(result.status().ok());
          EXPECT_THAT(
              ResolvedAddressesToStrings(result.value()),
              testing::IsSubsetOf(
                  {absl::StrFormat("100.0.0.%d:443", i),
                   absl::StrFormat("[64:ff9b::6400:%x]:443", i) /*NAT64*/}));

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
