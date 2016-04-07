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

/*
 * This is a demonstration program of various scenarios involving multiple
 * threads calling epoll_wait, and how many threads wake up in those scenarios.
 *
 * The basic setup is that we create an epoll set and add an eventfd to that
 * set. We then spawn some threads that poll the two in various ways.
 *
 * As of Linux 4.5 in almost all cases all threads wake up. In many of these
 * cases that is expected.
 *
 * The most interesting one for the purposes of grpc that could work but is
 * currently invalid (as of 2016-04) is EPOLLEXCLUSIVE on an epoll fd.
 */

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <grpc/support/log.h>
#include <grpc/support/thd.h>

#ifndef EPOLLEXCLUSIVE
/* 
 * The EPOLLEXCLUSIVE flag is very new -- introduced in 4.5, released
 * 2016-03-14, so as of 2016-04 we can't expect anybody has it.
 * TODO(dklempner): Remove this in a few years.
 */
#define EPOLLEXCLUSIVE (1 << 28)
#endif

struct thread_args {
  /* Input epoll fd */
  int epfd;
  /* Input fd that will become readable */
  int evfd;
  /* epoll_ctl args, where applicable */
  uint32_t epoll_ctl_args;
  /* Output whether this thread was awakened */
  int awakened;
  /* Output whether this thread completed validly */
  int error;
};

static void epoll_on_epfd(struct thread_args* args) {
  int rv;
  struct epoll_event epev[1];
  int saved_errno;

  args->error = 1;

  rv = epoll_wait(args->epfd, epev, 1, 2000);

  if (rv < 0) {
    saved_errno = errno;
    fprintf(stderr, "epoll_wait failed %s\n", strerror(saved_errno));
    return;
  }

  if (rv == 0) {
    gpr_log(GPR_INFO, "Thread timed out");
    args->awakened = 0;
  } else {
    gpr_log(GPR_INFO, "Thread was awakened");
    args->awakened = 1;
  }
  args->error = 0;
}

static void epoll_on_epfd_wrap(void* arg) {
  epoll_on_epfd(arg);
}

static void poll_epoll(struct thread_args* args) {
  int rv;
  struct pollfd pollfds[1];
  int saved_errno;

  args->error = 1;

  pollfds[0].fd = args->epfd;
  pollfds[0].events = POLLIN;

  rv = poll(pollfds, 1, 2000);

  if (rv < 0) {
    saved_errno = errno;
    fprintf(stderr, "poll failed %s\n", strerror(saved_errno));
    return;
  }

  if (rv == 0) {
    gpr_log(GPR_INFO, "Thread timed out");
    args->awakened = 0;
  } else {
    gpr_log(GPR_INFO, "Thread was awakened");
    args->awakened = 1;
  }
  args->error = 0;
}

static void poll_epoll_wrap(void* arg) {
  poll_epoll(arg);
}

static void epoll_epoll(struct thread_args* args) {
  int rv;
  struct epoll_event epev[1];
  int saved_errno;
  int super_epfd;

  args->error = 1;

  super_epfd = epoll_create(1000);
  if (super_epfd < 0) {
    saved_errno = errno;
    fprintf(stderr, "epoll_create failed %s\n", strerror(saved_errno));
    return;
  }

  epev[0].events = args->epoll_ctl_args;
  epev[0].data.fd = args->epfd;
  rv = epoll_ctl(super_epfd, EPOLL_CTL_ADD, args->epfd, epev);

  if (rv < 0) {
    saved_errno = errno;
    fprintf(stderr, "epoll_ctl failed %s\n", strerror(saved_errno));
    close(super_epfd);
    return;
  }

  rv = epoll_wait(super_epfd, epev, 1, 2000);

  if (rv < 0) {
    saved_errno = errno;
    fprintf(stderr, "epoll_wait failed %s\n", strerror(saved_errno));
    return;
  }

  if (rv == 0) {
    gpr_log(GPR_INFO, "Thread timed out");
    args->awakened = 0;
  } else {
    gpr_log(GPR_INFO, "Thread was awakened");
    args->awakened = 1;
  }
  args->error = 0;
  close(super_epfd);
}

static void epoll_epoll_wrap(void* arg) {
  epoll_epoll(arg);
}

