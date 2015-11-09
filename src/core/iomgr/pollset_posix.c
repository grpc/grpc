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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/core/iomgr/timer_internal.h"
#include "src/core/iomgr/fd_posix.h"
#include "src/core/iomgr/iomgr_internal.h"
#include "src/core/iomgr/socket_utils_posix.h"
#include "src/core/profiling/timers.h"
#include "src/core/support/block_annotate.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/tls.h>
#include <grpc/support/useful.h>

GPR_TLS_DECL(g_current_thread_poller);
GPR_TLS_DECL(g_current_thread_worker);

/** Default poll() function - a pointer so that it can be overridden by some
 *  tests */
grpc_poll_function_type grpc_poll_function = poll;

/** The alarm system needs to be able to wakeup 'some poller' sometimes
 *  (specifically when a new alarm needs to be triggered earlier than the next
 *  alarm 'epoch').
 *  This wakeup_fd gives us something to alert on when such a case occurs. */
grpc_wakeup_fd grpc_global_wakeup_fd;

static void remove_worker(grpc_pollset *p, grpc_pollset_worker *worker) {
  worker->prev->next = worker->next;
  worker->next->prev = worker->prev;
}

int grpc_pollset_has_workers(grpc_pollset *p) {
  return p->root_worker.next != &p->root_worker;
}

static grpc_pollset_worker *pop_front_worker(grpc_pollset *p) {
  if (grpc_pollset_has_workers(p)) {
    grpc_pollset_worker *w = p->root_worker.next;
    remove_worker(p, w);
    return w;
  } else {
    return NULL;
  }
}

static void push_back_worker(grpc_pollset *p, grpc_pollset_worker *worker) {
  worker->next = &p->root_worker;
  worker->prev = worker->next->prev;
  worker->prev->next = worker->next->prev = worker;
}

static void push_front_worker(grpc_pollset *p, grpc_pollset_worker *worker) {
  worker->prev = &p->root_worker;
  worker->next = worker->prev->next;
  worker->prev->next = worker->next->prev = worker;
}

void grpc_pollset_kick_ext(grpc_pollset *p,
                           grpc_pollset_worker *specific_worker,
                           gpr_uint32 flags) {
  GPR_TIMER_BEGIN("grpc_pollset_kick_ext", 0);

  /* pollset->mu already held */
  if (specific_worker != NULL) {
    if (specific_worker == GRPC_POLLSET_KICK_BROADCAST) {
      GPR_TIMER_BEGIN("grpc_pollset_kick_ext.broadcast", 0);
      GPR_ASSERT((flags & GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) == 0);
      for (specific_worker = p->root_worker.next;
           specific_worker != &p->root_worker;
           specific_worker = specific_worker->next) {
        grpc_wakeup_fd_wakeup(&specific_worker->wakeup_fd);
      }
      p->kicked_without_pollers = 1;
      GPR_TIMER_END("grpc_pollset_kick_ext.broadcast", 0);
    } else if (gpr_tls_get(&g_current_thread_worker) !=
               (gpr_intptr)specific_worker) {
      GPR_TIMER_MARK("different_thread_worker", 0);
      if ((flags & GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) != 0) {
        specific_worker->reevaluate_polling_on_wakeup = 1;
      }
      specific_worker->kicked_specifically = 1;
      grpc_wakeup_fd_wakeup(&specific_worker->wakeup_fd);
    } else if ((flags & GRPC_POLLSET_CAN_KICK_SELF) != 0) {
      GPR_TIMER_MARK("kick_yoself", 0);
      if ((flags & GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) != 0) {
        specific_worker->reevaluate_polling_on_wakeup = 1;
      }
      specific_worker->kicked_specifically = 1;
      grpc_wakeup_fd_wakeup(&specific_worker->wakeup_fd);
    }
  } else if (gpr_tls_get(&g_current_thread_poller) != (gpr_intptr)p) {
    GPR_ASSERT((flags & GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) == 0);
    GPR_TIMER_MARK("kick_anonymous", 0);
    specific_worker = pop_front_worker(p);
    if (specific_worker != NULL) {
      if (gpr_tls_get(&g_current_thread_worker) ==
          (gpr_intptr)specific_worker) {
        GPR_TIMER_MARK("kick_anonymous_not_self", 0);
        push_back_worker(p, specific_worker);
        specific_worker = pop_front_worker(p);
        if ((flags & GRPC_POLLSET_CAN_KICK_SELF) == 0 &&
            gpr_tls_get(&g_current_thread_worker) ==
                (gpr_intptr)specific_worker) {
          push_back_worker(p, specific_worker);
          specific_worker = NULL;
        }
      }
      if (specific_worker != NULL) {
        GPR_TIMER_MARK("finally_kick", 0);
        push_back_worker(p, specific_worker);
        grpc_wakeup_fd_wakeup(&specific_worker->wakeup_fd);
      }
    } else {
      GPR_TIMER_MARK("kicked_no_pollers", 0);
      p->kicked_without_pollers = 1;
    }
  }

  GPR_TIMER_END("grpc_pollset_kick_ext", 0);
}

