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

#include <grpc/support/port_platform.h>

#ifdef GPR_LINUX_MULTIPOLL_WITH_EPOLL

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "src/core/iomgr/fd_posix.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

typedef struct wakeup_fd_hdl {
  grpc_wakeup_fd wakeup_fd;
  struct wakeup_fd_hdl *next;
} wakeup_fd_hdl;

typedef struct {
  int epoll_fd;
  wakeup_fd_hdl *free_wakeup_fds;
} pollset_hdr;

static void multipoll_with_epoll_pollset_add_fd(grpc_pollset *pollset,
                                                grpc_fd *fd,
                                                int and_unlock_pollset) {
  pollset_hdr *h = pollset->data.ptr;
  struct epoll_event ev;
  int err;
  grpc_fd_watcher watcher;

  if (and_unlock_pollset) {
    gpr_mu_unlock(&pollset->mu);
  }

  /* We pretend to be polling whilst adding an fd to keep the fd from being
     closed during the add. This may result in a spurious wakeup being assigned
     to this pollset whilst adding, but that should be benign. */
  GPR_ASSERT(grpc_fd_begin_poll(fd, pollset, 0, 0, &watcher) == 0);
  if (watcher.fd != NULL) {
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = fd;
    err = epoll_ctl(h->epoll_fd, EPOLL_CTL_ADD, fd->fd, &ev);
    if (err < 0) {
      /* FDs may be added to a pollset multiple times, so EEXIST is normal. */
      if (errno != EEXIST) {
        gpr_log(GPR_ERROR, "epoll_ctl add for %d failed: %s", fd->fd,
                strerror(errno));
      }
    }
  }
  grpc_fd_end_poll(&watcher, 0, 0);
}

static void multipoll_with_epoll_pollset_del_fd(grpc_pollset *pollset,
                                                grpc_fd *fd,
                                                int and_unlock_pollset) {
  pollset_hdr *h = pollset->data.ptr;
  int err;

  if (and_unlock_pollset) {
    gpr_mu_unlock(&pollset->mu);
  }

  /* Note that this can race with concurrent poll, but that should be fine since
   * at worst it creates a spurious read event on a reused grpc_fd object. */
  err = epoll_ctl(h->epoll_fd, EPOLL_CTL_DEL, fd->fd, NULL);
  if (err < 0) {
    gpr_log(GPR_ERROR, "epoll_ctl del for %d failed: %s", fd->fd,
            strerror(errno));
  }
}

/* TODO(klempner): We probably want to turn this down a bit */
#define GRPC_EPOLL_MAX_EVENTS 1000

static void multipoll_with_epoll_pollset_maybe_work(
    grpc_pollset *pollset, grpc_pollset_worker *worker, gpr_timespec deadline,
    gpr_timespec now, int allow_synchronous_callback) {
  struct epoll_event ep_ev[GRPC_EPOLL_MAX_EVENTS];
  int ep_rv;
  int poll_rv;
  pollset_hdr *h = pollset->data.ptr;
  int timeout_ms;
  struct pollfd pfds[2];

  /* If you want to ignore epoll's ability to sanely handle parallel pollers,
   * for a more apples-to-apples performance comparison with poll, add a
   * if (pollset->counter != 0) { return 0; }
   * here.
   */

  gpr_mu_unlock(&pollset->mu);

  timeout_ms = grpc_poll_deadline_to_millis_timeout(deadline, now);

  pfds[0].fd = GRPC_WAKEUP_FD_GET_READ_FD(&worker->wakeup_fd);
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;
  pfds[1].fd = h->epoll_fd;
  pfds[1].events = POLLIN;
  pfds[1].revents = 0;

  poll_rv = poll(pfds, 2, timeout_ms);

  if (poll_rv < 0) {
    if (errno != EINTR) {
      gpr_log(GPR_ERROR, "poll() failed: %s", strerror(errno));
    }
  } else if (poll_rv == 0) {
    /* do nothing */
  } else {
    if (pfds[0].revents) {
      grpc_wakeup_fd_consume_wakeup(&worker->wakeup_fd);
    }
    if (pfds[1].revents) {
      do {
        ep_rv = epoll_wait(h->epoll_fd, ep_ev, GRPC_EPOLL_MAX_EVENTS, 0);
        if (ep_rv < 0) {
          if (errno != EINTR) {
            gpr_log(GPR_ERROR, "epoll_wait() failed: %s", strerror(errno));
          }
        } else {
          int i;
          for (i = 0; i < ep_rv; ++i) {
            grpc_fd *fd = ep_ev[i].data.ptr;
            /* TODO(klempner): We might want to consider making err and pri
             * separate events */
            int cancel = ep_ev[i].events & (EPOLLERR | EPOLLHUP);
            int read = ep_ev[i].events & (EPOLLIN | EPOLLPRI);
            int write = ep_ev[i].events & EPOLLOUT;
            if (read || cancel) {
              grpc_fd_become_readable(fd, allow_synchronous_callback);
            }
            if (write || cancel) {
              grpc_fd_become_writable(fd, allow_synchronous_callback);
            }
          }
        }
      } while (ep_rv == GRPC_EPOLL_MAX_EVENTS);
    }
  }

  gpr_mu_lock(&pollset->mu);
}

static void multipoll_with_epoll_pollset_finish_shutdown(
    grpc_pollset *pollset) {}

static void multipoll_with_epoll_pollset_destroy(grpc_pollset *pollset) {
  pollset_hdr *h = pollset->data.ptr;
  close(h->epoll_fd);
  gpr_free(h);
}

static const grpc_pollset_vtable multipoll_with_epoll_pollset = {
    multipoll_with_epoll_pollset_add_fd,
    multipoll_with_epoll_pollset_del_fd,
    multipoll_with_epoll_pollset_maybe_work,
    multipoll_with_epoll_pollset_finish_shutdown,
    multipoll_with_epoll_pollset_destroy};

static void epoll_become_multipoller(grpc_pollset *pollset, grpc_fd **fds,
                                     size_t nfds) {
  size_t i;
  pollset_hdr *h = gpr_malloc(sizeof(pollset_hdr));

  pollset->vtable = &multipoll_with_epoll_pollset;
  pollset->data.ptr = h;
  h->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (h->epoll_fd < 0) {
    /* TODO(klempner): Fall back to poll here, especially on ENOSYS */
    gpr_log(GPR_ERROR, "epoll_create1 failed: %s", strerror(errno));
    abort();
  }
  for (i = 0; i < nfds; i++) {
    multipoll_with_epoll_pollset_add_fd(pollset, fds[i], 0);
  }
}

grpc_platform_become_multipoller_type grpc_platform_become_multipoller =
    epoll_become_multipoller;

#endif /* GPR_LINUX_MULTIPOLL_WITH_EPOLL */
