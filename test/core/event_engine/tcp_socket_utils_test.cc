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
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/tcp_socket_utils.h"

#include <errno.h>

#include "src/core/lib/iomgr/port.h"  // IWYU pragma: keep

#ifdef GRPC_HAVE_VSOCK
#include <linux/vm_sockets.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

// IWYU pragma: no_include <arpa/inet.h>

#include <string>

#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/un.h>
#endif

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/sockaddr.h"

namespace grpc_event_engine {
namespace experimental {

namespace {
const uint8_t kMapped[] = {0, 0, 0,    0,    0,   0, 0, 0,
                           0, 0, 0xff, 0xff, 192, 0, 2, 1};
const uint8_t kNotQuiteMapped[] = {0, 0, 0,    0,    0,   0, 0, 0,
                                   0, 0, 0xff, 0xfe, 192, 0, 2, 99};
const uint8_t kIPv4[] = {192, 0, 2, 1};
const uint8_t kIPv6[] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                         0,    0,    0,    0,    0, 0, 0, 1};

EventEngine::ResolvedAddress MakeAddr4(const uint8_t* data, size_t data_len) {
  EventEngine::ResolvedAddress resolved_addr4;
  sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(
      const_cast<sockaddr*>(resolved_addr4.address()));
  memset(&resolved_addr4, 0, sizeof(resolved_addr4));
  addr4->sin_family = AF_INET;
  GPR_ASSERT(data_len == sizeof(addr4->sin_addr.s_addr));
  memcpy(&addr4->sin_addr.s_addr, data, data_len);
  addr4->sin_port = htons(12345);
  return EventEngine::ResolvedAddress(
      reinterpret_cast<sockaddr*>(addr4),
      static_cast<socklen_t>(sizeof(sockaddr_in)));
}

EventEngine::ResolvedAddress MakeAddr6(const uint8_t* data, size_t data_len) {
  EventEngine::ResolvedAddress resolved_addr6;
  sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(
      const_cast<sockaddr*>(resolved_addr6.address()));
  memset(&resolved_addr6, 0, sizeof(resolved_addr6));
  addr6->sin6_family = AF_INET6;
  GPR_ASSERT(data_len == sizeof(addr6->sin6_addr.s6_addr));
  memcpy(&addr6->sin6_addr.s6_addr, data, data_len);
  addr6->sin6_port = htons(12345);
  return EventEngine::ResolvedAddress(
      reinterpret_cast<sockaddr*>(addr6),
      static_cast<socklen_t>(sizeof(sockaddr_in6)));
}

void SetIPv6ScopeId(EventEngine::ResolvedAddress& addr, uint32_t scope_id) {
  sockaddr_in6* addr6 =
      reinterpret_cast<sockaddr_in6*>(const_cast<sockaddr*>(addr.address()));
  ASSERT_EQ(addr6->sin6_family, AF_INET6);
  addr6->sin6_scope_id = scope_id;
}

#ifdef GRPC_HAVE_UNIX_SOCKET
absl::StatusOr<EventEngine::ResolvedAddress> UnixSockaddrPopulate(
    absl::string_view path) {
  EventEngine::ResolvedAddress resolved_addr;
  memset(const_cast<sockaddr*>(resolved_addr.address()), 0,
         resolved_addr.size());
  struct sockaddr_un* un = reinterpret_cast<struct sockaddr_un*>(
      const_cast<sockaddr*>(resolved_addr.address()));
  const size_t maxlen = sizeof(un->sun_path) - 1;
  if (path.size() > maxlen) {
    return absl::InternalError(absl::StrCat(
        "Path name should not have more than ", maxlen, " characters"));
  }
  un->sun_family = AF_UNIX;
  path.copy(un->sun_path, path.size());
  un->sun_path[path.size()] = '\0';
  return EventEngine::ResolvedAddress(reinterpret_cast<sockaddr*>(un),
                                      static_cast<socklen_t>(sizeof(*un)));
}

absl::StatusOr<EventEngine::ResolvedAddress> UnixAbstractSockaddrPopulate(
    absl::string_view path) {
  EventEngine::ResolvedAddress resolved_addr;
  memset(const_cast<sockaddr*>(resolved_addr.address()), 0,
         resolved_addr.size());
  struct sockaddr* addr = const_cast<sockaddr*>(resolved_addr.address());
  struct sockaddr_un* un = reinterpret_cast<struct sockaddr_un*>(addr);
  const size_t maxlen = sizeof(un->sun_path) - 1;
  if (path.size() > maxlen) {
    return absl::InternalError(absl::StrCat(
        "Path name should not have more than ", maxlen, " characters"));
  }
  un->sun_family = AF_UNIX;
  un->sun_path[0] = '\0';
  path.copy(un->sun_path + 1, path.size());
#ifdef GPR_APPLE
  return EventEngine::ResolvedAddress(
      addr, static_cast<socklen_t>(sizeof(un->sun_len) +
                                   sizeof(un->sun_family) + path.size() + 1));
#else
  return EventEngine::ResolvedAddress(
      addr, static_cast<socklen_t>(sizeof(un->sun_family) + path.size() + 1));
#endif
}
#endif  //  GRPC_HAVE_UNIX_SOCKET

#ifdef GRPC_HAVE_VSOCK
absl::StatusOr<EventEngine::ResolvedAddress> VSockaddrPopulate(
    absl::string_view path) {
  EventEngine::ResolvedAddress resolved_addr;
  memset(const_cast<sockaddr*>(resolved_addr.address()), 0,
         resolved_addr.size());
  struct sockaddr_vm* vm = reinterpret_cast<struct sockaddr_vm*>(
      const_cast<sockaddr*>(resolved_addr.address()));
  vm->svm_family = AF_VSOCK;
  std::string s = std::string(path);
  if (sscanf(s.c_str(), "%u:%u", &vm->svm_cid, &vm->svm_port) != 2) {
    return absl::InternalError(
        absl::StrCat("Failed to parse vsock cid/port: ", s));
  }
  return EventEngine::ResolvedAddress(reinterpret_cast<sockaddr*>(vm),
                                      static_cast<socklen_t>(sizeof(*vm)));
}
#endif  //  GRPC_HAVE_VSOCK

}  // namespace

TEST(TcpSocketUtilsTest, ResolvedAddressIsV4MappedTest) {
  // v4mapped input should succeed.
  EventEngine::ResolvedAddress input6 = MakeAddr6(kMapped, sizeof(kMapped));
  ASSERT_TRUE(ResolvedAddressIsV4Mapped(input6, nullptr));
  EventEngine::ResolvedAddress output4;
  ASSERT_TRUE(ResolvedAddressIsV4Mapped(input6, &output4));
  EventEngine::ResolvedAddress expect4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  ASSERT_EQ(memcmp(expect4.address(), output4.address(), expect4.size()), 0);

  // Non-v4mapped input should fail.
  input6 = MakeAddr6(kNotQuiteMapped, sizeof(kNotQuiteMapped));
  ASSERT_FALSE(ResolvedAddressIsV4Mapped(input6, nullptr));
  ASSERT_FALSE(ResolvedAddressIsV4Mapped(input6, &output4));
  // Output is unchanged.
  ASSERT_EQ(memcmp(expect4.address(), output4.address(), expect4.size()), 0);

  // Plain IPv4 input should also fail.
  EventEngine::ResolvedAddress input4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  ASSERT_FALSE(ResolvedAddressIsV4Mapped(input4, nullptr));
}

TEST(TcpSocketUtilsTest, ResolvedAddressToV4MappedTest) {
  // IPv4 input should succeed.
  EventEngine::ResolvedAddress input4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  EventEngine::ResolvedAddress output6;
  ASSERT_TRUE(ResolvedAddressToV4Mapped(input4, &output6));
  EventEngine::ResolvedAddress expect6 = MakeAddr6(kMapped, sizeof(kMapped));
  ASSERT_EQ(memcmp(expect6.address(), output6.address(), output6.size()), 0);

  // IPv6 input should fail.
  EventEngine::ResolvedAddress input6 = MakeAddr6(kIPv6, sizeof(kIPv6));
  ASSERT_TRUE(!ResolvedAddressToV4Mapped(input6, &output6));
  // Output is unchanged.
  ASSERT_EQ(memcmp(expect6.address(), output6.address(), output6.size()), 0);

  // Already-v4mapped input should also fail.
  input6 = MakeAddr6(kMapped, sizeof(kMapped));
  ASSERT_TRUE(!ResolvedAddressToV4Mapped(input6, &output6));
}

TEST(TcpSocketUtilsTest, ResolvedAddressToStringTest) {
  errno = 0x7EADBEEF;

  EventEngine::ResolvedAddress input4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  EXPECT_EQ(ResolvedAddressToString(input4).value(), "192.0.2.1:12345");
  EventEngine::ResolvedAddress input6 = MakeAddr6(kIPv6, sizeof(kIPv6));
  EXPECT_EQ(ResolvedAddressToString(input6).value(), "[2001:db8::1]:12345");
  SetIPv6ScopeId(input6, 2);
  EXPECT_EQ(ResolvedAddressToString(input6).value(), "[2001:db8::1%2]:12345");
  SetIPv6ScopeId(input6, 101);
  EXPECT_EQ(ResolvedAddressToString(input6).value(), "[2001:db8::1%101]:12345");
  EventEngine::ResolvedAddress input6x = MakeAddr6(kMapped, sizeof(kMapped));
  EXPECT_EQ(ResolvedAddressToString(input6x).value(),
            "[::ffff:192.0.2.1]:12345");
  EventEngine::ResolvedAddress input6y =
      MakeAddr6(kNotQuiteMapped, sizeof(kNotQuiteMapped));
  EXPECT_EQ(ResolvedAddressToString(input6y).value(),
            "[::fffe:c000:263]:12345");
  EventEngine::ResolvedAddress phony;
  memset(const_cast<sockaddr*>(phony.address()), 0, phony.size());
  sockaddr* phony_addr = const_cast<sockaddr*>(phony.address());
  phony_addr->sa_family = 123;
  EXPECT_EQ(ResolvedAddressToString(phony).status(),
            absl::InvalidArgumentError("Unknown sockaddr family: 123"));
}

TEST(TcpSocketUtilsTest, ResolvedAddressToNormalizedStringTest) {
  errno = 0x7EADBEEF;

  EventEngine::ResolvedAddress input4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  EXPECT_EQ(ResolvedAddressToNormalizedString(input4).value(),
            "192.0.2.1:12345");
  EventEngine::ResolvedAddress input6 = MakeAddr6(kIPv6, sizeof(kIPv6));
  EXPECT_EQ(ResolvedAddressToNormalizedString(input6).value(),
            "[2001:db8::1]:12345");
  SetIPv6ScopeId(input6, 2);
  EXPECT_EQ(ResolvedAddressToNormalizedString(input6).value(),
            "[2001:db8::1%2]:12345");
  SetIPv6ScopeId(input6, 101);
  EXPECT_EQ(ResolvedAddressToNormalizedString(input6).value(),
            "[2001:db8::1%101]:12345");
  EventEngine::ResolvedAddress input6x = MakeAddr6(kMapped, sizeof(kMapped));
  EXPECT_EQ(ResolvedAddressToNormalizedString(input6x).value(),
            "192.0.2.1:12345");
  EventEngine::ResolvedAddress input6y =
      MakeAddr6(kNotQuiteMapped, sizeof(kNotQuiteMapped));
  EXPECT_EQ(ResolvedAddressToNormalizedString(input6y).value(),
            "[::fffe:c000:263]:12345");
  EventEngine::ResolvedAddress phony;
  memset(const_cast<sockaddr*>(phony.address()), 0, phony.size());
  sockaddr* phony_addr = const_cast<sockaddr*>(phony.address());
  phony_addr->sa_family = 123;
  EXPECT_EQ(ResolvedAddressToNormalizedString(phony).status(),
            absl::InvalidArgumentError("Unknown sockaddr family: 123"));

#ifdef GRPC_HAVE_UNIX_SOCKET
  EventEngine::ResolvedAddress inputun =
      *UnixSockaddrPopulate("/some/unix/path");
  struct sockaddr_un* sock_un = reinterpret_cast<struct sockaddr_un*>(
      const_cast<sockaddr*>(inputun.address()));
  EXPECT_EQ(ResolvedAddressToNormalizedString(inputun).value(),
            "/some/unix/path");
  std::string max_filepath(sizeof(sock_un->sun_path) - 1, 'x');
  inputun = *UnixSockaddrPopulate(max_filepath);
  EXPECT_EQ(ResolvedAddressToNormalizedString(inputun).value(), max_filepath);
  inputun = *UnixSockaddrPopulate(max_filepath);
  sock_un->sun_path[sizeof(sockaddr_un::sun_path) - 1] = 'x';
  EXPECT_EQ(ResolvedAddressToNormalizedString(inputun).status(),
            absl::InvalidArgumentError("UDS path is not null-terminated"));
  EventEngine::ResolvedAddress inputun2 =
      *UnixAbstractSockaddrPopulate("some_unix_path");
  EXPECT_EQ(ResolvedAddressToNormalizedString(inputun2).value(),
            absl::StrCat(std::string(1, '\0'), "some_unix_path"));
  std::string max_abspath(sizeof(sock_un->sun_path) - 1, '\0');
  EventEngine::ResolvedAddress inputun3 =
      *UnixAbstractSockaddrPopulate(max_abspath);
  EXPECT_EQ(ResolvedAddressToNormalizedString(inputun3).value(),
            absl::StrCat(std::string(1, '\0'), max_abspath));
#endif

#ifdef GRPC_HAVE_VSOCK
  EventEngine::ResolvedAddress inputvm = *VSockaddrPopulate("-1:12345");
  EXPECT_EQ(ResolvedAddressToNormalizedString(inputvm).value(),
            absl::StrCat((uint32_t)-1, ":12345"));
#endif
}

TEST(TcpSocketUtilsTest, SockAddrPortTest) {
  EventEngine::ResolvedAddress wild6 = ResolvedAddressMakeWild6(20);
  EventEngine::ResolvedAddress wild4 = ResolvedAddressMakeWild4(20);
  // Verify the string description matches the expected wildcard address with
  // correct port number.
  EXPECT_EQ(ResolvedAddressToNormalizedString(wild6).value(), "[::]:20");
  EXPECT_EQ(ResolvedAddressToNormalizedString(wild4).value(), "0.0.0.0:20");
  // Update the port values.
  ResolvedAddressSetPort(wild4, 21);
  ResolvedAddressSetPort(wild6, 22);
  // Read back the port values.
  EXPECT_EQ(ResolvedAddressGetPort(wild4), 21);
  EXPECT_EQ(ResolvedAddressGetPort(wild6), 22);
  // Ensure the string description reflects the updated port values.
  EXPECT_EQ(ResolvedAddressToNormalizedString(wild4).value(), "0.0.0.0:21");
  EXPECT_EQ(ResolvedAddressToNormalizedString(wild6).value(), "[::]:22");
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
