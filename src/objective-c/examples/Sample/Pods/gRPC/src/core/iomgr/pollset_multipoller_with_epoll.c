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
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "src/core/iomgr/fd_posix.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

typedef struct {
  int epoll_fd;
  grpc_wakeup_fd_info wakeup_fd;
} pollset_hdr;

static void multipoll_with_epoll_pollset_add_fd(grpc_pollset *pollset,
                                                grpc_fd *fd) {
  pollset_hdr *h = pollset->data.ptr;
  struct epoll_event ev;
  int err;

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

static void multipoll_with_epoll_pollset_del_fd(grpc_pollset *pollset,
                                                grpc_fd *fd) {
  pollset_hdr *h = pollset->data.ptr;
  int err;
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

static int multipoll_with_epoll_pollset_maybe_work(
    grpc_pollset *pollset, gpr_timespec deadline, gpr_timespec now,
    int allow_synchronous_callback) {
  struct epoll_event ep_ev[GRPC_EPOLL_MAX_EVENTS];
  int ep_rv;
  pollset_hdr *h = pollset->data.ptr;
  int timeout_ms;

  /* If you want to ignore epoll's ability to sanely handle parallel pollers,
   * for a more apples-to-apples performance comparison with poll, add a
   * if (pollset->counter != 0) { return 0; }
   * here.
   */

  if (gpr_time_cmp(deadline, gpr_inf_future) == 0) {
    timeout_ms = -1;
  } else {
    timeout_ms = gpr_time_to_millis(gpr_time_sub(deadline, now));
    if (timeout_ms <= 0) {
      return 1;
    }
  }
  pollset->counter += 1;
  gpr_mu_unlock(&pollset->mu);

  do {
    ep_rv = epoll_wait(h->epoll_fd, ep_ev, GRPC_EPOLL_MAX_EVENTS, timeout_ms);
    if (ep_rv < 0) {
      if (errno != EINTR) {
        gpr_log(GPR_ERROR, "epoll_wait() failed: %s", strerror(errno));
      }
    } else {
      int i;
      for (i = 0; i < ep_rv; ++i) {
        if (ep_ev[i].data.ptr == 0) {
          grpc_wakeup_fd_consume_wakeup(&h->wakeup_fd);
        } else {
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
    }
    timeout_ms = 0;
  } while (ep_rv == GRPC_EPOLL_MAX_EVENTS);

  gpr_mu_lock(&pollset->mu);
  pollset->counter -= 1;
  /* TODO(klempner): This should signal once per event rather than broadcast,
   * although it probably doesn't matter because threads will generally be
   * blocked in epoll_wait rather than being blocked on the cv. */
  gpr_cv_broadcast(&pollset->cv);
  return 1;
}

static void multipoll_with_epoll_pollset_destroy(grpc_pollset *pollset) {
  pollset_hdr *h = pollset->data.ptr;
  grpc_wakeup_fd_destroy(&h->wakeup_fd);
  close(h->epoll_fd);
  gpr_free(h);
}

static void epoll_kick(grpc_pollset *pollset) {
  pollset_hdr *h = pollset->data.ptr;
  grpc_wakeup_fd_wakeup(&h->wakeup_fd);
}

static const grpc_pollset_vtable multipoll_with_epoll_pollset = {
    multipoll_with_epoll_pollset_add_fd, multipoll_with_epoll_pollset_del_fd,
    multipoll_with_epoll_pollset_maybe_work, epoll_kick,
    multipoll_with_epoll_pollset_destroy};

void grpc_platform_become_multipoller(grpc_pollset *pollset, grpc_fd **fds,
                                      size_t nfds) {
  size_t i;
  pollset_hdr *h = gpr_malloc(sizeof(pollset_hdr));
  struct epoll_event ev;
  int err;

  pollset->vtable = &multipoll_with_epoll_pollset;
  pollset->data.ptr = h;
  h->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (h->epoll_fd < 0) {
    /* TODO(klempner): Fall back to poll here, especially on ENOSYS */
    gpr_log(GPR_ERROR, "epoll_create1 failed: %s", strerror(errno));
    abort();
  }
  for (i = 0; i < nfds; i++) {
    multipoll_with_epoll_pollset_add_fd(pollset, fds[i]);
  }

  grpc_wakeup_fd_create(&h->wakeup_fd);
  ev.events = EPOLLIN;
  ev.data.ptr = 0;
  err = epoll_ctl(h->epoll_fd, EPOLL_CTL_ADD,
                  GRPC_WAKEUP_FD_GET_READ_FD(&h->wakeup_fd), &ev);
  if (err < 0) {
    gpr_log(GPR_ERROR, "Wakeup fd epoll_ctl failed: %s", strerror(errno));
    abort();
  }
}

#endif  /* GPR_LINUX_MULTIPOLL_WITH_EPOLL */
