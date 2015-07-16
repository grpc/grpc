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

#ifdef GPR_POSIX_SOCKET

#include "src/core/iomgr/pollset_posix.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/iomgr/fd_posix.h"
#include "src/core/iomgr/iomgr_internal.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

typedef struct {
  /* all polled fds */
  size_t fd_count;
  size_t fd_capacity;
  grpc_fd **fds;
  /* fds being polled by the current poller: parallel arrays of pollfd, and
     a grpc_fd_watcher */
  size_t pfd_count;
  size_t pfd_capacity;
  grpc_fd_watcher *watchers;
  struct pollfd *pfds;
  /* fds that have been removed from the pollset explicitly */
  size_t del_count;
  size_t del_capacity;
  grpc_fd **dels;
} pollset_hdr;

static void multipoll_with_poll_pollset_add_fd(grpc_pollset *pollset,
                                               grpc_fd *fd,
                                               int and_unlock_pollset) {
  size_t i;
  pollset_hdr *h = pollset->data.ptr;
  /* TODO(ctiller): this is O(num_fds^2); maybe switch to a hash set here */
  for (i = 0; i < h->fd_count; i++) {
    if (h->fds[i] == fd) goto exit;
  }
  if (h->fd_count == h->fd_capacity) {
    h->fd_capacity = GPR_MAX(h->fd_capacity + 8, h->fd_count * 3 / 2);
    h->fds = gpr_realloc(h->fds, sizeof(grpc_fd *) * h->fd_capacity);
  }
  h->fds[h->fd_count++] = fd;
  GRPC_FD_REF(fd, "multipoller");
exit:  
  if (and_unlock_pollset) {
    gpr_mu_unlock(&pollset->mu);
  }
}

static void multipoll_with_poll_pollset_del_fd(grpc_pollset *pollset,
                                               grpc_fd *fd,
                                               int and_unlock_pollset) {
  /* will get removed next poll cycle */
  pollset_hdr *h = pollset->data.ptr;
  if (h->del_count == h->del_capacity) {
    h->del_capacity = GPR_MAX(h->del_capacity + 8, h->del_count * 3 / 2);
    h->dels = gpr_realloc(h->dels, sizeof(grpc_fd *) * h->del_capacity);
  }
  h->dels[h->del_count++] = fd;
  GRPC_FD_REF(fd, "multipoller_del");
  if (and_unlock_pollset) {
    gpr_mu_unlock(&pollset->mu);
  }
}

static void end_polling(grpc_pollset *pollset) {
  size_t i;
  pollset_hdr *h;
  h = pollset->data.ptr;
  for (i = 1; i < h->pfd_count; i++) {
    grpc_fd_end_poll(&h->watchers[i], h->pfds[i].revents & POLLIN,
                     h->pfds[i].revents & POLLOUT);
  }
}

