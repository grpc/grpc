//
//
// Copyright 2017 gRPC authors.
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

#include "src/core/lib/address_utils/parse_address.h"
#ifdef GRPC_HAVE_UNIX_SOCKET
#ifdef GPR_WINDOWS
// clang-format off
#include <ws2def.h>
#include <afunix.h>
// clang-format on
#else
#include <sys/un.h>
#endif  // GPR_WINDOWS
#endif  // GRPC_HAVE_UNIX_SOCKET

#ifdef GRPC_HAVE_VSOCK
#include <linux/vm_sockets.h>
#endif

#include <grpc/grpc.h>

#include <string>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"

#ifdef GRPC_HAVE_UNIX_SOCKET

static void test_grpc_parse_unix(const char* uri_text, const char* pathname) {
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  if (!uri.ok()) {
    LOG(ERROR) << uri.status();
    ASSERT_TRUE(uri.ok());
  }
  grpc_resolved_address addr;

  ASSERT_TRUE(grpc_parse_uri(*uri, &addr));
  struct sockaddr_un* addr_un =
      reinterpret_cast<struct sockaddr_un*>(addr.addr);
  ASSERT_EQ(AF_UNIX, addr_un->sun_family);
  ASSERT_STREQ(addr_un->sun_path, pathname);
}

static void test_grpc_parse_unix_abstract(const char* uri_text,
                                          const char* pathname) {
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  if (!uri.ok()) {
    LOG(ERROR) << uri.status();
    ASSERT_TRUE(uri.ok());
  }
  grpc_resolved_address addr;

  ASSERT_TRUE(grpc_parse_uri(*uri, &addr));
  struct sockaddr_un* addr_un =
      reinterpret_cast<struct sockaddr_un*>(addr.addr);
  ASSERT_EQ(AF_UNIX, addr_un->sun_family);
  ASSERT_EQ('\0', addr_un->sun_path[0]);
  ASSERT_TRUE(absl::StartsWith(addr_un->sun_path + 1, pathname));
}

#else  // GRPC_HAVE_UNIX_SOCKET

static void test_grpc_parse_unix(const char* uri_text, const char* pathname) {}
static void test_grpc_parse_unix_abstract(const char* uri_text,
                                          const char* pathname) {}

#endif  // GRPC_HAVE_UNIX_SOCKET

#ifdef GRPC_HAVE_VSOCK

static void test_grpc_parse_vsock(const char* uri_text, uint32_t cid,
                                  uint32_t port) {
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  if (!uri.ok()) {
    LOG(ERROR) << uri.status();
    ASSERT_TRUE(uri.ok());
  }
  grpc_resolved_address addr;

  ASSERT_TRUE(grpc_parse_uri(*uri, &addr));
  struct sockaddr_vm* addr_vm =
      reinterpret_cast<struct sockaddr_vm*>(addr.addr);
  ASSERT_EQ(AF_VSOCK, addr_vm->svm_family);
  // svm_cid and svm_port are host-order fields, unlike the sockaddr_in port.
  ASSERT_EQ(addr_vm->svm_cid, cid);
  ASSERT_EQ(addr_vm->svm_port, port);
}

static void test_grpc_parse_vsock_invalid(const char* uri_text) {
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  if (!uri.ok()) {
    LOG(ERROR) << uri.status();
    ASSERT_TRUE(uri.ok());
  }
  grpc_resolved_address addr;

  ASSERT_FALSE(grpc_parse_uri(*uri, &addr));
}

#else  // GRPC_HAVE_VSOCK

static void test_grpc_parse_vsock(const char* /* uri_text */, ...) {}
static void test_grpc_parse_vsock_invalid(const char* /* uri_text */) {}

#endif  // GRPC_HAVE_VSOCK

static void test_grpc_parse_ipv4(const char* uri_text, const char* host,
                                 unsigned short port) {
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  if (!uri.ok()) {
    LOG(ERROR) << uri.status();
    ASSERT_TRUE(uri.ok());
  }
  grpc_resolved_address addr;
  char ntop_buf[GRPC_INET_ADDRSTRLEN];

  ASSERT_TRUE(grpc_parse_ipv4(*uri, &addr));
  grpc_sockaddr_in* addr_in = reinterpret_cast<grpc_sockaddr_in*>(addr.addr);
  ASSERT_EQ(GRPC_AF_INET, addr_in->sin_family);
  ASSERT_NE(nullptr, grpc_inet_ntop(GRPC_AF_INET, &addr_in->sin_addr, ntop_buf,
                                    sizeof(ntop_buf)));
  ASSERT_STREQ(ntop_buf, host);
  ASSERT_EQ(grpc_ntohs(addr_in->sin_port), port);
}

// Test parsing invalid ipv4 addresses (valid uri_text but invalid ipv4 addr)
static void test_grpc_parse_ipv4_invalid(const char* uri_text) {
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  if (!uri.ok()) {
    LOG(ERROR) << uri.status();
    ASSERT_TRUE(uri.ok());
  }
  grpc_resolved_address addr;
  ASSERT_FALSE(grpc_parse_ipv4(*uri, &addr));
}

