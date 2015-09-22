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

static grpc_pollset g_pollset;
static int g_nconnects = 0;

static void on_connect(grpc_exec_ctx *exec_ctx, void *arg, grpc_endpoint *tcp) {
  grpc_endpoint_shutdown(exec_ctx, tcp);
  grpc_endpoint_destroy(exec_ctx, tcp);

  gpr_mu_lock(GRPC_POLLSET_MU(&g_pollset));
  g_nconnects++;
  grpc_pollset_kick(&g_pollset, NULL);
  gpr_mu_unlock(GRPC_POLLSET_MU(&g_pollset));
}

static void test_no_op(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_tcp_server *s = grpc_tcp_server_create();
  grpc_tcp_server_destroy(&exec_ctx, s, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_no_op_with_start(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_tcp_server *s = grpc_tcp_server_create();
  LOG_TEST("test_no_op_with_start");
  grpc_tcp_server_start(&exec_ctx, s, NULL, 0, on_connect, NULL);
  grpc_tcp_server_destroy(&exec_ctx, s, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_no_op_with_port(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  struct sockaddr_in addr;
  grpc_tcp_server *s = grpc_tcp_server_create();
  LOG_TEST("test_no_op_with_port");

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  GPR_ASSERT(
      grpc_tcp_server_add_port(s, (struct sockaddr *)&addr, sizeof(addr)));

  grpc_tcp_server_destroy(&exec_ctx, s, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_no_op_with_port_and_start(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  struct sockaddr_in addr;
  grpc_tcp_server *s = grpc_tcp_server_create();
  LOG_TEST("test_no_op_with_port_and_start");

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  GPR_ASSERT(
      grpc_tcp_server_add_port(s, (struct sockaddr *)&addr, sizeof(addr)));

  grpc_tcp_server_start(&exec_ctx, s, NULL, 0, on_connect, NULL);

  grpc_tcp_server_destroy(&exec_ctx, s, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_connect(int n) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  int svrfd, clifd;
  grpc_tcp_server *s = grpc_tcp_server_create();
  int nconnects_before;
  gpr_timespec deadline;
  grpc_pollset *pollsets[1];
  int i;
  LOG_TEST("test_connect");
  gpr_log(GPR_INFO, "clients=%d", n);

  memset(&addr, 0, sizeof(addr));
  addr.ss_family = AF_INET;
  GPR_ASSERT(grpc_tcp_server_add_port(s, (struct sockaddr *)&addr, addr_len));

  svrfd = grpc_tcp_server_get_fd(s, 0);
  GPR_ASSERT(svrfd >= 0);
  GPR_ASSERT(getsockname(svrfd, (struct sockaddr *)&addr, &addr_len) == 0);
  GPR_ASSERT(addr_len <= sizeof(addr));

  pollsets[0] = &g_pollset;
  grpc_tcp_server_start(&exec_ctx, s, pollsets, 1, on_connect, NULL);

  gpr_mu_lock(GRPC_POLLSET_MU(&g_pollset));

  for (i = 0; i < n; i++) {
    deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(10);

    nconnects_before = g_nconnects;
    clifd = socket(addr.ss_family, SOCK_STREAM, 0);
    GPR_ASSERT(clifd >= 0);
    gpr_log(GPR_DEBUG, "start connect");
    GPR_ASSERT(connect(clifd, (struct sockaddr *)&addr, addr_len) == 0);

    gpr_log(GPR_DEBUG, "wait");
    while (g_nconnects == nconnects_before &&
           gpr_time_cmp(deadline, gpr_now(deadline.clock_type)) > 0) {
      grpc_pollset_worker worker;
      grpc_pollset_work(&exec_ctx, &g_pollset, &worker,
                        gpr_now(GPR_CLOCK_MONOTONIC), deadline);
      gpr_mu_unlock(GRPC_POLLSET_MU(&g_pollset));
      grpc_exec_ctx_finish(&exec_ctx);
      gpr_mu_lock(GRPC_POLLSET_MU(&g_pollset));
    }
    gpr_log(GPR_DEBUG, "wait done");

    GPR_ASSERT(g_nconnects == nconnects_before + 1);
    close(clifd);
  }

  gpr_mu_unlock(GRPC_POLLSET_MU(&g_pollset));

  grpc_tcp_server_destroy(&exec_ctx, s, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void destroy_pollset(grpc_exec_ctx *exec_ctx, void *p, int success) {
  grpc_pollset_destroy(p);
}

int main(int argc, char **argv) {
  grpc_closure destroyed;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_test_init(argc, argv);
  grpc_iomgr_init();
  grpc_pollset_init(&g_pollset);

  test_no_op();
  test_no_op_with_start();
  test_no_op_with_port();
  test_no_op_with_port_and_start();
  test_connect(1);
  test_connect(10);

  grpc_closure_init(&destroyed, destroy_pollset, &g_pollset);
  grpc_pollset_shutdown(&exec_ctx, &g_pollset, &destroyed);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_iomgr_shutdown();
  return 0;
}