void grpc_pollset_kick(grpc_pollset *p, grpc_pollset_worker *specific_worker) {
  grpc_pollset_kick_ext(p, specific_worker, 0);
}

/* global state management */

void grpc_pollset_global_init(void) {
  gpr_tls_init(&g_current_thread_poller);
  gpr_tls_init(&g_current_thread_worker);
  grpc_wakeup_fd_global_init();
  grpc_wakeup_fd_init(&grpc_global_wakeup_fd);
}

void grpc_pollset_global_shutdown(void) {
  grpc_wakeup_fd_destroy(&grpc_global_wakeup_fd);
  grpc_wakeup_fd_global_destroy();
  gpr_tls_destroy(&g_current_thread_poller);
  gpr_tls_destroy(&g_current_thread_worker);
}

void grpc_kick_poller(void) { grpc_wakeup_fd_wakeup(&grpc_global_wakeup_fd); }

/* main interface */

static void become_basic_pollset(grpc_pollset *pollset, grpc_fd *fd_or_null);

void grpc_pollset_init(grpc_pollset *pollset) {
  gpr_mu_init(&pollset->mu);
  pollset->root_worker.next = pollset->root_worker.prev = &pollset->root_worker;
  pollset->in_flight_cbs = 0;
  pollset->shutting_down = 0;
  pollset->called_shutdown = 0;
  pollset->idle_jobs.head = pollset->idle_jobs.tail = NULL;
  become_basic_pollset(pollset, NULL);
}

void grpc_pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                         grpc_fd *fd) {
  gpr_mu_lock(&pollset->mu);
  pollset->vtable->add_fd(exec_ctx, pollset, fd, 1);
/* the following (enabled only in debug) will reacquire and then release
   our lock - meaning that if the unlocking flag passed to del_fd above is
   not respected, the code will deadlock (in a way that we have a chance of
   debugging) */
#ifndef NDEBUG
  gpr_mu_lock(&pollset->mu);
  gpr_mu_unlock(&pollset->mu);
#endif
}

void grpc_pollset_del_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                         grpc_fd *fd) {
  gpr_mu_lock(&pollset->mu);
  pollset->vtable->del_fd(exec_ctx, pollset, fd, 1);
/* the following (enabled only in debug) will reacquire and then release
   our lock - meaning that if the unlocking flag passed to del_fd above is
   not respected, the code will deadlock (in a way that we have a chance of
   debugging) */
#ifndef NDEBUG
  gpr_mu_lock(&pollset->mu);
  gpr_mu_unlock(&pollset->mu);
#endif
}

static void finish_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset) {
  GPR_ASSERT(grpc_closure_list_empty(pollset->idle_jobs));
  pollset->vtable->finish_shutdown(pollset);
  grpc_exec_ctx_enqueue(exec_ctx, pollset->shutdown_done, 1);
}

