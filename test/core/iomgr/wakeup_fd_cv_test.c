/*
 *
 * Copyright 2016, Google Inc.
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

#ifdef GRPC_POSIX_SOCKET

#include <pthread.h>

#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_posix.h"
#include "src/core/lib/support/env.h"

typedef struct poll_args {
  struct pollfd *fds;
  nfds_t nfds;
  int timeout;
  int result;
} poll_args;

gpr_cv poll_cv;
gpr_mu poll_mu;
static int socket_event = 0;

// Trigger a "socket" POLLIN in mock_poll()
void trigger_socket_event() {
  gpr_mu_lock(&poll_mu);
  socket_event = 1;
  gpr_cv_broadcast(&poll_cv);
  gpr_mu_unlock(&poll_mu);
}

void reset_socket_event() {
  gpr_mu_lock(&poll_mu);
  socket_event = 0;
  gpr_mu_unlock(&poll_mu);
}

// Mocks posix poll() function
int mock_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
  int res = 0;
  gpr_timespec poll_time;
  gpr_mu_lock(&poll_mu);
  GPR_ASSERT(nfds == 3);
  GPR_ASSERT(fds[0].fd == 20);
  GPR_ASSERT(fds[1].fd == 30);
  GPR_ASSERT(fds[2].fd == 50);
  GPR_ASSERT(fds[0].events == (POLLIN | POLLHUP));
  GPR_ASSERT(fds[1].events == (POLLIN | POLLHUP));
  GPR_ASSERT(fds[2].events == POLLIN);

  if (timeout < 0) {
    poll_time = gpr_inf_future(GPR_CLOCK_REALTIME);
  } else {
    poll_time = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                             gpr_time_from_millis(timeout, GPR_TIMESPAN));
  }

  if (socket_event || !gpr_cv_wait(&poll_cv, &poll_mu, poll_time)) {
    fds[0].revents = POLLIN;
    res = 1;
  }
  gpr_mu_unlock(&poll_mu);
  return res;
}

void background_poll(void *args) {
  poll_args *pargs = (poll_args *)args;
  pargs->result = grpc_poll_function(pargs->fds, pargs->nfds, pargs->timeout);
}

void test_many_fds(void) {
  int i;
  grpc_wakeup_fd fd[1000];
  for (i = 0; i < 1000; i++) {
    GPR_ASSERT(grpc_wakeup_fd_init(&fd[i]) == GRPC_ERROR_NONE);
  }
  for (i = 0; i < 1000; i++) {
    grpc_wakeup_fd_destroy(&fd[i]);
  }
}

void test_poll_cv_trigger(void) {
  grpc_wakeup_fd cvfd1, cvfd2, cvfd3;
  struct pollfd pfds[6];
  poll_args pargs;
  gpr_thd_id t_id;
  gpr_thd_options opt;

  GPR_ASSERT(grpc_wakeup_fd_init(&cvfd1) == GRPC_ERROR_NONE);
  GPR_ASSERT(grpc_wakeup_fd_init(&cvfd2) == GRPC_ERROR_NONE);
  GPR_ASSERT(grpc_wakeup_fd_init(&cvfd3) == GRPC_ERROR_NONE);
  GPR_ASSERT(cvfd1.read_fd < 0);
  GPR_ASSERT(cvfd2.read_fd < 0);
  GPR_ASSERT(cvfd3.read_fd < 0);
  GPR_ASSERT(cvfd1.read_fd != cvfd2.read_fd);
  GPR_ASSERT(cvfd2.read_fd != cvfd3.read_fd);
  GPR_ASSERT(cvfd1.read_fd != cvfd3.read_fd);

  pfds[0].fd = cvfd1.read_fd;
  pfds[1].fd = cvfd2.read_fd;
  pfds[2].fd = 20;
  pfds[3].fd = 30;
  pfds[4].fd = cvfd3.read_fd;
  pfds[5].fd = 50;

  pfds[0].events = 0;
  pfds[1].events = POLLIN;
  pfds[2].events = POLLIN | POLLHUP;
  pfds[3].events = POLLIN | POLLHUP;
  pfds[4].events = POLLIN;
  pfds[5].events = POLLIN;

  pargs.fds = pfds;
  pargs.nfds = 6;
  pargs.timeout = 1000;
  pargs.result = -2;

  opt = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&opt);
  gpr_thd_new(&t_id, &background_poll, &pargs, &opt);

  // Wakeup wakeup_fd not listening for events
  GPR_ASSERT(grpc_wakeup_fd_wakeup(&cvfd1) == GRPC_ERROR_NONE);
  gpr_thd_join(t_id);
  GPR_ASSERT(pargs.result == 0);
  GPR_ASSERT(pfds[0].revents == 0);
  GPR_ASSERT(pfds[1].revents == 0);
  GPR_ASSERT(pfds[2].revents == 0);
  GPR_ASSERT(pfds[3].revents == 0);
  GPR_ASSERT(pfds[4].revents == 0);
  GPR_ASSERT(pfds[5].revents == 0);

  // Pollin on socket fd
  pargs.timeout = -1;
  pargs.result = -2;
  gpr_thd_new(&t_id, &background_poll, &pargs, &opt);
  trigger_socket_event();
  gpr_thd_join(t_id);
  GPR_ASSERT(pargs.result == 1);
  GPR_ASSERT(pfds[0].revents == 0);
  GPR_ASSERT(pfds[1].revents == 0);
  GPR_ASSERT(pfds[2].revents == POLLIN);
  GPR_ASSERT(pfds[3].revents == 0);
  GPR_ASSERT(pfds[4].revents == 0);
  GPR_ASSERT(pfds[5].revents == 0);

  // Pollin on wakeup fd
  reset_socket_event();
  pargs.result = -2;
  gpr_thd_new(&t_id, &background_poll, &pargs, &opt);
  GPR_ASSERT(grpc_wakeup_fd_wakeup(&cvfd2) == GRPC_ERROR_NONE);
  gpr_thd_join(t_id);

  GPR_ASSERT(pargs.result == 1);
  GPR_ASSERT(pfds[0].revents == 0);
  GPR_ASSERT(pfds[1].revents == POLLIN);
  GPR_ASSERT(pfds[2].revents == 0);
  GPR_ASSERT(pfds[3].revents == 0);
  GPR_ASSERT(pfds[4].revents == 0);
  GPR_ASSERT(pfds[5].revents == 0);

  // Pollin on wakeupfd before poll()
  pargs.result = -2;
  gpr_thd_new(&t_id, &background_poll, &pargs, &opt);
  gpr_thd_join(t_id);

  GPR_ASSERT(pargs.result == 1);
  GPR_ASSERT(pfds[0].revents == 0);
  GPR_ASSERT(pfds[1].revents == POLLIN);
  GPR_ASSERT(pfds[2].revents == 0);
  GPR_ASSERT(pfds[3].revents == 0);
  GPR_ASSERT(pfds[4].revents == 0);
  GPR_ASSERT(pfds[5].revents == 0);

  // No Events
  pargs.result = -2;
  pargs.timeout = 1000;
  reset_socket_event();
  GPR_ASSERT(grpc_wakeup_fd_consume_wakeup(&cvfd1) == GRPC_ERROR_NONE);
  GPR_ASSERT(grpc_wakeup_fd_consume_wakeup(&cvfd2) == GRPC_ERROR_NONE);
  gpr_thd_new(&t_id, &background_poll, &pargs, &opt);
  gpr_thd_join(t_id);

  GPR_ASSERT(pargs.result == 0);
  GPR_ASSERT(pfds[0].revents == 0);
  GPR_ASSERT(pfds[1].revents == 0);
  GPR_ASSERT(pfds[2].revents == 0);
  GPR_ASSERT(pfds[3].revents == 0);
  GPR_ASSERT(pfds[4].revents == 0);
  GPR_ASSERT(pfds[5].revents == 0);
}

int main(int argc, char **argv) {
  gpr_setenv("GRPC_POLL_STRATEGY", "poll-cv");
  grpc_poll_function = &mock_poll;
  gpr_mu_init(&poll_mu);
  gpr_cv_init(&poll_cv);

  grpc_iomgr_platform_init();
  test_many_fds();
  grpc_iomgr_platform_shutdown();

  grpc_iomgr_platform_init();
  test_poll_cv_trigger();
  grpc_iomgr_platform_shutdown();
  return 0;
}

#else /* GRPC_POSIX_SOCKET */

int main(int argc, char **argv) { return 1; }

#endif /* GRPC_POSIX_SOCKET */
