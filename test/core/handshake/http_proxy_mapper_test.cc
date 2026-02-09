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

#include "src/core/handshaker/http_connect/http_proxy_mapper.h"

#include <grpc/impl/channel_arg_names.h>

#include <optional>

#include "src/core/handshaker/http_connect/http_connect_client_handshaker.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

namespace grpc_core {
namespace testing {
namespace {

const char* kNoProxyVarName = "no_proxy";

MATCHER_P(AddressEq, address, absl::StrFormat("is address %s", address)) {
  if (!arg.has_value()) {
    *result_listener << "is empty";
    return false;
  }
  auto address_string = grpc_sockaddr_to_string(&arg.value(), true);
  if (!address_string.ok()) {
    *result_listener << "unable to convert address to string: "
                     << address_string.status();
    return false;
  }
  if (*address_string != address) {
    *result_listener << "value: " << *address_string;
    return false;
  }
  return true;
}

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
            std::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), std::nullopt);
}

// Test empty entries in 'no_proxy' list.
TEST(NoProxyTest, EmptyEntries) {
  ScopedEnvVar no_proxy(kNoProxyVarName, "foo.com,,google.com,,");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///test.google.com:443", &args),
            std::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), std::nullopt);
}

// Test entries with CIDR blocks (Class A) in 'no_proxy' list.
TEST(NoProxyTest, CIDRClassAEntries) {
  ScopedEnvVar no_proxy(kNoProxyVarName, "foo.com,192.168.0.255/8");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  // address matching no_proxy cidr block
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///192.0.1.1:443", &args),
            std::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), std::nullopt);
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
            std::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), std::nullopt);
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
            std::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), std::nullopt);
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
            std::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), std::nullopt);
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
            std::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), std::nullopt);
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
            std::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), std::nullopt);
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

TEST(ProxyForAddressTest, ChannelArgPreferred) {
  ScopedEnvVar address_proxy(HttpProxyMapper::kAddressProxyEnvVar,
                             "192.168.0.100:2020");
  auto args = ChannelArgs()
                  .Set(GRPC_ARG_ADDRESS_HTTP_PROXY, "192.168.0.101:2020")
                  .Set(GRPC_ARG_ADDRESS_HTTP_PROXY_ENABLED_ADDRESSES,
                       "255.255.255.255/0");
  auto address = StringToSockaddr("192.168.0.1:3333");
  ASSERT_TRUE(address.ok()) << address.status();
  EXPECT_THAT(HttpProxyMapper().MapAddress(*address, &args),
              AddressEq("192.168.0.101:2020"));
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), "192.168.0.1:3333");
}

TEST(ProxyForAddressTest, AddressesNotIncluded) {
  ScopedEnvVar address_proxy(HttpProxyMapper::kAddressProxyEnvVar,
                             "192.168.0.100:2020");
  ScopedEnvVar address_proxy_enabled(
      HttpProxyMapper::kAddressProxyEnabledAddressesEnvVar,
      " 192.168.0.0/24 , 192.168.1.1 , 2001:db8:1::0/48 , 2001:db8:2::5");
  // v4 address
  auto address = StringToSockaddr("192.168.2.1:3333");
  ASSERT_TRUE(address.ok()) << address.status();
  ChannelArgs args;
  EXPECT_EQ(HttpProxyMapper().MapAddress(*address, &args), std::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), std::nullopt);
  // v6 address
  address = StringToSockaddr("[2001:db8:2::1]:3000");
  ASSERT_TRUE(address.ok()) << address.status();
  args = ChannelArgs();
  EXPECT_EQ(HttpProxyMapper().MapAddress(*address, &args), std::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), std::nullopt);
}

TEST(ProxyForAddressTest, BadProxy) {
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "192.168.0.0.100:2020");
  auto address = StringToSockaddr("192.168.0.1:3333");
  ASSERT_TRUE(address.ok()) << address.status();
  EXPECT_EQ(HttpProxyMapper().MapAddress(*address, &args), std::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), std::nullopt);
}

TEST(ProxyForAddressTest, UserInfo) {
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY,
                                "http://username:password@proxy.google.com");
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///test.google.com:443", &args),
            "proxy.google.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER),
            "test.google.com:443");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_HEADERS),
            "Proxy-Authorization:Basic dXNlcm5hbWU6cGFzc3dvcmQ=");
}

TEST(ProxyForAddressTest, PctEncodedUserInfo) {
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY,
                                "http://usern%40me:password@proxy.google.com");
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///test.google.com:443", &args),
            "proxy.google.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER),
            "test.google.com:443");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_HEADERS),
            "Proxy-Authorization:Basic dXNlcm5AbWU6cGFzc3dvcmQ=");
}

class IncludedAddressesTest
    : public ::testing::TestWithParam<absl::string_view> {};

INSTANTIATE_TEST_CASE_P(IncludedAddresses, IncludedAddressesTest,
                        ::testing::Values(
                            // IP v6 address in a proxied subnet
                            "[2001:db8:1::1]:2020",
                            // IP v6 address that is proxied
                            "[2001:db8:2::5]:2020",
                            // Proxied IP v4 address
                            "192.168.1.1:3333",
                            // IP v4 address in proxied subnet
                            "192.168.0.1:3333"));

TEST_P(IncludedAddressesTest, AddressIncluded) {
  ScopedEnvVar address_proxy(HttpProxyMapper::kAddressProxyEnvVar,
                             "[2001:db8::1111]:2020");
  ScopedEnvVar address_proxy_enabled(
      HttpProxyMapper::kAddressProxyEnabledAddressesEnvVar,
      // Whitespaces added to test that they are ignored as expected
      " 192.168.0.0/24 , 192.168.1.1 , 2001:db8:1::0/48 , 2001:db8:2::5");
  auto address = StringToSockaddr(GetParam());
  ASSERT_TRUE(address.ok()) << GetParam() << ": " << address.status();
  ChannelArgs args;
  EXPECT_THAT(HttpProxyMapper().MapAddress(*address, &args),
              AddressEq("[2001:db8::1111]:2020"));
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), GetParam());
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
