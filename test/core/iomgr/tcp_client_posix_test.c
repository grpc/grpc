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

#include "src/core/iomgr/tcp_client.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/core/iomgr/iomgr.h"
#include "src/core/iomgr/socket_utils_posix.h"
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "test/core/util/test_config.h"

static grpc_pollset_set g_pollset_set;
static grpc_pollset g_pollset;
static int g_connections_complete = 0;

static gpr_timespec test_deadline(void) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(10);
}

static void finish_connection() {
  gpr_mu_lock(GRPC_POLLSET_MU(&g_pollset));
  g_connections_complete++;
  grpc_pollset_kick(&g_pollset);
  gpr_mu_unlock(GRPC_POLLSET_MU(&g_pollset));
}

static void must_succeed(void *arg, grpc_endpoint *tcp) {
  GPR_ASSERT(tcp);
  grpc_endpoint_shutdown(tcp);
  grpc_endpoint_destroy(tcp);
  finish_connection();
}

static void must_fail(void *arg, grpc_endpoint *tcp) {
  GPR_ASSERT(!tcp);
  finish_connection();
}

void test_succeeds(void) {
  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  int svr_fd;
  int r;
  int connections_complete_before;

  gpr_log(GPR_DEBUG, "test_succeeds");

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;

  /* create a dummy server */
  svr_fd = socket(AF_INET, SOCK_STREAM, 0);
  GPR_ASSERT(svr_fd >= 0);
  GPR_ASSERT(0 == bind(svr_fd, (struct sockaddr *)&addr, addr_len));
  GPR_ASSERT(0 == listen(svr_fd, 1));

  gpr_mu_lock(GRPC_POLLSET_MU(&g_pollset));
  connections_complete_before = g_connections_complete;
  gpr_mu_unlock(GRPC_POLLSET_MU(&g_pollset));

  /* connect to it */
  GPR_ASSERT(getsockname(svr_fd, (struct sockaddr *)&addr, &addr_len) == 0);
  grpc_tcp_client_connect(must_succeed, NULL, &g_pollset_set,
                          (struct sockaddr *)&addr, addr_len,
                          gpr_inf_future(GPR_CLOCK_REALTIME));

  /* await the connection */
  do {
    addr_len = sizeof(addr);
    r = accept(svr_fd, (struct sockaddr *)&addr, &addr_len);
  } while (r == -1 && errno == EINTR);
  GPR_ASSERT(r >= 0);
  close(r);

  gpr_mu_lock(GRPC_POLLSET_MU(&g_pollset));

  while (g_connections_complete == connections_complete_before) {
    grpc_pollset_work(&g_pollset, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5));
  }

  gpr_mu_unlock(GRPC_POLLSET_MU(&g_pollset));
}

void test_fails(void) {
  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  int connections_complete_before;

  gpr_log(GPR_DEBUG, "test_fails");

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;

  gpr_mu_lock(GRPC_POLLSET_MU(&g_pollset));
  connections_complete_before = g_connections_complete;
  gpr_mu_unlock(GRPC_POLLSET_MU(&g_pollset));

  /* connect to a broken address */
  grpc_tcp_client_connect(must_fail, NULL, &g_pollset_set,
                          (struct sockaddr *)&addr, addr_len,
                          gpr_inf_future(GPR_CLOCK_REALTIME));

  gpr_mu_lock(GRPC_POLLSET_MU(&g_pollset));

  /* wait for the connection callback to finish */
  while (g_connections_complete == connections_complete_before) {
    grpc_pollset_work(&g_pollset, test_deadline());
  }

  gpr_mu_unlock(GRPC_POLLSET_MU(&g_pollset));
}

void test_times_out(void) {
  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  int svr_fd;
#define NUM_CLIENT_CONNECTS 100
  int client_fd[NUM_CLIENT_CONNECTS];
  int i;
  int r;
  int connections_complete_before;
  gpr_timespec connect_deadline;

  gpr_log(GPR_DEBUG, "test_times_out");

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;

  /* create a dummy server */
  svr_fd = socket(AF_INET, SOCK_STREAM, 0);
  GPR_ASSERT(svr_fd >= 0);
  GPR_ASSERT(0 == bind(svr_fd, (struct sockaddr *)&addr, addr_len));
  GPR_ASSERT(0 == listen(svr_fd, 1));
  /* Get its address */
  GPR_ASSERT(getsockname(svr_fd, (struct sockaddr *)&addr, &addr_len) == 0);

  /* tie up the listen buffer, which is somewhat arbitrarily sized. */
  for (i = 0; i < NUM_CLIENT_CONNECTS; ++i) {
    client_fd[i] = socket(AF_INET, SOCK_STREAM, 0);
    grpc_set_socket_nonblocking(client_fd[i], 1);
    do {
      r = connect(client_fd[i], (struct sockaddr *)&addr, addr_len);
    } while (r == -1 && errno == EINTR);
    GPR_ASSERT(r < 0);
    GPR_ASSERT(errno == EWOULDBLOCK || errno == EINPROGRESS);
  }

  /* connect to dummy server address */

  connect_deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(1);

  gpr_mu_lock(GRPC_POLLSET_MU(&g_pollset));
  connections_complete_before = g_connections_complete;
  gpr_mu_unlock(GRPC_POLLSET_MU(&g_pollset));

  grpc_tcp_client_connect(must_fail, NULL, &g_pollset_set,
                          (struct sockaddr *)&addr, addr_len, connect_deadline);

  /* Make sure the event doesn't trigger early */
  gpr_mu_lock(GRPC_POLLSET_MU(&g_pollset));
  while (gpr_time_cmp(gpr_time_add(connect_deadline,
                                   gpr_time_from_seconds(2, GPR_TIMESPAN)),
                      gpr_now(GPR_CLOCK_REALTIME)) > 0) {
    int is_after_deadline =
        gpr_time_cmp(connect_deadline, gpr_now(GPR_CLOCK_REALTIME)) <= 0;
    if (is_after_deadline &&
        gpr_time_cmp(gpr_time_add(connect_deadline,
                                  gpr_time_from_seconds(1, GPR_TIMESPAN)),
                     gpr_now(GPR_CLOCK_REALTIME)) > 0) {
      /* allow some slack before insisting that things be done */
    } else {
      GPR_ASSERT(g_connections_complete ==
                 connections_complete_before + is_after_deadline);
    }
    grpc_pollset_work(&g_pollset, GRPC_TIMEOUT_MILLIS_TO_DEADLINE(10));
  }
  gpr_mu_unlock(GRPC_POLLSET_MU(&g_pollset));

  close(svr_fd);
  for (i = 0; i < NUM_CLIENT_CONNECTS; ++i) {
    close(client_fd[i]);
  }
}

static void destroy_pollset(void *p) { grpc_pollset_destroy(p); }

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_iomgr_init();
  grpc_pollset_set_init(&g_pollset_set);
  grpc_pollset_init(&g_pollset);
  grpc_pollset_set_add_pollset(&g_pollset_set, &g_pollset);
  test_succeeds();
  gpr_log(GPR_ERROR, "End of first test");
  test_fails();
  test_times_out();
  grpc_pollset_set_destroy(&g_pollset_set);
  grpc_pollset_shutdown(&g_pollset, destroy_pollset, &g_pollset);
  grpc_iomgr_shutdown();
  return 0;
}
