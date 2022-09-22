// Copyright 2022 The gRPC Authors
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

#include <sys/socket.h>

#include "absl/status/status.h"

#include "src/core/lib/iomgr/port.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/un.h>
#endif

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/socket_mutator.h"

namespace grpc_event_engine {
namespace posix_engine {

namespace {

struct test_socket_mutator {
  grpc_socket_mutator base;
  int option_value;
};

bool MutateFd(int fd, grpc_socket_mutator* mutator) {
  int newval;
  socklen_t intlen = sizeof(newval);
  struct test_socket_mutator* m =
      reinterpret_cast<struct test_socket_mutator*>(mutator);

  if (0 != setsockopt(fd, IPPROTO_IP, IP_TOS, &m->option_value,
                      sizeof(m->option_value))) {
    return false;
  }
  if (0 != getsockopt(fd, IPPROTO_IP, IP_TOS, &newval, &intlen)) {
    return false;
  }
  if (newval != m->option_value) {
    return false;
  }
  return true;
}

bool MutateFd2(const grpc_mutate_socket_info* info,
               grpc_socket_mutator* mutator) {
  int newval;
  socklen_t intlen = sizeof(newval);
  struct test_socket_mutator* m =
      reinterpret_cast<struct test_socket_mutator*>(mutator);

  if (0 != setsockopt(info->fd, IPPROTO_IP, IP_TOS, &m->option_value,
                      sizeof(m->option_value))) {
    return false;
  }
  if (0 != getsockopt(info->fd, IPPROTO_IP, IP_TOS, &newval, &intlen)) {
    return false;
  }
  if (newval != m->option_value) {
    return false;
  }
  return true;
}

void DestroyTestMutator(grpc_socket_mutator* mutator) {
  struct test_socket_mutator* m =
      reinterpret_cast<struct test_socket_mutator*>(mutator);
  gpr_free(m);
}

int CompareTestMutator(grpc_socket_mutator* a, grpc_socket_mutator* b) {
  struct test_socket_mutator* ma =
      reinterpret_cast<struct test_socket_mutator*>(a);
  struct test_socket_mutator* mb =
      reinterpret_cast<struct test_socket_mutator*>(b);
  return grpc_core::QsortCompare(ma->option_value, mb->option_value);
}

const grpc_socket_mutator_vtable mutator_vtable = {MutateFd, CompareTestMutator,
                                                   DestroyTestMutator, nullptr};

const grpc_socket_mutator_vtable mutator_vtable2 = {
    nullptr, CompareTestMutator, DestroyTestMutator, MutateFd2};

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

void SetIPv6ScopeId(EventEngine::ResolvedAddress* addr, uint32_t scope_id) {
  sockaddr_in6* addr6 =
      reinterpret_cast<sockaddr_in6*>(const_cast<sockaddr*>(addr->address()));
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
#endif

}  // namespace

TEST(TcpPosixSocketUtilsTest, SocketMutatorTest) {
  auto test_with_vtable = [](const grpc_socket_mutator_vtable* vtable) {
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      // Try ipv6
      sock = socket(AF_INET6, SOCK_STREAM, 0);
    }
    EXPECT_GT(sock, 0);
    PosixSocketWrapper posix_sock(sock);
    struct test_socket_mutator mutator;
    grpc_socket_mutator_init(&mutator.base, vtable);

    mutator.option_value = IPTOS_LOWDELAY;
    EXPECT_TRUE(
        posix_sock
            .SetSocketMutator(GRPC_FD_CLIENT_CONNECTION_USAGE,
                              reinterpret_cast<grpc_socket_mutator*>(&mutator))
            .ok());
    mutator.option_value = IPTOS_THROUGHPUT;
    EXPECT_TRUE(
        posix_sock
            .SetSocketMutator(GRPC_FD_CLIENT_CONNECTION_USAGE,
                              reinterpret_cast<grpc_socket_mutator*>(&mutator))
            .ok());

    mutator.option_value = IPTOS_RELIABILITY;
    EXPECT_TRUE(
        posix_sock
            .SetSocketMutator(GRPC_FD_CLIENT_CONNECTION_USAGE,
                              reinterpret_cast<grpc_socket_mutator*>(&mutator))
            .ok());

    mutator.option_value = -1;
    EXPECT_FALSE(
        posix_sock
            .SetSocketMutator(GRPC_FD_CLIENT_CONNECTION_USAGE,
                              reinterpret_cast<grpc_socket_mutator*>(&mutator))
            .ok());
    close(sock);
  };
  test_with_vtable(&mutator_vtable);
  test_with_vtable(&mutator_vtable2);
}

TEST(TcpPosixSocketUtilsTest, SocketOptionsTest) {
  int sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    // Try ipv6
    sock = socket(AF_INET6, SOCK_STREAM, 0);
  }
  EXPECT_GT(sock, 0);
  PosixSocketWrapper posix_sock(sock);
  EXPECT_TRUE(posix_sock.SetSocketNonBlocking(1).ok());
  EXPECT_TRUE(posix_sock.SetSocketNonBlocking(0).ok());
  EXPECT_TRUE(posix_sock.SetSocketCloexec(1).ok());
  EXPECT_TRUE(posix_sock.SetSocketCloexec(0).ok());
  EXPECT_TRUE(posix_sock.SetSocketReuseAddr(1).ok());
  EXPECT_TRUE(posix_sock.SetSocketReuseAddr(0).ok());
  EXPECT_TRUE(posix_sock.SetSocketLowLatency(1).ok());
  EXPECT_TRUE(posix_sock.SetSocketLowLatency(0).ok());
  close(sock);
}

TEST(SockAddrUtilsTest, SockAddrIsV4MappedTest) {
  // v4mapped input should succeed.
  EventEngine::ResolvedAddress input6 = MakeAddr6(kMapped, sizeof(kMapped));
  ASSERT_TRUE(SockaddrIsV4Mapped(&input6, nullptr));
  EventEngine::ResolvedAddress output4;
  ASSERT_TRUE(SockaddrIsV4Mapped(&input6, &output4));
  EventEngine::ResolvedAddress expect4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  ASSERT_EQ(memcmp(expect4.address(), output4.address(), expect4.size()), 0);

  // Non-v4mapped input should fail.
  input6 = MakeAddr6(kNotQuiteMapped, sizeof(kNotQuiteMapped));
  ASSERT_FALSE(SockaddrIsV4Mapped(&input6, nullptr));
  ASSERT_FALSE(SockaddrIsV4Mapped(&input6, &output4));
  // Output is unchanged.
  ASSERT_EQ(memcmp(expect4.address(), output4.address(), expect4.size()), 0);

  // Plain IPv4 input should also fail.
  EventEngine::ResolvedAddress input4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  ASSERT_FALSE(SockaddrIsV4Mapped(&input4, nullptr));
}

TEST(TcpPosixSocketUtilsTest, SockAddrToV4MappedTest) {
  // IPv4 input should succeed.
  EventEngine::ResolvedAddress input4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  EventEngine::ResolvedAddress output6;
  ASSERT_TRUE(SockaddrToV4Mapped(&input4, &output6));
  EventEngine::ResolvedAddress expect6 = MakeAddr6(kMapped, sizeof(kMapped));
  ASSERT_EQ(memcmp(expect6.address(), output6.address(), output6.size()), 0);

  // IPv6 input should fail.
  EventEngine::ResolvedAddress input6 = MakeAddr6(kIPv6, sizeof(kIPv6));
  ASSERT_TRUE(!SockaddrToV4Mapped(&input6, &output6));
  // Output is unchanged.
  ASSERT_EQ(memcmp(expect6.address(), output6.address(), output6.size()), 0);

  // Already-v4mapped input should also fail.
  input6 = MakeAddr6(kMapped, sizeof(kMapped));
  ASSERT_TRUE(!SockaddrToV4Mapped(&input6, &output6));
}

TEST(TcpPosixSocketUtilsTest, SockAddrToStringTest) {
  errno = 0x7EADBEEF;

  EventEngine::ResolvedAddress input4 = MakeAddr4(kIPv4, sizeof(kIPv4));
  EXPECT_EQ(SockaddrToString(&input4, false).value(), "192.0.2.1:12345");
  EXPECT_EQ(SockaddrToString(&input4, true).value(), "192.0.2.1:12345");

  EventEngine::ResolvedAddress input6 = MakeAddr6(kIPv6, sizeof(kIPv6));
  EXPECT_EQ(SockaddrToString(&input6, false).value(), "[2001:db8::1]:12345");
  EXPECT_EQ(SockaddrToString(&input6, true).value(), "[2001:db8::1]:12345");

  SetIPv6ScopeId(&input6, 2);
  EXPECT_EQ(SockaddrToString(&input6, false).value(), "[2001:db8::1%2]:12345");
  EXPECT_EQ(SockaddrToString(&input6, true).value(), "[2001:db8::1%2]:12345");

  SetIPv6ScopeId(&input6, 101);
  EXPECT_EQ(SockaddrToString(&input6, false).value(),
            "[2001:db8::1%101]:12345");
  EXPECT_EQ(SockaddrToString(&input6, true).value(), "[2001:db8::1%101]:12345");

  EventEngine::ResolvedAddress input6x = MakeAddr6(kMapped, sizeof(kMapped));
  EXPECT_EQ(SockaddrToString(&input6x, false).value(),
            "[::ffff:192.0.2.1]:12345");
  EXPECT_EQ(SockaddrToString(&input6x, true).value(), "192.0.2.1:12345");

  EventEngine::ResolvedAddress input6y =
      MakeAddr6(kNotQuiteMapped, sizeof(kNotQuiteMapped));
  EXPECT_EQ(SockaddrToString(&input6y, false).value(),
            "[::fffe:c000:263]:12345");
  EXPECT_EQ(SockaddrToString(&input6y, true).value(),
            "[::fffe:c000:263]:12345");

  EventEngine::ResolvedAddress phony;
  memset(const_cast<sockaddr*>(phony.address()), 0, phony.size());
  sockaddr* phony_addr = const_cast<sockaddr*>(phony.address());
  phony_addr->sa_family = 123;
  EXPECT_EQ(SockaddrToString(&phony, false).status(),
            absl::InvalidArgumentError("Unknown sockaddr family: 123"));
  EXPECT_EQ(SockaddrToString(&phony, true).status(),
            absl::InvalidArgumentError("Unknown sockaddr family: 123"));

#ifdef GRPC_HAVE_UNIX_SOCKET
  EventEngine::ResolvedAddress inputun =
      *UnixSockaddrPopulate("/some/unix/path");
  struct sockaddr_un* sock_un = reinterpret_cast<struct sockaddr_un*>(
      const_cast<sockaddr*>(inputun.address()));
  EXPECT_EQ(SockaddrToString(&inputun, true).value(), "/some/unix/path");

  std::string max_filepath(sizeof(sock_un->sun_path) - 1, 'x');
  inputun = *UnixSockaddrPopulate(max_filepath);
  EXPECT_EQ(SockaddrToString(&inputun, true).value(), max_filepath);

  inputun = *UnixSockaddrPopulate(max_filepath);
  sock_un->sun_path[sizeof(sockaddr_un::sun_path) - 1] = 'x';
  EXPECT_EQ(SockaddrToString(&inputun, true).status(),
            absl::InvalidArgumentError("UDS path is not null-terminated"));

  EventEngine::ResolvedAddress inputun2 =
      *UnixAbstractSockaddrPopulate("some_unix_path");
  EXPECT_EQ(SockaddrToString(&inputun2, true).value(),
            absl::StrCat(std::string(1, '\0'), "some_unix_path"));

  std::string max_abspath(sizeof(sock_un->sun_path) - 1, '\0');
  EventEngine::ResolvedAddress inputun3 =
      *UnixAbstractSockaddrPopulate(max_abspath);
  EXPECT_EQ(SockaddrToString(&inputun3, true).value(),
            absl::StrCat(std::string(1, '\0'), max_abspath));
#endif
}

}  // namespace posix_engine
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#else /* GRPC_POSIX_SOCKET_UTILS_COMMON */

int main(int argc, char** argv) { return 1; }

#endif /* GRPC_POSIX_SOCKET_UTILS_COMMON */