static void test_grpc_parse_ipv6(const char* uri_text, const char* host,
                                 unsigned short port, uint32_t scope_id) {
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  if (!uri.ok()) {
    LOG(ERROR) << uri.status();
    ASSERT_TRUE(uri.ok());
  }
  grpc_resolved_address addr;
  char ntop_buf[GRPC_INET6_ADDRSTRLEN];
  ASSERT_TRUE(grpc_parse_ipv6(*uri, &addr));
  grpc_sockaddr_in6* addr_in6 = reinterpret_cast<grpc_sockaddr_in6*>(addr.addr);
  ASSERT_EQ(GRPC_AF_INET6, addr_in6->sin6_family);
  ASSERT_NE(nullptr, grpc_inet_ntop(GRPC_AF_INET6, &addr_in6->sin6_addr,
                                    ntop_buf, sizeof(ntop_buf)));
  ASSERT_STREQ(ntop_buf, host);
  ASSERT_EQ(grpc_ntohs(addr_in6->sin6_port), port);
  ASSERT_EQ(addr_in6->sin6_scope_id, scope_id);
}

// Test parsing invalid ipv6 addresses (valid uri_text but invalid ipv6 addr)
static void test_grpc_parse_ipv6_invalid(const char* uri_text) {
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(uri_text);
  if (!uri.ok()) {
    LOG(ERROR) << uri.status();
    ASSERT_TRUE(uri.ok());
  }
  grpc_resolved_address addr;
  ASSERT_FALSE(grpc_parse_ipv6(*uri, &addr));
}

TEST(ParseAddressTest, MainTest) {
  grpc_init();

  test_grpc_parse_unix("unix:/path/name", "/path/name");
  test_grpc_parse_unix_abstract("unix-abstract:foobar", "foobar");
  test_grpc_parse_vsock("vsock:-1:12345", -1, 12345);
  test_grpc_parse_ipv4("ipv4:192.0.2.1:12345", "192.0.2.1", 12345);
  test_grpc_parse_ipv6("ipv6:[2001:db8::1]:12345", "2001:db8::1", 12345, 0);
  test_grpc_parse_ipv6("ipv6:[2001:db8::1%252]:12345", "2001:db8::1", 12345, 2);

  // Address length greater than GRPC_INET6_ADDRSTRLEN
  test_grpc_parse_ipv6_invalid(
      "ipv6:WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW45%"
      "25v6:45%25x$1*");

  grpc_shutdown();
}

TEST(ParseAddressTest, PortParsing) {
  grpc_init();

  // Boundary ports are still accepted.
  test_grpc_parse_ipv4("ipv4:192.0.2.1:0", "192.0.2.1", 0);
  test_grpc_parse_ipv4("ipv4:192.0.2.1:65535", "192.0.2.1", 65535);
  test_grpc_parse_ipv6("ipv6:[2001:db8::1]:65535", "2001:db8::1", 65535, 0);

  // Trailing garbage after the digits is rejected instead of partially
  // parsed.
  test_grpc_parse_ipv4_invalid("ipv4:192.0.2.1:123x");
  test_grpc_parse_ipv6_invalid("ipv6:[2001:db8::1]:123x");
  test_grpc_parse_vsock_invalid("vsock:10:123x");

  // Ports that do not fit in 16 bits are rejected.
  test_grpc_parse_ipv4_invalid("ipv4:192.0.2.1:65536");
  test_grpc_parse_ipv6_invalid("ipv6:[2001:db8::1]:65536");

  // Negative ports are rejected.
  test_grpc_parse_ipv4_invalid("ipv4:192.0.2.1:-1");
  test_grpc_parse_ipv6_invalid("ipv6:[2001:db8::1]:-1");
  test_grpc_parse_vsock_invalid("vsock:10:-1");

  grpc_shutdown();
}

TEST(ParseAddressTest, VsockParsing) {
  grpc_init();

  test_grpc_parse_vsock("vsock:10:1234", 10, 1234);
  // Both spellings of VMADDR_CID_ANY.
  test_grpc_parse_vsock("vsock:4294967295:1234", 4294967295u, 1234);
  // Extra fields are rejected instead of ignored.
  test_grpc_parse_vsock_invalid("vsock:10");
  test_grpc_parse_vsock_invalid("vsock:10:20:30");
  test_grpc_parse_vsock_invalid("vsock:1x:20");
  // -1 is the only negative cid that is accepted.
  test_grpc_parse_vsock_invalid("vsock:-2:20");

  grpc_shutdown();
}

TEST(ParseAddressTest, StrHtons) {
  EXPECT_EQ(grpc_strhtons("80"), grpc_htons(80));
  EXPECT_EQ(grpc_strhtons("65535"), grpc_htons(65535));
  EXPECT_EQ(grpc_strhtons("http"), grpc_htons(80));
  EXPECT_EQ(grpc_strhtons("https"), grpc_htons(443));
  // Named ports that are unknown return 0.
  EXPECT_EQ(grpc_strhtons("notaport"), 0);
  // Values that do not fit in 16 bits return 0 instead of wrapping.
  EXPECT_EQ(grpc_strhtons("65536"), 0);
  // Trailing garbage is rejected instead of partially parsed.
  EXPECT_EQ(grpc_strhtons("123x"), 0);
  EXPECT_EQ(grpc_strhtons("-1"), 0);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