void grpc_pollset_work(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                       grpc_pollset_worker *worker, gpr_timespec now,
                       gpr_timespec deadline) {
  /* pollset->mu already held */
  int added_worker = 0;
  int locked = 1;
  int queued_work = 0;
  int keep_polling = 0;
  GPR_TIMER_BEGIN("grpc_pollset_work", 0);
  /* this must happen before we (potentially) drop pollset->mu */
  worker->next = worker->prev = NULL;
  worker->reevaluate_polling_on_wakeup = 0;
  worker->kicked_specifically = 0;
  /* TODO(ctiller): pool these */
  grpc_wakeup_fd_init(&worker->wakeup_fd);
  /* If there's work waiting for the pollset to be idle, and the
     pollset is idle, then do that work */
  if (!grpc_pollset_has_workers(pollset) &&
      !grpc_closure_list_empty(pollset->idle_jobs)) {
    grpc_exec_ctx_enqueue_list(exec_ctx, &pollset->idle_jobs);
    goto done;
  }
  /* Check alarms - these are a global resource so we just ping
     each time through on every pollset.
     May update deadline to ensure timely wakeups.
     TODO(ctiller): can this work be localized? */
  if (grpc_timer_check(exec_ctx, now, &deadline)) {
    gpr_mu_unlock(&pollset->mu);
    locked = 0;
    goto done;
  }
  /* If we're shutting down then we don't execute any extended work */
  if (pollset->shutting_down) {
    goto done;
  }
  /* Give do_promote priority so we don't starve it out */
  if (pollset->in_flight_cbs) {
    gpr_mu_unlock(&pollset->mu);
    locked = 0;
    goto done;
  }
  /* Start polling, and keep doing so while we're being asked to
     re-evaluate our pollers (this allows poll() based pollers to
     ensure they don't miss wakeups) */
  keep_polling = 1;
  while (keep_polling) {
    keep_polling = 0;
    if (!pollset->kicked_without_pollers) {
      if (!added_worker) {
        push_front_worker(pollset, worker);
        added_worker = 1;
        gpr_tls_set(&g_current_thread_worker, (gpr_intptr)worker);
      }
      gpr_tls_set(&g_current_thread_poller, (gpr_intptr)pollset);
      GPR_TIMER_BEGIN("maybe_work_and_unlock", 0);
      pollset->vtable->maybe_work_and_unlock(exec_ctx, pollset, worker,
                                             deadline, now);
      GPR_TIMER_END("maybe_work_and_unlock", 0);
      locked = 0;
      gpr_tls_set(&g_current_thread_poller, 0);
    } else {
      pollset->kicked_without_pollers = 0;
    }
  /* Finished execution - start cleaning up.
     Note that we may arrive here from outside the enclosing while() loop.
     In that case we won't loop though as we haven't added worker to the
     worker list, which means nobody could ask us to re-evaluate polling). */
  done:
    if (!locked) {
      queued_work |= grpc_exec_ctx_flush(exec_ctx);
      gpr_mu_lock(&pollset->mu);
      locked = 1;
    }
    /* If we're forced to re-evaluate polling (via grpc_pollset_kick with
       GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) then we land here and force
       a loop */
    if (worker->reevaluate_polling_on_wakeup) {
      worker->reevaluate_polling_on_wakeup = 0;
      pollset->kicked_without_pollers = 0;
      if (queued_work || worker->kicked_specifically) {
        /* If there's queued work on the list, then set the deadline to be
           immediate so we get back out of the polling loop quickly */
        deadline = gpr_inf_past(GPR_CLOCK_MONOTONIC);
      }
      keep_polling = 1;
    }
  }
  if (added_worker) {
    remove_worker(pollset, worker);
    gpr_tls_set(&g_current_thread_worker, 0);
  }
  grpc_wakeup_fd_destroy(&worker->wakeup_fd);
  if (pollset->shutting_down) {
    if (grpc_pollset_has_workers(pollset)) {
      grpc_pollset_kick(pollset, NULL);
    } else if (!pollset->called_shutdown && pollset->in_flight_cbs == 0) {
      pollset->called_shutdown = 1;
      gpr_mu_unlock(&pollset->mu);
      finish_shutdown(exec_ctx, pollset);
      grpc_exec_ctx_flush(exec_ctx);
      /* Continuing to access pollset here is safe -- it is the caller's
       * responsibility to not destroy when it has outstanding calls to
       * grpc_pollset_work.
       * TODO(dklempner): Can we refactor the shutdown logic to avoid this? */
      gpr_mu_lock(&pollset->mu);
    } else if (!grpc_closure_list_empty(pollset->idle_jobs)) {
      gpr_mu_unlock(&pollset->mu);
      grpc_exec_ctx_enqueue_list(exec_ctx, &pollset->idle_jobs);
      grpc_exec_ctx_flush(exec_ctx);
      gpr_mu_lock(&pollset->mu);
    }
  }
  GPR_TIMER_END("grpc_pollset_work", 0);
}