static void epoll_on_fd(struct thread_args* args) {
  int rv;
  struct epoll_event epev[1];
  int saved_errno;
  int super_epfd;

  args->error = 1;

  super_epfd = epoll_create(1000);
  if (super_epfd < 0) {
    saved_errno = errno;
    fprintf(stderr, "epoll_create failed %s\n", strerror(saved_errno));
    return;
  }

  epev[0].events = args->epoll_ctl_args;
  epev[0].data.fd = args->evfd;
  rv = epoll_ctl(super_epfd, EPOLL_CTL_ADD, args->evfd, epev);

  if (rv < 0) {
    saved_errno = errno;
    fprintf(stderr, "epoll_ctl failed %s\n", strerror(saved_errno));
    close(super_epfd);
    return;
  }

  rv = epoll_wait(super_epfd, epev, 1, 2000);

  if (rv < 0) {
    saved_errno = errno;
    fprintf(stderr, "epoll_wait failed %s\n", strerror(saved_errno));
    return;
  }

  if (rv == 0) {
    gpr_log(GPR_INFO, "Thread timed out");
    args->awakened = 0;
  } else {
    gpr_log(GPR_INFO, "Thread was awakened");
    args->awakened = 1;
  }
  args->error = 0;
  close(super_epfd);

}

static void epoll_on_fd_wrap(void* arg) {
  epoll_on_fd(arg);
}

struct test_results {
  int threads_awakened;
  int valid;
};

struct multi_epoll_test_poller_args {
  enum poll_style {
    DIRECT_EPOLL,
    POLL_EPOLL,
    EPOLL_EPOLL,
    DIRECT,
  } poll_style;
  /* encoded args for epoll_ctl, when applicable */
  uint32_t epoll_ctl_args;
};

struct multi_epoll_test_args {
  uint32_t epoll_ctl_args;
  struct multi_epoll_test_poller_args poller_args;
  gpr_thd_id main_tid;
  struct test_results results;
};

struct multi_epoll_test_args default_test_args() {
  struct multi_epoll_test_args args;
  args.epoll_ctl_args = EPOLLIN;
  args.poller_args.poll_style = DIRECT_EPOLL;
  args.poller_args.epoll_ctl_args = EPOLLIN;
  return args;
}

/* Describe the events field from epoll */
static void concat_epoll_ctl_events(uint32_t events, char* buf) {
  strcat(buf, "(");
  if (events & EPOLLIN) {
    strcat(buf, " IN");
  }
  if (events & EPOLLET) {
    strcat(buf, " ET");
  }
  if (events & EPOLLONESHOT) {
    strcat(buf, " ONESHOT");
  }
  if (events & EPOLLEXCLUSIVE) {
    strcat(buf, " EXCLUSIVE");
  }
  strcat(buf, ")");
}

static void concat_test_description(struct multi_epoll_test_args* args,
    char* buf) {
  switch (args->poller_args.poll_style) {
    case DIRECT_EPOLL:
      strcat(buf, " polling via epoll_wait() on epollfd");
      break;
    case POLL_EPOLL:
      strcat(buf, " polling via poll() of epollfd");
      break;
    case EPOLL_EPOLL:
      strcat(buf, " polling via epoll_wait() of epollset containing epollfd");
      concat_epoll_ctl_events(args->poller_args.epoll_ctl_args, buf);
      break;
    case DIRECT:
      strcat(buf, " epoll_wait() on separate pollset containing shared eventfd");
      concat_epoll_ctl_events(args->poller_args.epoll_ctl_args, buf);
      break;
    default:
      strcat(buf, " unknown polling mechanism");
      break;
  }
  concat_epoll_ctl_events(args->epoll_ctl_args, buf);
}

static void log_test_args(struct multi_epoll_test_args* args) {
  char buf[1024];
  buf[0] = '\0';
  strcat(buf, "Starting multi_epoll_test");
  concat_test_description(args, buf);
  gpr_log(GPR_INFO, buf);
}

