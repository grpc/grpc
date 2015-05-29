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
#include <unistd.h>

#include "src/core/iomgr/alarm_internal.h"
#include "src/core/iomgr/fd_posix.h"
#include "src/core/iomgr/iomgr_internal.h"
#include "src/core/iomgr/socket_utils_posix.h"
#include "src/core/profiling/timers.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/tls.h>
#include <grpc/support/useful.h>

static grpc_pollset g_backup_pollset;
static int g_shutdown_backup_poller;
static gpr_event g_backup_poller_done;
static gpr_event g_backup_pollset_shutdown_done;

GPR_TLS_DECL(g_current_thread_poller);

static void backup_poller(void *p) {
  gpr_timespec delta = gpr_time_from_millis(100);
  gpr_timespec last_poll = gpr_now();

  gpr_mu_lock(&g_backup_pollset.mu);
  while (g_shutdown_backup_poller == 0) {
    gpr_timespec next_poll = gpr_time_add(last_poll, delta);
    grpc_pollset_work(&g_backup_pollset, gpr_time_add(gpr_now(), gpr_time_from_seconds(1)));
    gpr_mu_unlock(&g_backup_pollset.mu);
    gpr_sleep_until(next_poll);
    gpr_mu_lock(&g_backup_pollset.mu);
    last_poll = next_poll;
  }
  gpr_mu_unlock(&g_backup_pollset.mu);

  gpr_event_set(&g_backup_poller_done, (void *)1);
}

void grpc_pollset_kick(grpc_pollset *p) {
  if (gpr_tls_get(&g_current_thread_poller) != (gpr_intptr)p && p->counter) {
    p->vtable->kick(p);
  }
}

void grpc_pollset_force_kick(grpc_pollset *p) {
  if (gpr_tls_get(&g_current_thread_poller) != (gpr_intptr)p) {
    grpc_pollset_kick_kick(&p->kick_state);
  }
}

static void kick_using_pollset_kick(grpc_pollset *p) {
  if (gpr_tls_get(&g_current_thread_poller) != (gpr_intptr)p) {
    grpc_pollset_kick_kick(&p->kick_state);
  }
}

/* global state management */

grpc_pollset *grpc_backup_pollset(void) { return &g_backup_pollset; }

void grpc_pollset_global_init(void) {
  gpr_thd_id id;

  gpr_tls_init(&g_current_thread_poller);

  /* Initialize kick fd state */
  grpc_pollset_kick_global_init();

  /* initialize the backup pollset */
  grpc_pollset_init(&g_backup_pollset);

  /* start the backup poller thread */
  g_shutdown_backup_poller = 0;
  gpr_event_init(&g_backup_poller_done);
  gpr_event_init(&g_backup_pollset_shutdown_done);
  gpr_thd_new(&id, backup_poller, NULL, NULL);
}

static void on_backup_pollset_shutdown_done(void *arg) {
  gpr_event_set(&g_backup_pollset_shutdown_done, (void *)1);
}

void grpc_pollset_global_shutdown(void) {
  /* terminate the backup poller thread */
  gpr_mu_lock(&g_backup_pollset.mu);
  g_shutdown_backup_poller = 1;
  gpr_mu_unlock(&g_backup_pollset.mu);
  gpr_event_wait(&g_backup_poller_done, gpr_inf_future);

  grpc_pollset_shutdown(&g_backup_pollset, on_backup_pollset_shutdown_done,
                        NULL);
  gpr_event_wait(&g_backup_pollset_shutdown_done, gpr_inf_future);

  /* destroy the backup pollset */
  grpc_pollset_destroy(&g_backup_pollset);

  /* destroy the kick pipes */
  grpc_pollset_kick_global_destroy();

  gpr_tls_destroy(&g_current_thread_poller);
}

/* main interface */

static void become_empty_pollset(grpc_pollset *pollset);
static void become_unary_pollset(grpc_pollset *pollset, grpc_fd *fd);

void grpc_pollset_init(grpc_pollset *pollset) {
  gpr_mu_init(&pollset->mu);
  gpr_cv_init(&pollset->cv);
  grpc_pollset_kick_init(&pollset->kick_state);
  pollset->in_flight_cbs = 0;
  pollset->shutting_down = 0;
  become_empty_pollset(pollset);
}