void grpc_pollset_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_closure *closure) {
  int call_shutdown = 0;
  gpr_mu_lock(&pollset->mu);
  GPR_ASSERT(!pollset->shutting_down);
  pollset->shutting_down = 1;
  if (!pollset->called_shutdown && pollset->in_flight_cbs == 0 &&
      !grpc_pollset_has_workers(pollset)) {
    pollset->called_shutdown = 1;
    call_shutdown = 1;
  }
  if (!grpc_pollset_has_workers(pollset)) {
    grpc_exec_ctx_enqueue_list(exec_ctx, &pollset->idle_jobs);
  }
  pollset->shutdown_done = closure;
  grpc_pollset_kick(pollset, GRPC_POLLSET_KICK_BROADCAST);
  gpr_mu_unlock(&pollset->mu);

  if (call_shutdown) {
    finish_shutdown(exec_ctx, pollset);
  }
}

void grpc_pollset_destroy(grpc_pollset *pollset) {
  GPR_ASSERT(pollset->shutting_down);
  GPR_ASSERT(pollset->in_flight_cbs == 0);
  GPR_ASSERT(!grpc_pollset_has_workers(pollset));
  pollset->vtable->destroy(pollset);
  gpr_mu_destroy(&pollset->mu);
}

int grpc_poll_deadline_to_millis_timeout(gpr_timespec deadline,
                                         gpr_timespec now) {
  gpr_timespec timeout;
  static const int max_spin_polling_us = 10;
  if (gpr_time_cmp(deadline, gpr_inf_future(deadline.clock_type)) == 0) {
    return -1;
  }
  if (gpr_time_cmp(deadline, gpr_time_add(now, gpr_time_from_micros(
                                                   max_spin_polling_us,
                                                   GPR_TIMESPAN))) <= 0) {
    return 0;
  }
  timeout = gpr_time_sub(deadline, now);
  return gpr_time_to_millis(gpr_time_add(
      timeout, gpr_time_from_nanos(GPR_NS_PER_MS - 1, GPR_TIMESPAN)));
}

/*
 * basic_pollset - a vtable that provides polling for zero or one file
 *                 descriptor via poll()
 */

typedef struct grpc_unary_promote_args {
  const grpc_pollset_vtable *original_vtable;
  grpc_pollset *pollset;
  grpc_fd *fd;
  grpc_closure promotion_closure;
} grpc_unary_promote_args;

