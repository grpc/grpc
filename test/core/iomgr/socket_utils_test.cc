//
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
//

#include "src/core/lib/iomgr/port.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>

#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/socket_mutator.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "test/core/util/test_config.h"

struct test_socket_mutator {
  grpc_socket_mutator base;
  int option_value;
};

static bool mutate_fd(int fd, grpc_socket_mutator* mutator) {
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

static bool mutate_fd_2(const grpc_mutate_socket_info* info,
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

static void destroy_test_mutator(grpc_socket_mutator* mutator) {
  struct test_socket_mutator* m =
      reinterpret_cast<struct test_socket_mutator*>(mutator);
  gpr_free(m);
}

static int compare_test_mutator(grpc_socket_mutator* a,
                                grpc_socket_mutator* b) {
  struct test_socket_mutator* ma =
      reinterpret_cast<struct test_socket_mutator*>(a);
  struct test_socket_mutator* mb =
      reinterpret_cast<struct test_socket_mutator*>(b);
  return grpc_core::QsortCompare(ma->option_value, mb->option_value);
}

static const grpc_socket_mutator_vtable mutator_vtable = {
    mutate_fd, compare_test_mutator, destroy_test_mutator, nullptr};

static const grpc_socket_mutator_vtable mutator_vtable2 = {
    nullptr, compare_test_mutator, destroy_test_mutator, mutate_fd_2};

static void test_with_vtable(const grpc_socket_mutator_vtable* vtable) {
  int sock = socket(PF_INET, SOCK_STREAM, 0);
  ASSERT_GT(sock, 0);

  struct test_socket_mutator mutator;
  grpc_socket_mutator_init(&mutator.base, vtable);

  mutator.option_value = IPTOS_LOWDELAY;
  ASSERT_TRUE(GRPC_LOG_IF_ERROR(
      "set_socket_with_mutator",
      grpc_set_socket_with_mutator(sock, GRPC_FD_CLIENT_CONNECTION_USAGE,
                                   (grpc_socket_mutator*)&mutator)));

  mutator.option_value = IPTOS_THROUGHPUT;
  ASSERT_TRUE(GRPC_LOG_IF_ERROR(
      "set_socket_with_mutator",
      grpc_set_socket_with_mutator(sock, GRPC_FD_CLIENT_CONNECTION_USAGE,
                                   (grpc_socket_mutator*)&mutator)));

  mutator.option_value = IPTOS_RELIABILITY;
  ASSERT_TRUE(GRPC_LOG_IF_ERROR(
      "set_socket_with_mutator",
      grpc_set_socket_with_mutator(sock, GRPC_FD_CLIENT_CONNECTION_USAGE,
                                   (grpc_socket_mutator*)&mutator)));

  mutator.option_value = -1;
  auto err = grpc_set_socket_with_mutator(
      sock, GRPC_FD_CLIENT_CONNECTION_USAGE,
      reinterpret_cast<grpc_socket_mutator*>(&mutator));
  ASSERT_FALSE(err.ok());
}

static void test_set_socket_dscp(int sock, int dscp) {
  // Get the initial IP_TOS byte that consisting of following bits:
  // | 7 6 5 4 3 2 | 1 0 |
  // |    DSCP     | ECN |
  int optval;
  socklen_t optlen = sizeof(optval);
  ASSERT_TRUE(getsockopt(sock, IPPROTO_IP, IP_TOS, &optval, &optlen) == 0);
  ASSERT_TRUE((optval >> 2) != dscp);

  ASSERT_TRUE(
      GRPC_LOG_IF_ERROR("set_socket_dscp", grpc_set_socket_dscp(sock, dscp)));

  // Verify that value was changed
  ASSERT_TRUE(getsockopt(sock, IPPROTO_IP, IP_TOS, &optval, &optlen) == 0);
  ASSERT_TRUE((optval >> 2) == dscp);
}

TEST(SocketUtilsTest, MainTest) {
  int sock;

  sock = socket(PF_INET, SOCK_STREAM, 0);
  ASSERT_GT(sock, 0);

  ASSERT_TRUE(GRPC_LOG_IF_ERROR("set_socket_nonblocking",
                                grpc_set_socket_nonblocking(sock, 1)));
  ASSERT_TRUE(GRPC_LOG_IF_ERROR("set_socket_nonblocking",
                                grpc_set_socket_nonblocking(sock, 0)));
  ASSERT_TRUE(GRPC_LOG_IF_ERROR("set_socket_cloexec",
                                grpc_set_socket_cloexec(sock, 1)));
  ASSERT_TRUE(GRPC_LOG_IF_ERROR("set_socket_cloexec",
                                grpc_set_socket_cloexec(sock, 0)));
  ASSERT_TRUE(GRPC_LOG_IF_ERROR("set_socket_reuse_addr",
                                grpc_set_socket_reuse_addr(sock, 1)));
  ASSERT_TRUE(GRPC_LOG_IF_ERROR("set_socket_reuse_addr",
                                grpc_set_socket_reuse_addr(sock, 0)));
  ASSERT_TRUE(GRPC_LOG_IF_ERROR("set_socket_low_latency",
                                grpc_set_socket_low_latency(sock, 1)));
  ASSERT_TRUE(GRPC_LOG_IF_ERROR("set_socket_low_latency",
                                grpc_set_socket_low_latency(sock, 0)));
  test_set_socket_dscp(sock, 16 /*CS2*/);

  close(sock);

  if (grpc_ipv6_loopback_available()) {
    sock = socket(AF_INET6, SOCK_STREAM, 0);
    GPR_ASSERT(sock > 0);

    test_set_socket_dscp(sock, 8 /*CS1*/);

    close(sock);
  }

  test_with_vtable(&mutator_vtable);
  test_with_vtable(&mutator_vtable2);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#else  // GRPC_POSIX_SOCKET_UTILS_COMMON

int main(int argc, char** argv) { return 1; }

#endif  // GRPC_POSIX_SOCKET_UTILS_COMMON
