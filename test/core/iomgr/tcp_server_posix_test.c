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

#include "src/core/iomgr/tcp_server.h"
#include "src/core/iomgr/iomgr.h"
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include "test/core/util/test_config.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", #x)

static gpr_mu mu;
static gpr_cv cv;
static int nconnects = 0;

static void on_connect(void *arg, grpc_endpoint *tcp) {
  grpc_endpoint_shutdown(tcp);
  grpc_endpoint_destroy(tcp);

  gpr_mu_lock(&mu);
  nconnects++;
  gpr_cv_broadcast(&cv);
  gpr_mu_unlock(&mu);
}

static void test_no_op(void) {
  grpc_tcp_server *s = grpc_tcp_server_create();
  grpc_tcp_server_destroy(s, NULL, NULL);
}

static void test_no_op_with_start(void) {
  grpc_tcp_server *s = grpc_tcp_server_create();
  LOG_TEST("test_no_op_with_start");
  grpc_tcp_server_start(s, NULL, 0, on_connect, NULL);
  grpc_tcp_server_destroy(s, NULL, NULL);
}

static void test_no_op_with_port(void) {
  struct sockaddr_in addr;
  grpc_tcp_server *s = grpc_tcp_server_create();
  LOG_TEST("test_no_op_with_port");

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  GPR_ASSERT(
      grpc_tcp_server_add_port(s, (struct sockaddr *)&addr, sizeof(addr)));

  grpc_tcp_server_destroy(s, NULL, NULL);
}

static void test_no_op_with_port_and_start(void) {
  struct sockaddr_in addr;
  grpc_tcp_server *s = grpc_tcp_server_create();
  LOG_TEST("test_no_op_with_port_and_start");

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  GPR_ASSERT(
      grpc_tcp_server_add_port(s, (struct sockaddr *)&addr, sizeof(addr)));

  grpc_tcp_server_start(s, NULL, 0, on_connect, NULL);

  grpc_tcp_server_destroy(s, NULL, NULL);
}

static void test_connect(int n) {
  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  int svrfd, clifd;
  grpc_tcp_server *s = grpc_tcp_server_create();
  int nconnects_before;
  gpr_timespec deadline;
  int i;
  LOG_TEST("test_connect");
  gpr_log(GPR_INFO, "clients=%d", n);

  gpr_mu_lock(&mu);

  memset(&addr, 0, sizeof(addr));
  addr.ss_family = AF_INET;
  GPR_ASSERT(grpc_tcp_server_add_port(s, (struct sockaddr *)&addr, addr_len));

  svrfd = grpc_tcp_server_get_fd(s, 0);
  GPR_ASSERT(svrfd >= 0);
  GPR_ASSERT(getsockname(svrfd, (struct sockaddr *)&addr, &addr_len) == 0);
  GPR_ASSERT(addr_len <= sizeof(addr));

  grpc_tcp_server_start(s, NULL, 0, on_connect, NULL);

  for (i = 0; i < n; i++) {
    deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(1);

    nconnects_before = nconnects;
    clifd = socket(addr.ss_family, SOCK_STREAM, 0);
    GPR_ASSERT(clifd >= 0);
    GPR_ASSERT(connect(clifd, (struct sockaddr *)&addr, addr_len) == 0);

    while (nconnects == nconnects_before) {
      GPR_ASSERT(gpr_cv_wait(&cv, &mu, deadline) == 0);
    }

    GPR_ASSERT(nconnects == nconnects_before + 1);
    close(clifd);

    if (i != n - 1) {
      sleep(1);
    }
  }

  gpr_mu_unlock(&mu);

  grpc_tcp_server_destroy(s, NULL, NULL);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_iomgr_init();
  gpr_mu_init(&mu);
  gpr_cv_init(&cv);

  test_no_op();
  test_no_op_with_start();
  test_no_op_with_port();
  test_no_op_with_port_and_start();
  test_connect(1);
  test_connect(10);

  grpc_iomgr_shutdown();
  gpr_mu_destroy(&mu);
  gpr_cv_destroy(&cv);
  return 0;
}