static void basic_do_promote(grpc_exec_ctx *exec_ctx, void *args, int success) {
  grpc_unary_promote_args *up_args = args;
  const grpc_pollset_vtable *original_vtable = up_args->original_vtable;
  grpc_pollset *pollset = up_args->pollset;
  grpc_fd *fd = up_args->fd;

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
  GPR_ASSERT(!grpc_pollset_has_workers(pollset));

  gpr_free(up_args);
  /* At this point the pollset may no longer be a unary poller. In that case
   * we should just call the right add function and be done. */
  /* TODO(klempner): If we're not careful this could cause infinite recursion.
   * That's not a problem for now because empty_pollset has a trivial poller
   * and we don't have any mechanism to unbecome multipoller. */
  pollset->in_flight_cbs--;
  if (pollset->shutting_down) {
    /* We don't care about this pollset anymore. */
    if (pollset->in_flight_cbs == 0 && !pollset->called_shutdown) {
      pollset->called_shutdown = 1;
      finish_shutdown(exec_ctx, pollset);
    }
  } else if (grpc_fd_is_orphaned(fd)) {
    /* Don't try to add it to anything, we'll drop our ref on it below */
  } else if (pollset->vtable != original_vtable) {
    pollset->vtable->add_fd(exec_ctx, pollset, fd, 0);
  } else if (fd != pollset->data.ptr) {
    grpc_fd *fds[2];
    fds[0] = pollset->data.ptr;
    fds[1] = fd;

    if (fds[0] && !grpc_fd_is_orphaned(fds[0])) {
      grpc_platform_become_multipoller(exec_ctx, pollset, fds,
                                       GPR_ARRAY_SIZE(fds));
      GRPC_FD_UNREF(fds[0], "basicpoll");
    } else {
      /* old fd is orphaned and we haven't cleaned it up until now, so remain a
       * unary poller */
      /* Note that it is possible that fds[1] is also orphaned at this point.
       * That's okay, we'll correct it at the next add or poll. */
      if (fds[0]) GRPC_FD_UNREF(fds[0], "basicpoll");
      pollset->data.ptr = fd;
      GRPC_FD_REF(fd, "basicpoll");
    }
  }

  gpr_mu_unlock(&pollset->mu);

  /* Matching ref in basic_pollset_add_fd */
  GRPC_FD_UNREF(fd, "basicpoll_add");
}

static void basic_pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                                 grpc_fd *fd, int and_unlock_pollset) {
  grpc_unary_promote_args *up_args;
  GPR_ASSERT(fd);
  if (fd == pollset->data.ptr) goto exit;

  if (!grpc_pollset_has_workers(pollset)) {
    /* Fast path -- no in flight cbs */
    /* TODO(klempner): Comment this out and fix any test failures or establish
     * they are due to timing issues */
    grpc_fd *fds[2];
    fds[0] = pollset->data.ptr;
    fds[1] = fd;

    if (fds[0] == NULL) {
      pollset->data.ptr = fd;
      GRPC_FD_REF(fd, "basicpoll");
    } else if (!grpc_fd_is_orphaned(fds[0])) {
      grpc_platform_become_multipoller(exec_ctx, pollset, fds,
                                       GPR_ARRAY_SIZE(fds));
      GRPC_FD_UNREF(fds[0], "basicpoll");
    } else {
      /* old fd is orphaned and we haven't cleaned it up until now, so remain a
       * unary poller */
      GRPC_FD_UNREF(fds[0], "basicpoll");
      pollset->data.ptr = fd;
      GRPC_FD_REF(fd, "basicpoll");
    }
    goto exit;
  }

  /* Now we need to promote. This needs to happen when we're not polling. Since
   * this may be called from poll, the wait needs to happen asynchronously. */
  GRPC_FD_REF(fd, "basicpoll_add");
  pollset->in_flight_cbs++;
  up_args = gpr_malloc(sizeof(*up_args));
  up_args->fd = fd;
  up_args->original_vtable = pollset->vtable;
  up_args->pollset = pollset;
  up_args->promotion_closure.cb = basic_do_promote;
  up_args->promotion_closure.cb_arg = up_args;

  grpc_closure_list_add(&pollset->idle_jobs, &up_args->promotion_closure, 1);
  grpc_pollset_kick(pollset, GRPC_POLLSET_KICK_BROADCAST);

exit:
  if (and_unlock_pollset) {
    gpr_mu_unlock(&pollset->mu);
  }
}

static void basic_pollset_del_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                                 grpc_fd *fd, int and_unlock_pollset) {
  GPR_ASSERT(fd);
  if (fd == pollset->data.ptr) {
    GRPC_FD_UNREF(pollset->data.ptr, "basicpoll");
    pollset->data.ptr = NULL;
  }

  if (and_unlock_pollset) {
    gpr_mu_unlock(&pollset->mu);
  }
}