void grpc_pollset_add_fd(grpc_pollset *pollset, grpc_fd *fd) {
  gpr_mu_lock(&pollset->mu);
  pollset->vtable->add_fd(pollset, fd);
  gpr_cv_broadcast(&pollset->cv);
  gpr_mu_unlock(&pollset->mu);
}

void grpc_pollset_del_fd(grpc_pollset *pollset, grpc_fd *fd) {
  gpr_mu_lock(&pollset->mu);
  pollset->vtable->del_fd(pollset, fd);
  gpr_cv_broadcast(&pollset->cv);
  gpr_mu_unlock(&pollset->mu);
}

int grpc_pollset_work(grpc_pollset *pollset, gpr_timespec deadline) {
  /* pollset->mu already held */
  gpr_timespec now = gpr_now();
  /* FIXME(ctiller): see below */
  gpr_timespec maximum_deadline = gpr_time_add(now, gpr_time_from_seconds(1));
  int r;
  if (gpr_time_cmp(now, deadline) > 0) {
    return 0;
  }
  if (grpc_maybe_call_delayed_callbacks(&pollset->mu, 1)) {
    return 1;
  }
  if (grpc_alarm_check(&pollset->mu, now, &deadline)) {
    return 1;
  }
  /* FIXME(ctiller): we should not clamp deadline, however we have some
     stuck at shutdown bugs that this resolves */
  if (gpr_time_cmp(deadline, maximum_deadline) > 0) {
    deadline = maximum_deadline;
  }
  gpr_tls_set(&g_current_thread_poller, (gpr_intptr)pollset);
  r = pollset->vtable->maybe_work(pollset, deadline, now, 1);
  gpr_tls_set(&g_current_thread_poller, 0);
  return r;
}

void grpc_pollset_shutdown(grpc_pollset *pollset,
                           void (*shutdown_done)(void *arg),
                           void *shutdown_done_arg) {
  int in_flight_cbs;
  gpr_mu_lock(&pollset->mu);
  pollset->shutting_down = 1;
  in_flight_cbs = pollset->in_flight_cbs;
  pollset->shutdown_done_cb = shutdown_done;
  pollset->shutdown_done_arg = shutdown_done_arg;
  gpr_mu_unlock(&pollset->mu);
  if (in_flight_cbs == 0) {
    shutdown_done(shutdown_done_arg);
  }
}

void grpc_pollset_destroy(grpc_pollset *pollset) {
  GPR_ASSERT(pollset->shutting_down);
  GPR_ASSERT(pollset->in_flight_cbs == 0);
  pollset->vtable->destroy(pollset);
  grpc_pollset_kick_destroy(&pollset->kick_state);
  gpr_mu_destroy(&pollset->mu);
  gpr_cv_destroy(&pollset->cv);
}

/*
 * empty_pollset - a vtable that provides polling for NO file descriptors
 */

static void empty_pollset_add_fd(grpc_pollset *pollset, grpc_fd *fd) {
  become_unary_pollset(pollset, fd);
}

static void empty_pollset_del_fd(grpc_pollset *pollset, grpc_fd *fd) {}

static int empty_pollset_maybe_work(grpc_pollset *pollset,
                                    gpr_timespec deadline, gpr_timespec now,
                                    int allow_synchronous_callback) {
  return 0;
}

static void empty_pollset_destroy(grpc_pollset *pollset) {}

static const grpc_pollset_vtable empty_pollset = {
    empty_pollset_add_fd, empty_pollset_del_fd, empty_pollset_maybe_work,
    kick_using_pollset_kick, empty_pollset_destroy};

static void become_empty_pollset(grpc_pollset *pollset) {
  pollset->vtable = &empty_pollset;
}

/*
 * unary_poll_pollset - a vtable that provides polling for one file descriptor
 *                      via poll()
 */


typedef struct grpc_unary_promote_args {
  const grpc_pollset_vtable *original_vtable;
  grpc_pollset *pollset;
  grpc_fd *fd;
} grpc_unary_promote_args;

