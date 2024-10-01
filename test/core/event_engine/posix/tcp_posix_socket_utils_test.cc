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

#include <grpc/grpc.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/port.h"

// IWYU pragma: no_include <arpa/inet.h>

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

#include <grpc/support/alloc.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/socket_mutator.h"
#include "src/core/util/useful.h"

namespace grpc_event_engine {
namespace experimental {

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

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#else  // GRPC_POSIX_SOCKET_UTILS_COMMON

int main(int argc, char** argv) { return 0; }

#endif  // GRPC_POSIX_SOCKET_UTILS_COMMON
