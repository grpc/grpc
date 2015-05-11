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

static gpr_timespec test_deadline(void) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(10);
}

static void must_succeed(void *arg, grpc_endpoint *tcp) {
  GPR_ASSERT(tcp);
  grpc_endpoint_shutdown(tcp);
  grpc_endpoint_destroy(tcp);
  gpr_event_set(arg, (void *)1);
}

static void must_fail(void *arg, grpc_endpoint *tcp) {
  GPR_ASSERT(!tcp);
  gpr_event_set(arg, (void *)1);
}

void test_succeeds(void) {
  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  int svr_fd;
  int r;
  gpr_event ev;

  gpr_event_init(&ev);

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;

  /* create a dummy server */
  svr_fd = socket(AF_INET, SOCK_STREAM, 0);
  GPR_ASSERT(svr_fd >= 0);
  GPR_ASSERT(0 == bind(svr_fd, (struct sockaddr *)&addr, addr_len));
  GPR_ASSERT(0 == listen(svr_fd, 1));

  /* connect to it */
  GPR_ASSERT(getsockname(svr_fd, (struct sockaddr *)&addr, &addr_len) == 0);
  grpc_tcp_client_connect(must_succeed, &ev, &g_pollset_set, (struct sockaddr *)&addr, addr_len,
                          gpr_inf_future);

  /* await the connection */
  do {
    addr_len = sizeof(addr);
    r = accept(svr_fd, (struct sockaddr *)&addr, &addr_len);
  } while (r == -1 && errno == EINTR);
  GPR_ASSERT(r >= 0);
  close(r);

  /* wait for the connection callback to finish */
  GPR_ASSERT(gpr_event_wait(&ev, test_deadline()));
}

void test_fails(void) {
  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  gpr_event ev;

  gpr_event_init(&ev);

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;

  /* connect to a broken address */
  grpc_tcp_client_connect(must_fail, &ev, &g_pollset_set, (struct sockaddr *)&addr, addr_len,
                          gpr_inf_future);

  /* wait for the connection callback to finish */
  GPR_ASSERT(gpr_event_wait(&ev, test_deadline()));
}

void test_times_out(void) {
  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  int svr_fd;
#define NUM_CLIENT_CONNECTS 10
  int client_fd[NUM_CLIENT_CONNECTS];
  int i;
  int r;
  gpr_event ev;
  gpr_timespec connect_deadline;

  gpr_event_init(&ev);

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

  grpc_tcp_client_connect(must_fail, &ev, &g_pollset_set, (struct sockaddr *)&addr, addr_len,
                          connect_deadline);
  /* Make sure the event doesn't trigger early */
  GPR_ASSERT(!gpr_event_wait(&ev, GRPC_TIMEOUT_MILLIS_TO_DEADLINE(500)));
  /* Now wait until it should have triggered */
  sleep(1);

  /* wait for the connection callback to finish */
  GPR_ASSERT(gpr_event_wait(&ev, test_deadline()));
  close(svr_fd);
  for (i = 0; i < NUM_CLIENT_CONNECTS; ++i) {
    close(client_fd[i]);
  }
}

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
  grpc_pollset_destroy(&g_pollset);
  grpc_iomgr_shutdown();
  return 0;
}