static void unary_poll_do_promote(void *args, int success) {
  grpc_unary_promote_args *up_args = args;
  const grpc_pollset_vtable *original_vtable = up_args->original_vtable;
  grpc_pollset *pollset = up_args->pollset;
  grpc_fd *fd = up_args->fd;
  int do_shutdown_cb = 0;

  /*
   * This is quite tricky. There are a number of cases to keep in mind here:
   * 1. fd may have been orphaned
   * 2. The pollset may no longer be a unary poller (and we can't let case #1
   * leak to other pollset types!)
   * 3. pollset's fd (which may have changed) may have been orphaned
   * 4. The pollset may be shutting down.
   */

  gpr_mu_lock(&pollset->mu);
  /* First we need to ensure that nobody is polling concurrently */
  while (pollset->counter != 0) {
    grpc_pollset_kick(pollset);
    grpc_iomgr_add_callback(unary_poll_do_promote, up_args);
    gpr_mu_unlock(&pollset->mu);
    return;
  }

  gpr_free(up_args);
  /* At this point the pollset may no longer be a unary poller. In that case
   * we should just call the right add function and be done. */
  /* TODO(klempner): If we're not careful this could cause infinite recursion.
   * That's not a problem for now because empty_pollset has a trivial poller
   * and we don't have any mechanism to unbecome multipoller. */
  pollset->in_flight_cbs--;
  if (pollset->shutting_down) {
    /* We don't care about this pollset anymore. */
    if (pollset->in_flight_cbs == 0) {
      do_shutdown_cb = 1;
    }
  } else if (grpc_fd_is_orphaned(fd)) {
    /* Don't try to add it to anything, we'll drop our ref on it below */
  } else if (pollset->vtable != original_vtable) {
    pollset->vtable->add_fd(pollset, fd);
  } else if (fd != pollset->data.ptr) {
    grpc_fd *fds[2];
    fds[0] = pollset->data.ptr;
    fds[1] = fd;

    if (!grpc_fd_is_orphaned(fds[0])) {
      grpc_platform_become_multipoller(pollset, fds, GPR_ARRAY_SIZE(fds));
      grpc_fd_unref(fds[0]);
    } else {
      /* old fd is orphaned and we haven't cleaned it up until now, so remain a
       * unary poller */
      /* Note that it is possible that fds[1] is also orphaned at this point.
       * That's okay, we'll correct it at the next add or poll. */
      grpc_fd_unref(fds[0]);
      pollset->data.ptr = fd;
      grpc_fd_ref(fd);
    }
  }

  gpr_cv_broadcast(&pollset->cv);
  gpr_mu_unlock(&pollset->mu);

  if (do_shutdown_cb) {
    pollset->shutdown_done_cb(pollset->shutdown_done_arg);
  }

  /* Matching ref in unary_poll_pollset_add_fd */
  grpc_fd_unref(fd);
}

static void unary_poll_pollset_add_fd(grpc_pollset *pollset, grpc_fd *fd) {
  grpc_unary_promote_args *up_args;
  if (fd == pollset->data.ptr) return;

  if (!pollset->counter) {
    /* Fast path -- no in flight cbs */
    /* TODO(klempner): Comment this out and fix any test failures or establish
     * they are due to timing issues */
    grpc_fd *fds[2];
    fds[0] = pollset->data.ptr;
    fds[1] = fd;

    if (!grpc_fd_is_orphaned(fds[0])) {
      grpc_platform_become_multipoller(pollset, fds, GPR_ARRAY_SIZE(fds));
      grpc_fd_unref(fds[0]);
    } else {
      /* old fd is orphaned and we haven't cleaned it up until now, so remain a
       * unary poller */
      grpc_fd_unref(fds[0]);
      pollset->data.ptr = fd;
      grpc_fd_ref(fd);
    }
    return;
  }

  /* Now we need to promote. This needs to happen when we're not polling. Since
   * this may be called from poll, the wait needs to happen asynchronously. */
  grpc_fd_ref(fd);
  pollset->in_flight_cbs++;
  up_args = gpr_malloc(sizeof(*up_args));
  up_args->pollset = pollset;
  up_args->fd = fd;
  up_args->original_vtable = pollset->vtable;
  grpc_iomgr_add_callback(unary_poll_do_promote, up_args);

  grpc_pollset_kick(pollset);
}

