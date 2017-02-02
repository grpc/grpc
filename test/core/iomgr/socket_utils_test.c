/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/iomgr/port.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/socket_utils_posix.h"

#include <errno.h>
#include <netinet/ip.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>
#include "src/core/lib/iomgr/socket_mutator.h"
#include "test/core/util/test_config.h"

struct test_socket_mutator {
  grpc_socket_mutator base;
  int option_value;
};

static bool mutate_fd(int fd, grpc_socket_mutator *mutator) {
  int newval;
  socklen_t intlen = sizeof(newval);
  struct test_socket_mutator *m = (struct test_socket_mutator *)mutator;

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

static void destroy_test_mutator(grpc_socket_mutator *mutator) {
  struct test_socket_mutator *m = (struct test_socket_mutator *)mutator;
  gpr_free(m);
}

static int compare_test_mutator(grpc_socket_mutator *a,
                                grpc_socket_mutator *b) {
  struct test_socket_mutator *ma = (struct test_socket_mutator *)a;
  struct test_socket_mutator *mb = (struct test_socket_mutator *)b;
  return GPR_ICMP(ma->option_value, mb->option_value);
}

static const grpc_socket_mutator_vtable mutator_vtable = {
    mutate_fd, compare_test_mutator, destroy_test_mutator};

int main(int argc, char **argv) {
  int sock;
  grpc_error *err;
  grpc_test_init(argc, argv);

  sock = socket(PF_INET, SOCK_STREAM, 0);
  GPR_ASSERT(sock > 0);

  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_nonblocking",
                               grpc_set_socket_nonblocking(sock, 1)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_nonblocking",
                               grpc_set_socket_nonblocking(sock, 0)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_cloexec",
                               grpc_set_socket_cloexec(sock, 1)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_cloexec",
                               grpc_set_socket_cloexec(sock, 0)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_reuse_addr",
                               grpc_set_socket_reuse_addr(sock, 1)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_reuse_addr",
                               grpc_set_socket_reuse_addr(sock, 0)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_low_latency",
                               grpc_set_socket_low_latency(sock, 1)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("set_socket_low_latency",
                               grpc_set_socket_low_latency(sock, 0)));

  struct test_socket_mutator mutator;
  grpc_socket_mutator_init(&mutator.base, &mutator_vtable);

  mutator.option_value = IPTOS_LOWDELAY;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "set_socket_with_mutator",
      grpc_set_socket_with_mutator(sock, (grpc_socket_mutator *)&mutator)));

  mutator.option_value = IPTOS_THROUGHPUT;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "set_socket_with_mutator",
      grpc_set_socket_with_mutator(sock, (grpc_socket_mutator *)&mutator)));

  mutator.option_value = IPTOS_RELIABILITY;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "set_socket_with_mutator",
      grpc_set_socket_with_mutator(sock, (grpc_socket_mutator *)&mutator)));

  mutator.option_value = -1;
  err = grpc_set_socket_with_mutator(sock, (grpc_socket_mutator *)&mutator);
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  GRPC_ERROR_UNREF(err);

  close(sock);

  return 0;
}

#else /* GRPC_POSIX_SOCKET */

int main(int argc, char **argv) { return 1; }

#endif /* GRPC_POSIX_SOCKET */
