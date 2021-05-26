//
// Copyright 2015 gRPC authors.
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

// With the addition of a libuv endpoint, sockaddr.h now includes uv.h when
// using that endpoint. Because of various transitive includes in uv.h,
// including windows.h on Windows, uv.h must be included before other system
// headers. Therefore, sockaddr.h must always be included first
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"

#include <errno.h>
#include <string.h>

#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include "test/core/util/test_config.h"

namespace {

grpc_resolved_address MakeAddr4(const uint8_t* data, size_t data_len) {
  grpc_resolved_address resolved_addr4;
  grpc_sockaddr_in* addr4 =
      reinterpret_cast<grpc_sockaddr_in*>(resolved_addr4.addr);
  memset(&resolved_addr4, 0, sizeof(resolved_addr4));
  addr4->sin_family = GRPC_AF_INET;
  GPR_ASSERT(data_len == sizeof(addr4->sin_addr.s_addr));
  memcpy(&addr4->sin_addr.s_addr, data, data_len);
  addr4->sin_port = grpc_htons(12345);
  resolved_addr4.len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in));
  return resolved_addr4;
}

grpc_resolved_address MakeAddr6(const uint8_t* data, size_t data_len) {
  grpc_resolved_address resolved_addr6;
  grpc_sockaddr_in6* addr6 =
      reinterpret_cast<grpc_sockaddr_in6*>(resolved_addr6.addr);
  memset(&resolved_addr6, 0, sizeof(resolved_addr6));
  addr6->sin6_family = GRPC_AF_INET6;
  GPR_ASSERT(data_len == sizeof(addr6->sin6_addr.s6_addr));
  memcpy(&addr6->sin6_addr.s6_addr, data, data_len);
  addr6->sin6_port = grpc_htons(12345);
  resolved_addr6.len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in6));
  return resolved_addr6;
}

void SetIPv6ScopeId(grpc_resolved_address* addr, uint32_t scope_id) {
  grpc_sockaddr_in6* addr6 = reinterpret_cast<grpc_sockaddr_in6*>(addr->addr);
  ASSERT_EQ(addr6->sin6_family, GRPC_AF_INET6);
  addr6->sin6_scope_id = scope_id;
}

const uint8_t kMapped[] = {0, 0, 0,    0,    0,   0, 0, 0,
                           0, 0, 0xff, 0xff, 192, 0, 2, 1};

const uint8_t kNotQuiteMapped[] = {0, 0, 0,    0,    0,   0, 0, 0,
                                   0, 0, 0xff, 0xfe, 192, 0, 2, 99};
const uint8_t kIPv4[] = {192, 0, 2, 1};

const uint8_t kIPv6[] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                         0,    0,    0,    0,    0, 0, 0, 1};

TEST(SockAddrUtilsTest, SockAddrIsV4Mapped) {
  // v4mapped input should succeed.
  grpc_resolved_address input6 = MakeAddr6(kMapped, sizeof(kMapped));
  ASSERT_TRUE(grpc_sockaddr_is_v4mapped(&input6, nullptr));
  grpc_resolved_address output4;
  ASSERT_TRUE(grpc_sockaddr_is_v4mapped(&input6, &output4));
  grpc_resolved_address expect4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  ASSERT_EQ(memcmp(&expect4, &output4, sizeof(expect4)), 0);

  // Non-v4mapped input should fail.
  input6 = MakeAddr6(kNotQuiteMapped, sizeof(kNotQuiteMapped));
  ASSERT_FALSE(grpc_sockaddr_is_v4mapped(&input6, nullptr));
  ASSERT_FALSE(grpc_sockaddr_is_v4mapped(&input6, &output4));
  // Output is unchanged.
  ASSERT_EQ(memcmp(&expect4, &output4, sizeof(expect4)), 0);

  // Plain IPv4 input should also fail.
  grpc_resolved_address input4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  ASSERT_FALSE(grpc_sockaddr_is_v4mapped(&input4, nullptr));
}

TEST(SockAddrUtilsTest, SockAddrToV4Mapped) {
  // IPv4 input should succeed.
  grpc_resolved_address input4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  grpc_resolved_address output6;
  ASSERT_TRUE(grpc_sockaddr_to_v4mapped(&input4, &output6));
  grpc_resolved_address expect6 = MakeAddr6(kMapped, sizeof(kMapped));
  ASSERT_EQ(memcmp(&expect6, &output6, sizeof(output6)), 0);

  // IPv6 input should fail.
  grpc_resolved_address input6 = MakeAddr6(kIPv6, sizeof(kIPv6));
  ASSERT_TRUE(!grpc_sockaddr_to_v4mapped(&input6, &output6));
  // Output is unchanged.
  ASSERT_EQ(memcmp(&expect6, &output6, sizeof(output6)), 0);

  // Already-v4mapped input should also fail.
  input6 = MakeAddr6(kMapped, sizeof(kMapped));
  ASSERT_TRUE(!grpc_sockaddr_to_v4mapped(&input6, &output6));
}