static void multi_epoll_test(struct multi_epoll_test_args* args) {
  int epfd;
  int saved_errno;
  int evfd;
  int rv;
  struct epoll_event epev;
  const int kPollers = 2;
  gpr_thd_id tid[kPollers];
  struct thread_args poller_args[kPollers];

  args->results.valid = 0;

  log_test_args(args);

  epfd = epoll_create(1000);
  if (epfd < 0) {
    saved_errno = errno;
    fprintf(stderr, "epoll_create failed %s\n", strerror(saved_errno));
    return;
  }

  evfd = eventfd(0, 0);
  if (evfd < 0) {
    saved_errno = errno;
    fprintf(stderr, "eventfd failed %s\n", strerror(saved_errno));
    close(epfd);
    return;
  }

  epev.events = args->epoll_ctl_args;
  epev.data.fd = evfd;
  rv = epoll_ctl(epfd, EPOLL_CTL_ADD, evfd, &epev);

  if (rv < 0) {
    saved_errno = errno;
    fprintf(stderr, "epoll_ctl failed %s\n", strerror(saved_errno));
    close(evfd);
    close(epfd);
    return;
  }

  gpr_thd_options thd_options = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&thd_options);

  for (int i = 0; i < kPollers; ++i) {
    poller_args[i].epfd = epfd;
    poller_args[i].evfd = evfd;
    poller_args[i].awakened = 0;
    poller_args[i].epoll_ctl_args = args->poller_args.epoll_ctl_args;
    switch (args->poller_args.poll_style) {
      case DIRECT_EPOLL:
        gpr_thd_new(&tid[i], epoll_on_epfd_wrap, &poller_args[i],
            &thd_options);
        break;
      case POLL_EPOLL:
        gpr_thd_new(&tid[i], poll_epoll_wrap, &poller_args[i],
            &thd_options);
        break;
      case EPOLL_EPOLL:
        gpr_thd_new(&tid[i], epoll_epoll_wrap, &poller_args[i],
            &thd_options);
        break;
      case DIRECT:
        gpr_thd_new(&tid[i], epoll_on_fd_wrap, &poller_args[i],
            &thd_options);
        break;
      default:
        fprintf(stderr, "Unrecognized poller style %d\n",
            args->poller_args.poll_style);
        break;
    }
  }

  /* Ensure threads are polling */
  sleep(1);
  eventfd_write(evfd, 1);
  args->results.valid = 1;
  for (int i = 0; i < kPollers; ++i) {
    gpr_thd_join(tid[i]);
    args->results.threads_awakened += poller_args[i].awakened;
    args->results.valid = args->results.valid && !poller_args[i].error;
  }

  close(evfd);
  close(epfd);
}

static void multi_epoll_test_wrap(void* args) {
  multi_epoll_test(args);
}

static void start_multi_epoll_test(struct multi_epoll_test_args* args) {
  gpr_thd_options thd_options = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&thd_options);

  gpr_thd_new(&args->main_tid, multi_epoll_test_wrap, args, &thd_options);
}

static void join_multi_epoll_test(struct multi_epoll_test_args* args) {
  gpr_thd_join(args->main_tid);
}

void log_results(struct multi_epoll_test_args args) {
  char buf[1024];
  buf[0] = '\0';
  strcat(buf, "multi_epoll_test");
  concat_test_description(&args, buf);
  gpr_log(GPR_INFO, "%s Valid %d Threads awakened: %d", buf,
      args.results.valid, args.results.threads_awakened);
}

int fill_test_args(struct multi_epoll_test_args* args) {
  uint32_t epoll_ctl_variants[] = {
      EPOLLIN,
      EPOLLIN | EPOLLET,
      EPOLLIN | EPOLLONESHOT,
      EPOLLIN | EPOLLEXCLUSIVE };
  const int kEpollCtlVariants = sizeof(epoll_ctl_variants) / sizeof(uint32_t);
  int test_count = 0;

  /* call epoll_wait on a shared epoll set, with an eventfd added various
   * ways */
  for (int i = 0; i < kEpollCtlVariants; ++i) {
    args[i] = default_test_args();
    args[test_count].poller_args.poll_style = DIRECT_EPOLL;
    args[test_count].epoll_ctl_args = epoll_ctl_variants[test_count];
    ++test_count;
  }
  /* As above, but poll on the shared epoll */
  for (int i = 0; i < kEpollCtlVariants; ++i) {
    args[test_count] = args[test_count - kEpollCtlVariants];
    args[test_count].poller_args.poll_style = POLL_EPOLL;
    ++test_count;
  }
  /* As above, but epoll_wait (on separate epoll sets) */
  for (int i = 0; i < kEpollCtlVariants; ++i) {
    for (int j = 0; j < kEpollCtlVariants; ++j) {
      args[test_count] = args[test_count - kEpollCtlVariants];
      args[test_count].poller_args.poll_style = EPOLL_EPOLL;
      args[test_count].poller_args.epoll_ctl_args = epoll_ctl_variants[j];
      ++test_count;
    }
  }
  /* epoll_wait on separate epoll sets directly containing the eventfd */
  for (int i = 0; i < kEpollCtlVariants; ++i) {
    args[test_count] = default_test_args();
    args[test_count].poller_args.poll_style = DIRECT;
    args[test_count].poller_args.epoll_ctl_args = epoll_ctl_variants[i];
    ++test_count;
  }
  return test_count;
}

int main(int argc, char **argv) {
  /* TODO(dklempner): Count the number of test configurations. */
  const int kMaxTests = 100;
  struct multi_epoll_test_args args[kMaxTests];

  int test_count = fill_test_args(args);

  for (int i = 0; i < test_count; ++i) {
    start_multi_epoll_test(&args[i]);
  }
  for (int i = 0; i < test_count; ++i) {
    join_multi_epoll_test(&args[i]);
  }
  for (int i = 0; i < test_count; ++i) {
    log_results(args[i]);
  }

  return 0;
}
