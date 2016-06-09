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

#include "src/core/lib/iomgr/udp_server.h"

#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/util/test_config.h"

#ifdef GRPC_NEED_UDP

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", #x)

static grpc_pollset *g_pollset;
static gpr_mu *g_mu;
static int g_number_of_reads = 0;
static int g_number_of_bytes_read = 0;
static int g_number_of_orphan_calls = 0;

static void on_read(grpc_exec_ctx *exec_ctx, grpc_fd *emfd,
                    grpc_server *server) {
  char read_buffer[512];
  ssize_t byte_count;

  gpr_mu_lock(g_mu);
  byte_count =
      recv(grpc_fd_wrapped_fd(emfd), read_buffer, sizeof(read_buffer), 0);

  g_number_of_reads++;
  g_number_of_bytes_read += (int)byte_count;

  grpc_pollset_kick(g_pollset, NULL);
  gpr_mu_unlock(g_mu);
}

static void on_fd_orphaned(grpc_fd *emfd) {
  gpr_log(GPR_INFO, "gRPC FD about to be orphaned: %d",
          grpc_fd_wrapped_fd(emfd));
  g_number_of_orphan_calls++;
}

static void test_no_op(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_udp_server *s = grpc_udp_server_create();
  grpc_udp_server_destroy(&exec_ctx, s, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_no_op_with_start(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_udp_server *s = grpc_udp_server_create();
  LOG_TEST("test_no_op_with_start");
  grpc_udp_server_start(&exec_ctx, s, NULL, 0, NULL);
  grpc_udp_server_destroy(&exec_ctx, s, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_no_op_with_port(void) {
  g_number_of_orphan_calls = 0;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  struct sockaddr_in addr;
  grpc_udp_server *s = grpc_udp_server_create();
  LOG_TEST("test_no_op_with_port");

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  GPR_ASSERT(grpc_udp_server_add_port(s, (struct sockaddr *)&addr, sizeof(addr),
                                      on_read, on_fd_orphaned));

  grpc_udp_server_destroy(&exec_ctx, s, NULL);
  grpc_exec_ctx_finish(&exec_ctx);

  /* The server had a single FD, which should have been orphaned. */
  GPR_ASSERT(g_number_of_orphan_calls == 1);
}

static void test_no_op_with_port_and_start(void) {
  g_number_of_orphan_calls = 0;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  struct sockaddr_in addr;
  grpc_udp_server *s = grpc_udp_server_create();
  LOG_TEST("test_no_op_with_port_and_start");

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  GPR_ASSERT(grpc_udp_server_add_port(s, (struct sockaddr *)&addr, sizeof(addr),
                                      on_read, on_fd_orphaned));

  grpc_udp_server_start(&exec_ctx, s, NULL, 0, NULL);

  grpc_udp_server_destroy(&exec_ctx, s, NULL);
  grpc_exec_ctx_finish(&exec_ctx);

  /* The server had a single FD, which should have been orphaned. */
  GPR_ASSERT(g_number_of_orphan_calls == 1);
}

static void test_receive(int number_of_clients) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  int clifd, svrfd;
  grpc_udp_server *s = grpc_udp_server_create();
  int i;
  int number_of_reads_before;
  gpr_timespec deadline;
  grpc_pollset *pollsets[1];
  LOG_TEST("test_receive");
  gpr_log(GPR_INFO, "clients=%d", number_of_clients);

  g_number_of_bytes_read = 0;
  g_number_of_orphan_calls = 0;

  memset(&addr, 0, sizeof(addr));
  addr.ss_family = AF_INET;
  GPR_ASSERT(grpc_udp_server_add_port(s, (struct sockaddr *)&addr, addr_len,
                                      on_read, on_fd_orphaned));

  svrfd = grpc_udp_server_get_fd(s, 0);
  GPR_ASSERT(svrfd >= 0);
  GPR_ASSERT(getsockname(svrfd, (struct sockaddr *)&addr, &addr_len) == 0);
  GPR_ASSERT(addr_len <= sizeof(addr));

  pollsets[0] = g_pollset;
  grpc_udp_server_start(&exec_ctx, s, pollsets, 1, NULL);

  gpr_mu_lock(g_mu);

  for (i = 0; i < number_of_clients; i++) {
    deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(10);

    number_of_reads_before = g_number_of_reads;
    /* Create a socket, send a packet to the UDP server. */
    clifd = socket(addr.ss_family, SOCK_DGRAM, 0);
    GPR_ASSERT(clifd >= 0);
    GPR_ASSERT(connect(clifd, (struct sockaddr *)&addr, addr_len) == 0);
    GPR_ASSERT(5 == write(clifd, "hello", 5));
    while (g_number_of_reads == number_of_reads_before &&
           gpr_time_cmp(deadline, gpr_now(deadline.clock_type)) > 0) {
      grpc_pollset_worker *worker = NULL;
      grpc_pollset_work(&exec_ctx, g_pollset, &worker,
                        gpr_now(GPR_CLOCK_MONOTONIC), deadline);
      gpr_mu_unlock(g_mu);
      grpc_exec_ctx_finish(&exec_ctx);
      gpr_mu_lock(g_mu);
    }
    GPR_ASSERT(g_number_of_reads == number_of_reads_before + 1);
    close(clifd);
  }
  GPR_ASSERT(g_number_of_bytes_read == 5 * number_of_clients);

  gpr_mu_unlock(g_mu);

  grpc_udp_server_destroy(&exec_ctx, s, NULL);
  grpc_exec_ctx_finish(&exec_ctx);

  /* The server had a single FD, which should have been orphaned. */
  GPR_ASSERT(g_number_of_orphan_calls == 1);
}

static void destroy_pollset(grpc_exec_ctx *exec_ctx, void *p, bool success) {
  grpc_pollset_destroy(p);
}

int main(int argc, char **argv) {
  grpc_closure destroyed;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_test_init(argc, argv);
  grpc_init();
  g_pollset = gpr_malloc(grpc_pollset_size());
  grpc_pollset_init(g_pollset, &g_mu);

  test_no_op();
  test_no_op_with_start();
  test_no_op_with_port();
  test_no_op_with_port_and_start();
  test_receive(1);
  test_receive(10);

  grpc_closure_init(&destroyed, destroy_pollset, g_pollset);
  grpc_pollset_shutdown(&exec_ctx, g_pollset, &destroyed);
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_free(g_pollset);
  grpc_iomgr_shutdown();
  return 0;
}

#else

int main(int argc, char **argv) { return 0; }

#endif