TEST(SockAddrUtilsTest, SockAddrIsWildCard) {
  // Generate wildcards.
  grpc_resolved_address wild4;
  grpc_resolved_address wild6;
  grpc_sockaddr_make_wildcards(555, &wild4, &wild6);
  grpc_resolved_address wild_mapped;
  ASSERT_TRUE(grpc_sockaddr_to_v4mapped(&wild4, &wild_mapped));

  // Test 0.0.0.0:555
  int port = -1;
  ASSERT_TRUE(grpc_sockaddr_is_wildcard(&wild4, &port));
  ASSERT_TRUE(port == 555);
  grpc_sockaddr_in* wild4_addr =
      reinterpret_cast<grpc_sockaddr_in*>(&wild4.addr);
  memset(&wild4_addr->sin_addr.s_addr, 0xbd, 1);
  ASSERT_FALSE(grpc_sockaddr_is_wildcard(&wild4, &port));

  // Test [::]:555
  port = -1;
  ASSERT_TRUE(grpc_sockaddr_is_wildcard(&wild6, &port));
  ASSERT_EQ(port, 555);
  grpc_sockaddr_in6* wild6_addr =
      reinterpret_cast<grpc_sockaddr_in6*>(&wild6.addr);
  memset(&wild6_addr->sin6_addr.s6_addr, 0xbd, 1);
  ASSERT_FALSE(grpc_sockaddr_is_wildcard(&wild6, &port));

  // Test [::ffff:0.0.0.0]:555
  port = -1;
  ASSERT_TRUE(grpc_sockaddr_is_wildcard(&wild_mapped, &port));
  ASSERT_EQ(port, 555);
  grpc_sockaddr_in6* wild_mapped_addr =
      reinterpret_cast<grpc_sockaddr_in6*>(&wild_mapped.addr);
  memset(&wild_mapped_addr->sin6_addr.s6_addr, 0xbd, 1);
  ASSERT_FALSE(grpc_sockaddr_is_wildcard(&wild_mapped, &port));

  // Test AF_UNSPEC.
  port = -1;
  grpc_resolved_address phony;
  memset(&phony, 0, sizeof(phony));
  ASSERT_FALSE(grpc_sockaddr_is_wildcard(&phony, &port));
  ASSERT_EQ(port, -1);
}

TEST(SockAddrUtilsTest, SockAddrToString) {
  errno = 0x7EADBEEF;

  grpc_resolved_address input4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  EXPECT_EQ(grpc_sockaddr_to_string(&input4, false), "192.0.2.1:12345");
  EXPECT_EQ(grpc_sockaddr_to_string(&input4, true), "192.0.2.1:12345");
  EXPECT_EQ(grpc_sockaddr_to_uri(&input4), "ipv4:192.0.2.1:12345");

  grpc_resolved_address input6 = MakeAddr6(kIPv6, sizeof(kIPv6));
  EXPECT_EQ(grpc_sockaddr_to_string(&input6, false), "[2001:db8::1]:12345");
  EXPECT_EQ(grpc_sockaddr_to_string(&input6, true), "[2001:db8::1]:12345");
  EXPECT_EQ(grpc_sockaddr_to_uri(&input6), "ipv6:[2001:db8::1]:12345");

  SetIPv6ScopeId(&input6, 2);
  EXPECT_EQ(grpc_sockaddr_to_string(&input6, false), "[2001:db8::1%252]:12345");
  EXPECT_EQ(grpc_sockaddr_to_string(&input6, true), "[2001:db8::1%252]:12345");
  EXPECT_EQ(grpc_sockaddr_to_uri(&input6), "ipv6:[2001:db8::1%252]:12345");

  SetIPv6ScopeId(&input6, 101);
  EXPECT_EQ(grpc_sockaddr_to_string(&input6, false),
            "[2001:db8::1%25101]:12345");
  EXPECT_EQ(grpc_sockaddr_to_string(&input6, true),
            "[2001:db8::1%25101]:12345");
  EXPECT_EQ(grpc_sockaddr_to_uri(&input6), "ipv6:[2001:db8::1%25101]:12345");

  input6 = MakeAddr6(kMapped, sizeof(kMapped));
  EXPECT_EQ(grpc_sockaddr_to_string(&input6, false),
            "[::ffff:192.0.2.1]:12345");
  EXPECT_EQ(grpc_sockaddr_to_string(&input6, true), "192.0.2.1:12345");
  EXPECT_EQ(grpc_sockaddr_to_uri(&input6), "ipv4:192.0.2.1:12345");

  input6 = MakeAddr6(kNotQuiteMapped, sizeof(kNotQuiteMapped));
  EXPECT_EQ(grpc_sockaddr_to_string(&input6, false), "[::fffe:c000:263]:12345");
  EXPECT_EQ(grpc_sockaddr_to_string(&input6, true), "[::fffe:c000:263]:12345");
  EXPECT_EQ(grpc_sockaddr_to_uri(&input6), "ipv6:[::fffe:c000:263]:12345");

  grpc_resolved_address phony;
  memset(&phony, 0, sizeof(phony));
  grpc_sockaddr* phony_addr = reinterpret_cast<grpc_sockaddr*>(phony.addr);
  phony_addr->sa_family = 123;
  EXPECT_EQ(grpc_sockaddr_to_string(&phony, false), "(sockaddr family=123)");
  EXPECT_EQ(grpc_sockaddr_to_string(&phony, true), "(sockaddr family=123)");
  EXPECT_TRUE(grpc_sockaddr_to_uri(&phony).empty());
}