static void basic_pollset_maybe_work_and_unlock(grpc_exec_ctx *exec_ctx,
                                                grpc_pollset *pollset,
                                                grpc_pollset_worker *worker,
                                                gpr_timespec deadline,
                                                gpr_timespec now) {
#define POLLOUT_CHECK (POLLOUT | POLLHUP | POLLERR)
#define POLLIN_CHECK (POLLIN | POLLHUP | POLLERR)

  struct pollfd pfd[3];
  grpc_fd *fd;
  grpc_fd_watcher fd_watcher;
  int timeout;
  int r;
  nfds_t nfds;

  fd = pollset->data.ptr;
  if (fd && grpc_fd_is_orphaned(fd)) {
    GRPC_FD_UNREF(fd, "basicpoll");
    fd = pollset->data.ptr = NULL;
  }
  timeout = grpc_poll_deadline_to_millis_timeout(deadline, now);
  pfd[0].fd = GRPC_WAKEUP_FD_GET_READ_FD(&grpc_global_wakeup_fd);
  pfd[0].events = POLLIN;
  pfd[0].revents = 0;
  pfd[1].fd = GRPC_WAKEUP_FD_GET_READ_FD(&worker->wakeup_fd);
  pfd[1].events = POLLIN;
  pfd[1].revents = 0;
  nfds = 2;
  if (fd) {
    pfd[2].fd = fd->fd;
    pfd[2].revents = 0;
    GRPC_FD_REF(fd, "basicpoll_begin");
    gpr_mu_unlock(&pollset->mu);
    pfd[2].events = (short)grpc_fd_begin_poll(fd, pollset, worker, POLLIN,
                                              POLLOUT, &fd_watcher);
    if (pfd[2].events != 0) {
      nfds++;
    }
  } else {
    gpr_mu_unlock(&pollset->mu);
  }

  /* TODO(vpai): Consider first doing a 0 timeout poll here to avoid
     even going into the blocking annotation if possible */
  /* poll fd count (argument 2) is shortened by one if we have no events
     to poll on - such that it only includes the kicker */
  GPR_TIMER_BEGIN("poll", 0);
  GRPC_SCHEDULING_START_BLOCKING_REGION;
  r = grpc_poll_function(pfd, nfds, timeout);
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  GPR_TIMER_END("poll", 0);

  if (r < 0) {
    gpr_log(GPR_ERROR, "poll() failed: %s", strerror(errno));
    if (fd) {
      grpc_fd_end_poll(exec_ctx, &fd_watcher, 0, 0);
    }
  } else if (r == 0) {
    if (fd) {
      grpc_fd_end_poll(exec_ctx, &fd_watcher, 0, 0);
    }
  } else {
    if (pfd[0].revents & POLLIN_CHECK) {
      grpc_wakeup_fd_consume_wakeup(&grpc_global_wakeup_fd);
    }
    if (pfd[1].revents & POLLIN_CHECK) {
      grpc_wakeup_fd_consume_wakeup(&worker->wakeup_fd);
    }
    if (nfds > 2) {
      grpc_fd_end_poll(exec_ctx, &fd_watcher, pfd[2].revents & POLLIN_CHECK,
                       pfd[2].revents & POLLOUT_CHECK);
    } else if (fd) {
      grpc_fd_end_poll(exec_ctx, &fd_watcher, 0, 0);
    }
  }

  if (fd) {
    GRPC_FD_UNREF(fd, "basicpoll_begin");
  }
}

static void basic_pollset_destroy(grpc_pollset *pollset) {
  if (pollset->data.ptr != NULL) {
    GRPC_FD_UNREF(pollset->data.ptr, "basicpoll");
    pollset->data.ptr = NULL;
  }
}

static const grpc_pollset_vtable basic_pollset = {
    basic_pollset_add_fd, basic_pollset_del_fd,
    basic_pollset_maybe_work_and_unlock, basic_pollset_destroy,
    basic_pollset_destroy};

static void become_basic_pollset(grpc_pollset *pollset, grpc_fd *fd_or_null) {
  pollset->vtable = &basic_pollset;
  pollset->data.ptr = fd_or_null;
  if (fd_or_null != NULL) {
    GRPC_FD_REF(fd_or_null, "basicpoll");
  }
}

#endif /* GPR_POSIX_POLLSET */
