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

/* TODO: sreek: REMOVE THIS FILE */

#include <grpc/support/port_platform.h>

#include <errno.h>
#include <netinet/ip.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>

int g_signal_num = SIGUSR1;

int g_timeout_secs = 2;

int g_eventfd_create = 1;
int g_eventfd_wakeup = 0;
int g_eventfd_teardown = 0;
int g_close_epoll_fd = 1;

typedef struct thread_args {
  gpr_thd_id id;
  int epoll_fd;
  int thread_num;
} thread_args;

static int eventfd_create() {
  if (!g_eventfd_create) {
    return -1;
  }

  int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  GPR_ASSERT(efd >= 0);
  return efd;
}

static void eventfd_wakeup(int efd) {
  if (!g_eventfd_wakeup) {
    return;
  }

  int err;
  do {
    err = eventfd_write(efd, 1);
  } while (err < 0 && errno == EINTR);
}

static void epoll_teardown(int epoll_fd, int fd) {
  if (!g_eventfd_teardown) {
    return;
  }

  if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
    if (errno != ENOENT) {
      gpr_log(GPR_ERROR, "epoll_ctl: %s", strerror(errno));
      GPR_ASSERT(0);
    }
  }
}

/* Special case for epoll, where we need to create the fd ahead of time. */
static int epoll_setup(int fd) {
  int epoll_fd;
  struct epoll_event ev;

  epoll_fd = epoll_create(1);
  if (epoll_fd < 0) {
    gpr_log(GPR_ERROR, "epoll_create: %s", strerror(errno));
    return -1;
  }

  ev.events = (uint32_t)EPOLLIN;
  ev.data.fd = fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
    if (errno != EEXIST) {
      gpr_log(GPR_ERROR, "epoll_ctl: %s", strerror(errno));
      return -1;
    }

    gpr_log(GPR_ERROR, "epoll_ctl: The fd %d already exists", fd);
  }

  return epoll_fd;
}

#define GRPC_EPOLL_MAX_EVENTS 1000
static void thread_main(void *args) {
  int ep_rv;
  struct epoll_event ep_ev[GRPC_EPOLL_MAX_EVENTS];
  int fd;
  int i;
  int cancel;
  int read;
  int write;
  thread_args *thd_args = args;
  sigset_t new_mask;
  sigset_t orig_mask;
  int keep_polling = 0;

  gpr_log(GPR_INFO, "Thread: %d Started", thd_args->thread_num);

  do {
    keep_polling = 0;

    /* Mask the signal before getting the epoll_fd */
    gpr_log(GPR_INFO, "Thread: %d Blocking signal: %d", thd_args->thread_num,
            g_signal_num);
    sigemptyset(&new_mask);
    sigaddset(&new_mask, g_signal_num);
    pthread_sigmask(SIG_BLOCK, &new_mask, &orig_mask);

    gpr_log(GPR_INFO, "Thread: %d Waiting on epoll_wait()",
            thd_args->thread_num);
    ep_rv = epoll_pwait(thd_args->epoll_fd, ep_ev, GRPC_EPOLL_MAX_EVENTS,
                        g_timeout_secs * 5000, &orig_mask);
    gpr_log(GPR_INFO, "Thread: %d out of epoll_wait. ep_rv = %d",
            thd_args->thread_num, ep_rv);

    if (ep_rv < 0) {
      if (errno != EINTR) {
        gpr_log(GPR_ERROR, "Thread: %d. epoll_wait failed with error: %d",
                thd_args->thread_num, errno);
      } else {
        gpr_log(GPR_INFO,
                "Thread: %d. epoll_wait was interrupted. Polling again >>>>>>>",
                thd_args->thread_num);
        keep_polling = 1;
      }
    } else {
      if (ep_rv == 0) {
        gpr_log(GPR_INFO,
                "Thread: %d - epoll_wait returned 0. Most likely a timeout. "
                "Polling again",
                thd_args->thread_num);
        keep_polling = 1;
      }

      for (i = 0; i < ep_rv; i++) {
        fd = ep_ev[i].data.fd;
        cancel = ep_ev[i].events & (EPOLLERR | EPOLLHUP);
        read = ep_ev[i].events & (EPOLLIN | EPOLLPRI);
        write = ep_ev[i].events & EPOLLOUT;
        gpr_log(GPR_INFO,
                "Thread: %d. epoll_wait returned that fd: %d has event of "
                "interest. read: %d, write: %d, cancel: %d",
                thd_args->thread_num, fd, read, write, cancel);
      }
    }
  } while (keep_polling);
}

static void close_fd(int fd) {
  if (!g_close_epoll_fd) {
    return;
  }

  gpr_log(GPR_INFO, "*** Closing fd : %d ****", fd);
  close(fd);
  gpr_log(GPR_INFO, "*** Closed fd : %d ****", fd);
}


static void sig_handler(int sig_num) {
  gpr_log(GPR_INFO, "<<<<< Received signal %d", sig_num);
}

static void set_signal_handler() {
  gpr_log(GPR_INFO, "Setting signal handler");
  signal(g_signal_num, sig_handler);
}

#define NUM_THREADS 2
int main(int argc, char **argv) {
  int efd;
  int epoll_fd;
  int i;
  thread_args thd_args[NUM_THREADS];
  gpr_thd_options options = gpr_thd_options_default();

  set_signal_handler();

  gpr_log(GPR_INFO, "Starting..");
  efd = eventfd_create();
  gpr_log(GPR_INFO, "Created event fd: %d", efd);
  epoll_fd = epoll_setup(efd);
  gpr_log(GPR_INFO, "Created epoll_fd: %d", epoll_fd);

  gpr_thd_options_set_joinable(&options);
  for (i = 0; i < NUM_THREADS; i++) {
    thd_args[i].thread_num = i;
    thd_args[i].epoll_fd = epoll_fd;
    gpr_log(GPR_INFO, "Starting thread: %d", i);
    gpr_thd_new(&thd_args[i].id, thread_main, &thd_args[i], &options);
  }

  sleep((unsigned)g_timeout_secs * 2);

  /* Send signals first */
  for (i = 0; i < NUM_THREADS; i++) {
    gpr_log(GPR_INFO, "Sending signal to thread: %d", thd_args->thread_num);
    pthread_kill(thd_args[i].id, g_signal_num);
    gpr_log(GPR_INFO, "Sent signal to thread: %d >>>>>> ",
            thd_args->thread_num);
  }

  sleep((unsigned)g_timeout_secs * 2);

  close_fd(epoll_fd);

  sleep((unsigned)g_timeout_secs * 2);

  eventfd_wakeup(efd);
  epoll_teardown(epoll_fd, efd);

  for (i = 0; i < NUM_THREADS; i++) {
    gpr_thd_join(thd_args[i].id);
    gpr_log(GPR_INFO, "Thread: %d joined", i);
  }

  return 0;
}