TEST(SockAddrUtilsTest, SockAddrSetGetPort) {
  grpc_resolved_address input4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  ASSERT_EQ(grpc_sockaddr_get_port(&input4), 12345);
  ASSERT_TRUE(grpc_sockaddr_set_port(&input4, 54321));
  ASSERT_EQ(grpc_sockaddr_get_port(&input4), 54321);

  grpc_resolved_address input6 = MakeAddr6(kIPv6, sizeof(kIPv6));
  ASSERT_EQ(grpc_sockaddr_get_port(&input6), 12345);
  ASSERT_TRUE(grpc_sockaddr_set_port(&input6, 54321));
  ASSERT_EQ(grpc_sockaddr_get_port(&input6), 54321);

  grpc_resolved_address phony;
  memset(&phony, 0, sizeof(phony));
  grpc_sockaddr* phony_addr = reinterpret_cast<grpc_sockaddr*>(phony.addr);
  phony_addr->sa_family = 123;
  ASSERT_EQ(grpc_sockaddr_get_port(&phony), false);
  ASSERT_EQ(grpc_sockaddr_set_port(&phony, 1234), false);
}

void VerifySocketAddressMatch(const std::string& ip_address,
                              const std::string& subnet, uint32_t mask_bits,
                              bool success) {
  grpc_resolved_address addr;
  ASSERT_EQ(grpc_string_to_sockaddr(&addr, ip_address.c_str(), false),
            GRPC_ERROR_NONE);
  // Setting the port has no effect on the match.
  grpc_sockaddr_set_port(&addr, 12345);
  grpc_resolved_address subnet_addr;
  ASSERT_EQ(grpc_string_to_sockaddr(&subnet_addr, subnet.c_str(), false),
            GRPC_ERROR_NONE);
  grpc_sockaddr_mask_bits(&subnet_addr, mask_bits);
  EXPECT_EQ(grpc_sockaddr_match_subnet(&addr, &subnet_addr, mask_bits), success)
      << "IP=" << ip_address << " Subnet=" << subnet << " Mask=" << mask_bits;
}

void VerifySocketAddressMatchSuccess(const std::string& ip_address,
                                     const std::string& subnet,
                                     uint32_t mask_bits) {
  // If the IP address matches the subnet for a particular length, then it would
  // match for all lengths [0, mask_bits]
  for (uint32_t i = 0; i <= mask_bits; i++) {
    VerifySocketAddressMatch(ip_address, subnet, i, true);
  }
}

void VerifySocketAddressMatchFailure(const std::string& ip_address,
                                     const std::string& subnet,
                                     uint32_t mask_bits) {
  // If the IP address fails matching the subnet for a particular length, then
  // it would also fail for all lengths [mask_bits, 128]
  for (auto i = mask_bits; i <= 128; i++) {
    VerifySocketAddressMatch(ip_address, subnet, i, false);
  }
}

TEST(SockAddrUtilsTest, SockAddrMatchSubnet) {
  // IPv4 Tests
  VerifySocketAddressMatchSuccess("192.168.1.1", "192.168.1.1", 32);
  VerifySocketAddressMatchSuccess("255.255.255.255", "255.255.255.255", 32);
  VerifySocketAddressMatchFailure("192.168.1.1", "192.168.1.2", 31);
  VerifySocketAddressMatchFailure("192.168.1.1", "191.0.0.0", 8);
  VerifySocketAddressMatchFailure("192.168.1.1", "0.0.0.0", 1);
  // IPv6 Tests
  VerifySocketAddressMatchSuccess("2001:db8::", "2001::", 16);
  VerifySocketAddressMatchSuccess("2001:db8:cfe:134:3ab:3456:78:9",
                                  "2001:db8:cfe:134:3ab:3456:78:9", 128);
  VerifySocketAddressMatchSuccess("FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF",
                                  "FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF",
                                  128);
  VerifySocketAddressMatchFailure("2001:db8:cfe:134:3ab:3456:78:9",
                                  "3001:2:3:4:5:6:7:8", 4);
  VerifySocketAddressMatchFailure("FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF",
                                  "::", 1);
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int retval = RUN_ALL_TESTS();
  return retval;
}