static void unary_poll_pollset_del_fd(grpc_pollset *pollset, grpc_fd *fd) {
  if (fd == pollset->data.ptr) {
    grpc_fd_unref(pollset->data.ptr);
    become_empty_pollset(pollset);
  }
}

static int unary_poll_pollset_maybe_work(grpc_pollset *pollset,
                                         gpr_timespec deadline,
                                         gpr_timespec now,
                                         int allow_synchronous_callback) {
  struct pollfd pfd[2];
  grpc_fd *fd;
  grpc_fd_watcher fd_watcher;
  int timeout;
  int r;

  if (pollset->counter) {
    return 0;
  }
  if (pollset->in_flight_cbs) {
    /* Give do_promote priority so we don't starve it out */
    return 0;
  }
  fd = pollset->data.ptr;
  if (grpc_fd_is_orphaned(fd)) {
    grpc_fd_unref(fd);
    become_empty_pollset(pollset);
    return 0;
  }
  if (gpr_time_cmp(deadline, gpr_inf_future) == 0) {
    timeout = -1;
  } else {
    timeout = gpr_time_to_millis(gpr_time_sub(deadline, now));
    if (timeout <= 0) {
      return 1;
    }
  }
  pfd[0].fd = grpc_pollset_kick_pre_poll(&pollset->kick_state);
  if (pfd[0].fd < 0) {
    /* Already kicked */
    return 1;
  }
  pfd[0].events = POLLIN;
  pfd[0].revents = 0;
  pfd[1].fd = fd->fd;
  pfd[1].revents = 0;
  pollset->counter = 1;
  gpr_mu_unlock(&pollset->mu);

  pfd[1].events = grpc_fd_begin_poll(fd, pollset, POLLIN, POLLOUT, &fd_watcher);

  /* poll fd count (argument 2) is shortened by one if we have no events
     to poll on - such that it only includes the kicker */
  r = poll(pfd, GPR_ARRAY_SIZE(pfd) - (pfd[1].events == 0), timeout);
  GRPC_TIMER_MARK(GRPC_PTAG_POLL_FINISHED, r);

  grpc_fd_end_poll(&fd_watcher, pfd[1].revents & POLLIN, pfd[1].revents & POLLOUT);

  if (r < 0) {
    if (errno != EINTR) {
      gpr_log(GPR_ERROR, "poll() failed: %s", strerror(errno));
    }
  } else if (r == 0) {
    /* do nothing */
  } else {
    if (pfd[0].revents & POLLIN) {
      grpc_pollset_kick_consume(&pollset->kick_state);
    }
    if (pfd[1].revents & (POLLIN | POLLHUP | POLLERR)) {
      grpc_fd_become_readable(fd, allow_synchronous_callback);
    }
    if (pfd[1].revents & (POLLOUT | POLLHUP | POLLERR)) {
      grpc_fd_become_writable(fd, allow_synchronous_callback);
    }
  }

  grpc_pollset_kick_post_poll(&pollset->kick_state);

  gpr_mu_lock(&pollset->mu);
  pollset->counter = 0;
  gpr_cv_broadcast(&pollset->cv);
  return 1;
}

static void unary_poll_pollset_destroy(grpc_pollset *pollset) {
  GPR_ASSERT(pollset->counter == 0);
  grpc_fd_unref(pollset->data.ptr);
}

static const grpc_pollset_vtable unary_poll_pollset = {
    unary_poll_pollset_add_fd, unary_poll_pollset_del_fd,
    unary_poll_pollset_maybe_work, kick_using_pollset_kick,
    unary_poll_pollset_destroy};

static void become_unary_pollset(grpc_pollset *pollset, grpc_fd *fd) {
  pollset->vtable = &unary_poll_pollset;
  pollset->counter = 0;
  pollset->data.ptr = fd;
  grpc_fd_ref(fd);
}

#endif /* GPR_POSIX_POLLSET */
