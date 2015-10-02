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
#include "src/core/support/block_annotate.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

typedef struct {
  /* all polled fds */
  size_t fd_count;
  size_t fd_capacity;
  grpc_fd **fds;
  /* fds that have been removed from the pollset explicitly */
  size_t del_count;
  size_t del_capacity;
  grpc_fd **dels;
} pollset_hdr;

static void multipoll_with_poll_pollset_add_fd(grpc_exec_ctx *exec_ctx,
                                               grpc_pollset *pollset,
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

static void multipoll_with_poll_pollset_del_fd(grpc_exec_ctx *exec_ctx,
                                               grpc_pollset *pollset,
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

static void multipoll_with_poll_pollset_maybe_work_and_unlock(
    grpc_exec_ctx *exec_ctx, grpc_pollset *pollset, grpc_pollset_worker *worker,
    gpr_timespec deadline, gpr_timespec now) {
#define POLLOUT_CHECK (POLLOUT | POLLHUP | POLLERR)
#define POLLIN_CHECK (POLLIN | POLLHUP | POLLERR)

  int timeout;
  int r;
  size_t i, j, fd_count;
  nfds_t pfd_count;
  pollset_hdr *h;
  /* TODO(ctiller): inline some elements to avoid an allocation */
  grpc_fd_watcher *watchers;
  struct pollfd *pfds;

  h = pollset->data.ptr;
  timeout = grpc_poll_deadline_to_millis_timeout(deadline, now);
  /* TODO(ctiller): perform just one malloc here if we exceed the inline case */
  pfds = gpr_malloc(sizeof(*pfds) * (h->fd_count + 2));
  watchers = gpr_malloc(sizeof(*watchers) * (h->fd_count + 2));
  fd_count = 0;
  pfd_count = 2;
  pfds[0].fd = GRPC_WAKEUP_FD_GET_READ_FD(&grpc_global_wakeup_fd);
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;
  pfds[1].fd = GRPC_WAKEUP_FD_GET_READ_FD(&worker->wakeup_fd);
  pfds[1].events = POLLIN;
  pfds[1].revents = 0;
  for (i = 0; i < h->fd_count; i++) {
    int remove = grpc_fd_is_orphaned(h->fds[i]);
    for (j = 0; !remove && j < h->del_count; j++) {
      if (h->fds[i] == h->dels[j]) remove = 1;
    }
    if (remove) {
      GRPC_FD_UNREF(h->fds[i], "multipoller");
    } else {
      h->fds[fd_count++] = h->fds[i];
      watchers[pfd_count].fd = h->fds[i];
      pfds[pfd_count].fd = h->fds[i]->fd;
      pfds[pfd_count].revents = 0;
      pfd_count++;
    }
  }
  for (j = 0; j < h->del_count; j++) {
    GRPC_FD_UNREF(h->dels[j], "multipoller_del");
  }
  h->del_count = 0;
  h->fd_count = fd_count;
  gpr_mu_unlock(&pollset->mu);

  for (i = 2; i < pfd_count; i++) {
    pfds[i].events = (short)grpc_fd_begin_poll(watchers[i].fd, pollset, worker,
                                               POLLIN, POLLOUT, &watchers[i]);
  }

  /* TODO(vpai): Consider first doing a 0 timeout poll here to avoid
     even going into the blocking annotation if possible */
  GRPC_SCHEDULING_START_BLOCKING_REGION;
  r = grpc_poll_function(pfds, pfd_count, timeout);
  GRPC_SCHEDULING_END_BLOCKING_REGION;

  if (r < 0) {
    gpr_log(GPR_ERROR, "poll() failed: %s", strerror(errno));
    for (i = 2; i < pfd_count; i++) {
      grpc_fd_end_poll(exec_ctx, &watchers[i], 0, 0);
    }
  } else if (r == 0) {
    for (i = 2; i < pfd_count; i++) {
      grpc_fd_end_poll(exec_ctx, &watchers[i], 0, 0);
    }
  } else {
    if (pfds[0].revents & POLLIN_CHECK) {
      grpc_wakeup_fd_consume_wakeup(&grpc_global_wakeup_fd);
    }
    if (pfds[1].revents & POLLIN_CHECK) {
      grpc_wakeup_fd_consume_wakeup(&worker->wakeup_fd);
    }
    for (i = 2; i < pfd_count; i++) {
      if (watchers[i].fd == NULL) {
        grpc_fd_end_poll(exec_ctx, &watchers[i], 0, 0);
        continue;
      }
      grpc_fd_end_poll(exec_ctx, &watchers[i], pfds[i].revents & POLLIN_CHECK,
                       pfds[i].revents & POLLOUT_CHECK);
    }
  }

  gpr_free(pfds);
  gpr_free(watchers);
}

static void multipoll_with_poll_pollset_finish_shutdown(grpc_pollset *pollset) {
  size_t i;
  pollset_hdr *h = pollset->data.ptr;
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
  gpr_free(h->fds);
  gpr_free(h->dels);
  gpr_free(h);
}

static const grpc_pollset_vtable multipoll_with_poll_pollset = {
    multipoll_with_poll_pollset_add_fd, multipoll_with_poll_pollset_del_fd,
    multipoll_with_poll_pollset_maybe_work_and_unlock,
    multipoll_with_poll_pollset_finish_shutdown,
    multipoll_with_poll_pollset_destroy};

void grpc_poll_become_multipoller(grpc_exec_ctx *exec_ctx,
                                  grpc_pollset *pollset, grpc_fd **fds,
                                  size_t nfds) {
  size_t i;
  pollset_hdr *h = gpr_malloc(sizeof(pollset_hdr));
  pollset->vtable = &multipoll_with_poll_pollset;
  pollset->data.ptr = h;
  h->fd_count = nfds;
  h->fd_capacity = nfds;
  h->fds = gpr_malloc(nfds * sizeof(grpc_fd *));
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
