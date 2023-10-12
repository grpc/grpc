//
//
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
//
//

#include "src/core/ext/filters/client_channel/http_proxy_mapper.h"

#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/transport/http_connect_handshaker.h"
#include "test/core/util/scoped_env_var.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

const char* kNoProxyVarName = "no_proxy";

// Test that an empty no_proxy works as expected, i.e., proxy is used.
TEST(NoProxyTest, EmptyList) {
  ScopedEnvVar no_proxy(kNoProxyVarName, "");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///test.google.com:443", &args),
            "proxy.google.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER),
            "test.google.com:443");
}

// Test basic usage of 'no_proxy' to avoid using proxy for certain domain names.
TEST(NoProxyTest, Basic) {
  ScopedEnvVar no_proxy(kNoProxyVarName, "google.com");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///test.google.com:443", &args),
            absl::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), absl::nullopt);
}

// Test empty entries in 'no_proxy' list.
TEST(NoProxyTest, EmptyEntries) {
  ScopedEnvVar no_proxy(kNoProxyVarName, "foo.com,,google.com,,");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///test.google.com:443", &args),
            absl::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), absl::nullopt);
}

// Test entries with CIDR blocks (Class A) in 'no_proxy' list.
TEST(NoProxyTest, CIDRClassAEntries) {
  ScopedEnvVar no_proxy(kNoProxyVarName, "foo.com,192.168.0.255/8");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  // address matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///192.0.1.1:443", &args),
            absl::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), absl::nullopt);
  // address not matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///193.0.1.1:443", &args),
            "proxy.google.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), "193.0.1.1:443");
}

// Test entries with CIDR blocks (Class B) in 'no_proxy' list.
TEST(NoProxyTest, CIDRClassBEntries) {
  ScopedEnvVar no_proxy(kNoProxyVarName, "foo.com,192.168.0.255/16");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  // address matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///192.168.1.5:443", &args),
            absl::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), absl::nullopt);
  // address not matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///192.169.1.1:443", &args),
            "proxy.google.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), "192.169.1.1:443");
}

// Test entries with CIDR blocks (Class C) in 'no_proxy' list.
TEST(NoProxyTest, CIDRClassCEntries) {
  ScopedEnvVar no_proxy(kNoProxyVarName, "foo.com,192.168.0.255/24");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  // address matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///192.168.0.5:443", &args),
            absl::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), absl::nullopt);
  // address not matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///192.168.1.1:443", &args),
            "proxy.google.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), "192.168.1.1:443");
}

// Test entries with CIDR blocks (exact match) in 'no_proxy' list.
TEST(NoProxyTest, CIDREntriesExactMatch) {
  ScopedEnvVar no_proxy(kNoProxyVarName, "foo.com,192.168.0.4/32");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  // address matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///192.168.0.4:443", &args),
            absl::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), absl::nullopt);
  // address not matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///192.168.0.5:443", &args),
            "proxy.google.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), "192.168.0.5:443");
}

// Test entries with IPv6 CIDR blocks in 'no_proxy' list.
TEST(NoProxyTest, CIDREntriesIPv6ExactMatch) {
  ScopedEnvVar no_proxy(kNoProxyVarName, "foo.com,2002:db8:a::45/64");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  // address matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName(
                "dns:///[2002:0db8:000a:0000:0000:0000:0000:0001]:443", &args),
            absl::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), absl::nullopt);
  // address not matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName(
                "dns:///[2003:0db8:000a:0000:0000:0000:0000:0000]:443", &args),
            "proxy.google.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER),
            "[2003:0db8:000a:0000:0000:0000:0000:0000]:443");
}

// Test entries with whitespaced CIDR blocks in 'no_proxy' list.
TEST(NoProxyTest, WhitespacedEntries) {
  ScopedEnvVar no_proxy(kNoProxyVarName, "foo.com, 192.168.0.255/24");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  // address matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///192.168.0.5:443", &args),
            absl::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), absl::nullopt);
  // address not matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///192.168.1.0:443", &args),
            "proxy.google.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), "192.168.1.0:443");
}

// Test entries with invalid CIDR blocks in 'no_proxy' list.
TEST(NoProxyTest, InvalidCIDREntries) {
  ScopedEnvVar no_proxy(kNoProxyVarName, "foo.com, 192.168.0.255/33");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///192.168.1.0:443", &args),
            "proxy.google.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), "192.168.1.0:443");
}

TEST(ProxyForAddressTest, SetByEnvVar) {
  ScopedEnvVar address_proxy(HttpProxyMapper::kAddressProxyEnvVar,
                             "192.168.0.100:2020");
  auto args = ChannelArgs();
  auto address = StringToSockaddr("192.168.0.1:3333");
  ASSERT_TRUE(address.ok()) << address.status();
  auto mapped = HttpProxyMapper().MapAddress(*address, &args);
  ASSERT_TRUE(mapped.has_value());
  auto addr_string = grpc_sockaddr_to_string(&mapped.value(), true);
  ASSERT_TRUE(addr_string.ok()) << addr_string.status();
  EXPECT_EQ(*addr_string, "192.168.0.100:2020");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), "192.168.0.1:3333");
}

TEST(ProxyForAddressTest, ChannelArgPreferred) {
  ScopedEnvVar address_proxy(HttpProxyMapper::kAddressProxyEnvVar,
                             "192.168.0.100:2020");
  auto args =
      ChannelArgs().Set(GRPC_ARG_ADDRESS_HTTP_PROXY, "192.168.0.101:2020");
  auto address = StringToSockaddr("192.168.0.1:3333");
  ASSERT_TRUE(address.ok()) << address.status();
  auto mapped = HttpProxyMapper().MapAddress(*address, &args);
  ASSERT_TRUE(mapped.has_value());
  auto address_string = grpc_sockaddr_to_string(&mapped.value(), true);
  ASSERT_TRUE(address_string.ok()) << address.status();
  EXPECT_EQ(*address_string, "192.168.0.101:2020");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), "192.168.0.1:3333");
}

TEST(ProxyForAddressTest, BadProxy) {
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "192.168.0.0.100:2020");
  auto address = StringToSockaddr("192.168.0.1:3333");
  ASSERT_TRUE(address.ok()) << address.status();
  auto mapped = HttpProxyMapper().MapAddress(*address, &args);
  EXPECT_FALSE(mapped.has_value());
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), absl::nullopt);
}
}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