static void multipoll_with_poll_pollset_maybe_work(
    grpc_pollset *pollset, gpr_timespec deadline, gpr_timespec now,
    int allow_synchronous_callback) {
  int timeout;
  int r;
  size_t i, np, nf, nd;
  pollset_hdr *h;
  grpc_kick_fd_info *kfd;

  h = pollset->data.ptr;
  timeout = grpc_poll_deadline_to_millis_timeout(deadline, now);
  if (h->pfd_capacity < h->fd_count + 1) {
    h->pfd_capacity = GPR_MAX(h->pfd_capacity * 3 / 2, h->fd_count + 1);
    gpr_free(h->pfds);
    gpr_free(h->watchers);
    h->pfds = gpr_malloc(sizeof(struct pollfd) * h->pfd_capacity);
    h->watchers = gpr_malloc(sizeof(grpc_fd_watcher) * h->pfd_capacity);
  }
  nf = 0;
  np = 1;
  kfd = grpc_pollset_kick_pre_poll(&pollset->kick_state);
  if (kfd == NULL) {
    /* Already kicked */
    return;
  }
  h->pfds[0].fd = GRPC_POLLSET_KICK_GET_FD(kfd);
  h->pfds[0].events = POLLIN;
  h->pfds[0].revents = POLLOUT;
  for (i = 0; i < h->fd_count; i++) {
    int remove = grpc_fd_is_orphaned(h->fds[i]);
    for (nd = 0; nd < h->del_count; nd++) {
      if (h->fds[i] == h->dels[nd]) remove = 1;
    }
    if (remove) {
      GRPC_FD_UNREF(h->fds[i], "multipoller");
    } else {
      h->fds[nf++] = h->fds[i];
      h->watchers[np].fd = h->fds[i];
      h->pfds[np].fd = h->fds[i]->fd;
      h->pfds[np].revents = 0;
      np++;
    }
  }
  h->pfd_count = np;
  h->fd_count = nf;
  for (nd = 0; nd < h->del_count; nd++) {
    GRPC_FD_UNREF(h->dels[nd], "multipoller_del");
  }
  h->del_count = 0;
  if (h->pfd_count == 0) {
    end_polling(pollset);
    return;
  }
  pollset->counter++;
  gpr_mu_unlock(&pollset->mu);

  for (i = 1; i < np; i++) {
    h->pfds[i].events = grpc_fd_begin_poll(h->watchers[i].fd, pollset, POLLIN,
                                           POLLOUT, &h->watchers[i]);
  }

  r = poll(h->pfds, h->pfd_count, timeout);

  end_polling(pollset);

  if (r < 0) {
    if (errno != EINTR) {
      gpr_log(GPR_ERROR, "poll() failed: %s", strerror(errno));
    }
  } else if (r == 0) {
    /* do nothing */
  } else {
    if (h->pfds[0].revents & POLLIN) {
      grpc_pollset_kick_consume(&pollset->kick_state, kfd);
    }
    for (i = 1; i < np; i++) {
      if (h->watchers[i].fd == NULL) {
        continue;
      }
      if (h->pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
        grpc_fd_become_readable(h->watchers[i].fd, allow_synchronous_callback);
      }
      if (h->pfds[i].revents & (POLLOUT | POLLHUP | POLLERR)) {
        grpc_fd_become_writable(h->watchers[i].fd, allow_synchronous_callback);
      }
    }
  }
  grpc_pollset_kick_post_poll(&pollset->kick_state, kfd);

  gpr_mu_lock(&pollset->mu);
  pollset->counter--;
}

static void multipoll_with_poll_pollset_kick(grpc_pollset *p) {
  grpc_pollset_force_kick(p);
}

static void multipoll_with_poll_pollset_finish_shutdown(grpc_pollset *pollset) {
  size_t i;
  pollset_hdr *h = pollset->data.ptr;
  GPR_ASSERT(pollset->counter == 0);
  for (i = 0; i < h->fd_count; i++) {
    GRPC_FD_UNREF(h->fds[i], "multipoller");
  }
  for (i = 0; i < h->del_count; i++) {
    GRPC_FD_UNREF(h->dels[i], "multipoller_del");
  }
  h->fd_count = 0;
  h->del_count = 0;
}

static void multipoll_with_poll_pollset_destroy(grpc_pollset *pollset) {
  pollset_hdr *h = pollset->data.ptr;
  multipoll_with_poll_pollset_finish_shutdown(pollset);
  gpr_free(h->pfds);
  gpr_free(h->watchers);
  gpr_free(h->fds);
  gpr_free(h->dels);
  gpr_free(h);
}

static const grpc_pollset_vtable multipoll_with_poll_pollset = {
    multipoll_with_poll_pollset_add_fd,
    multipoll_with_poll_pollset_del_fd,
    multipoll_with_poll_pollset_maybe_work,
    multipoll_with_poll_pollset_kick,
    multipoll_with_poll_pollset_finish_shutdown,
    multipoll_with_poll_pollset_destroy};

void grpc_poll_become_multipoller(grpc_pollset *pollset, grpc_fd **fds,
                                  size_t nfds) {
  size_t i;
  pollset_hdr *h = gpr_malloc(sizeof(pollset_hdr));
  pollset->vtable = &multipoll_with_poll_pollset;
  pollset->data.ptr = h;
  h->fd_count = nfds;
  h->fd_capacity = nfds;
  h->fds = gpr_malloc(nfds * sizeof(grpc_fd *));
  h->pfd_count = 0;
  h->pfd_capacity = 0;
  h->pfds = NULL;
  h->watchers = NULL;
  h->del_count = 0;
  h->del_capacity = 0;
  h->dels = NULL;
  for (i = 0; i < nfds; i++) {
    h->fds[i] = fds[i];
    GRPC_FD_REF(fds[i], "multipoller");
  }
}

#endif /* GPR_POSIX_SOCKET */

#ifdef GPR_POSIX_MULTIPOLL_WITH_POLL
grpc_platform_become_multipoller_type grpc_platform_become_multipoller =
    grpc_poll_become_multipoller;
#endif
