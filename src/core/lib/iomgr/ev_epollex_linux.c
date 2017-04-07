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

/* This polling engine is only relevant on linux kernels supporting epoll() */
#ifdef GRPC_LINUX_EPOLL

#include "src/core/lib/iomgr/ev_epoll_linux.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/tls.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/lockfree_event.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/lib/iomgr/workqueue.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/block_annotate.h"
#include "src/core/lib/support/spinlock.h"

#ifndef EPOLLEXCLUSIVE
#define EPOLLEXCLUSIVE (1u << 28)
#endif

/* TODO: sreek: Right now, this wakes up all pollers. In future we should make
 * sure to wake up one polling thread (which can wake up other threads if
 * needed) */
static grpc_wakeup_fd global_wakeup_fd;

/*******************************************************************************
 * Pollset-set sibling link
 */

typedef enum {
  PSS_FD,
  PSS_POLLSET,
  PSS_POLLSET_SET,
  PSS_OBJ_TYPE_COUNT
} pss_obj_type;

typedef struct pss_obj {
  gpr_mu mu;
  struct pss_obj *pss_next;
  struct pss_obj *pss_prev;
  int pss_refs;
  grpc_pollset_set *pss_master;
} pss_obj;

static void pss_obj_init(pss_obj *obj) {
  gpr_mu_init(&obj->mu);
  obj->pss_refs = 0;
  obj->pss_next = NULL;
  obj->pss_prev = NULL;
  obj->pss_master = NULL;
}

/*******************************************************************************
 * Fd Declarations
 */

struct grpc_fd {
  pss_obj po;
  int fd;
  /* refst format:
       bit 0    : 1=Active / 0=Orphaned
       bits 1-n : refcount
     Ref/Unref by two to avoid altering the orphaned bit */
  gpr_atm refst;

  /* Wakeup fd used to wake pollers to check the contents of workqueue_items */
  grpc_wakeup_fd workqueue_wakeup_fd;
  grpc_closure_scheduler workqueue_scheduler;
  /* Spinlock guarding the read end of the workqueue (must be held to pop from
   * workqueue_items) */
  gpr_spinlock workqueue_read_mu;
  /* Queue of closures to be executed */
  gpr_mpscq workqueue_items;
  /* Count of items in workqueue_items */
  gpr_atm workqueue_item_count;

  /* The fd is either closed or we relinquished control of it. In either
     cases, this indicates that the 'fd' on this structure is no longer
     valid */
  bool orphaned;

  gpr_atm read_closure;
  gpr_atm write_closure;

  struct grpc_fd *freelist_next;
  grpc_closure *on_done_closure;

  /* The pollset that last noticed that the fd is readable. The actual type
   * stored in this is (grpc_pollset *) */
  gpr_atm read_notifier_pollset;

  grpc_iomgr_object iomgr_object;
};

static void fd_global_init(void);
static void fd_global_shutdown(void);

static void workqueue_enqueue(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                              grpc_error *error);

static const grpc_closure_scheduler_vtable workqueue_scheduler_vtable = {
    workqueue_enqueue, workqueue_enqueue, "workqueue"};

/*******************************************************************************
 * Pollset Declarations
 */
struct grpc_pollset_worker {
  bool kicked;
  bool initialized_cv;
  gpr_cv cv;
  grpc_pollset_worker *next;
  grpc_pollset_worker *prev;
};

struct grpc_pollset {
  pss_obj po;
  int epfd;
  int num_pollers;
  gpr_atm shutdown_atm;
  grpc_closure *shutdown_closure;
  grpc_wakeup_fd pollset_wakeup;
  grpc_pollset_worker *root_worker;

  grpc_pollset *pss_next;
  grpc_pollset *pss_prev;
  int pss_refs;
  grpc_pollset_set *pss_master;
};

/*******************************************************************************
 * Pollset-set Declarations
 */
struct grpc_pollset_set {
  pss_obj po;
  gpr_refcount refs;

  /* roots are only used if master == self */
  pss_obj *roots[PSS_OBJ_TYPE_COUNT];
};

/*******************************************************************************
 * Common helpers
 */

static bool append_error(grpc_error **composite, grpc_error *error,
                         const char *desc) {
  if (error == GRPC_ERROR_NONE) return true;
  if (*composite == GRPC_ERROR_NONE) {
    *composite = GRPC_ERROR_CREATE_FROM_COPIED_STRING(desc);
  }
  *composite = grpc_error_add_child(*composite, error);
  return false;
}

/*******************************************************************************
 * Fd Definitions
 */

/* We need to keep a freelist not because of any concerns of malloc performance
 * but instead so that implementations with multiple threads in (for example)
 * epoll_wait deal with the race between pollset removal and incoming poll
 * notifications.
 *
 * The problem is that the poller ultimately holds a reference to this
 * object, so it is very difficult to know when is safe to free it, at least
 * without some expensive synchronization.
 *
 * If we keep the object freelisted, in the worst case losing this race just
 * becomes a spurious read notification on a reused fd.
 */

/* The alarm system needs to be able to wakeup 'some poller' sometimes
 * (specifically when a new alarm needs to be triggered earlier than the next
 * alarm 'epoch'). This wakeup_fd gives us something to alert on when such a
 * case occurs. */

static grpc_fd *fd_freelist = NULL;
static gpr_mu fd_freelist_mu;

#ifdef GRPC_FD_REF_COUNT_DEBUG
#define REF_BY(fd, n, reason) ref_by(fd, n, reason, __FILE__, __LINE__)
#define UNREF_BY(fd, n, reason) unref_by(fd, n, reason, __FILE__, __LINE__)
static void ref_by(grpc_fd *fd, int n, const char *reason, const char *file,
                   int line) {
  gpr_log(GPR_DEBUG, "FD %d %p   ref %d %ld -> %ld [%s; %s:%d]", fd->fd,
          (void *)fd, n, gpr_atm_no_barrier_load(&fd->refst),
          gpr_atm_no_barrier_load(&fd->refst) + n, reason, file, line);
#else
#define REF_BY(fd, n, reason) ref_by(fd, n)
#define UNREF_BY(fd, n, reason) unref_by(fd, n)
static void ref_by(grpc_fd *fd, int n) {
#endif
  GPR_ASSERT(gpr_atm_no_barrier_fetch_add(&fd->refst, n) > 0);
}

#ifdef GRPC_FD_REF_COUNT_DEBUG
static void unref_by(grpc_fd *fd, int n, const char *reason, const char *file,
                     int line) {
  gpr_atm old;
  gpr_log(GPR_DEBUG, "FD %d %p unref %d %ld -> %ld [%s; %s:%d]", fd->fd,
          (void *)fd, n, gpr_atm_no_barrier_load(&fd->refst),
          gpr_atm_no_barrier_load(&fd->refst) - n, reason, file, line);
#else
static void unref_by(grpc_fd *fd, int n) {
  gpr_atm old;
#endif
  old = gpr_atm_full_fetch_add(&fd->refst, -n);
  if (old == n) {
    /* Add the fd to the freelist */
    gpr_mu_lock(&fd_freelist_mu);
    fd->freelist_next = fd_freelist;
    fd_freelist = fd;
    grpc_iomgr_unregister_object(&fd->iomgr_object);

    grpc_lfev_destroy(&fd->read_closure);
    grpc_lfev_destroy(&fd->write_closure);

    gpr_mu_unlock(&fd_freelist_mu);
  } else {
    GPR_ASSERT(old > n);
  }
}

static void fd_global_init(void) { gpr_mu_init(&fd_freelist_mu); }

static void fd_global_shutdown(void) {
  gpr_mu_lock(&fd_freelist_mu);
  gpr_mu_unlock(&fd_freelist_mu);
  while (fd_freelist != NULL) {
    grpc_fd *fd = fd_freelist;
    fd_freelist = fd_freelist->freelist_next;
    gpr_mu_destroy(&fd->po.mu);
    gpr_free(fd);
  }
  gpr_mu_destroy(&fd_freelist_mu);
}

static grpc_fd *fd_create(int fd, const char *name) {
  grpc_fd *new_fd = NULL;

  gpr_mu_lock(&fd_freelist_mu);
  if (fd_freelist != NULL) {
    new_fd = fd_freelist;
    fd_freelist = fd_freelist->freelist_next;
  }
  gpr_mu_unlock(&fd_freelist_mu);

  if (new_fd == NULL) {
    new_fd = gpr_malloc(sizeof(grpc_fd));
  }

  pss_obj_init(&new_fd->po);

  gpr_atm_rel_store(&new_fd->refst, (gpr_atm)1);
  new_fd->fd = fd;
  new_fd->orphaned = false;
  grpc_lfev_init(&new_fd->read_closure);
  grpc_lfev_init(&new_fd->write_closure);
  gpr_atm_no_barrier_store(&new_fd->read_notifier_pollset, (gpr_atm)NULL);

  GRPC_LOG_IF_ERROR("fd_create",
                    grpc_wakeup_fd_init(&new_fd->workqueue_wakeup_fd));
  new_fd->workqueue_scheduler.vtable = &workqueue_scheduler_vtable;
  new_fd->workqueue_read_mu = GPR_SPINLOCK_INITIALIZER;
  gpr_mpscq_init(&new_fd->workqueue_items);
  gpr_atm_no_barrier_store(&new_fd->workqueue_item_count, 0);

  new_fd->freelist_next = NULL;
  new_fd->on_done_closure = NULL;

  char *fd_name;
  gpr_asprintf(&fd_name, "%s fd=%d", name, fd);
  grpc_iomgr_register_object(&new_fd->iomgr_object, fd_name);
#ifdef GRPC_FD_REF_COUNT_DEBUG
  gpr_log(GPR_DEBUG, "FD %d %p create %s", fd, (void *)new_fd, fd_name);
#endif
  gpr_free(fd_name);
  return new_fd;
}

static int fd_wrapped_fd(grpc_fd *fd) {
  int ret_fd = -1;
  gpr_mu_lock(&fd->po.mu);
  if (!fd->orphaned) {
    ret_fd = fd->fd;
  }
  gpr_mu_unlock(&fd->po.mu);

  return ret_fd;
}

static void fd_orphan(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                      grpc_closure *on_done, int *release_fd,
                      const char *reason) {
  bool is_fd_closed = false;
  grpc_error *error = GRPC_ERROR_NONE;

  gpr_mu_lock(&fd->po.mu);
  fd->on_done_closure = on_done;

  /* If release_fd is not NULL, we should be relinquishing control of the file
     descriptor fd->fd (but we still own the grpc_fd structure). */
  if (release_fd != NULL) {
    *release_fd = fd->fd;
  } else {
    close(fd->fd);
    is_fd_closed = true;
  }

  fd->orphaned = true;

  if (!is_fd_closed) {
    gpr_log(GPR_DEBUG, "TODO: handle fd removal?");
  }

  /* Remove the active status but keep referenced. We want this grpc_fd struct
     to be alive (and not added to freelist) until the end of this function */
  REF_BY(fd, 1, reason);

  grpc_closure_sched(exec_ctx, fd->on_done_closure, GRPC_ERROR_REF(error));

  gpr_mu_unlock(&fd->po.mu);
  UNREF_BY(fd, 2, reason); /* Drop the reference */
  GRPC_LOG_IF_ERROR("fd_orphan", GRPC_ERROR_REF(error));
  GRPC_ERROR_UNREF(error);
}

static grpc_pollset *fd_get_read_notifier_pollset(grpc_exec_ctx *exec_ctx,
                                                  grpc_fd *fd) {
  gpr_atm notifier = gpr_atm_acq_load(&fd->read_notifier_pollset);
  return (grpc_pollset *)notifier;
}

static bool fd_is_shutdown(grpc_fd *fd) {
  return grpc_lfev_is_shutdown(&fd->read_closure);
}

/* Might be called multiple times */
static void fd_shutdown(grpc_exec_ctx *exec_ctx, grpc_fd *fd, grpc_error *why) {
  if (grpc_lfev_set_shutdown(exec_ctx, &fd->read_closure,
                             GRPC_ERROR_REF(why))) {
    shutdown(fd->fd, SHUT_RDWR);
    grpc_lfev_set_shutdown(exec_ctx, &fd->write_closure, GRPC_ERROR_REF(why));
  }
  GRPC_ERROR_UNREF(why);
}

static void fd_notify_on_read(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                              grpc_closure *closure) {
  grpc_lfev_notify_on(exec_ctx, &fd->read_closure, closure);
}

static void fd_notify_on_write(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                               grpc_closure *closure) {
  grpc_lfev_notify_on(exec_ctx, &fd->write_closure, closure);
}

static grpc_workqueue *fd_get_workqueue(grpc_fd *fd) { abort(); }

#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
static grpc_workqueue *workqueue_ref(grpc_workqueue *workqueue,
                                     const char *file, int line,
                                     const char *reason) {
  if (workqueue != NULL) {
    ref_by((grpc_fd *)workqueue, 2, file, line, reason);
  }
  return workqueue;
}

static void workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue,
                            const char *file, int line, const char *reason) {
  if (workqueue != NULL) {
    unref_by((grpc_fd *)workqueue, 2, file, line, reason);
  }
}
#else
static grpc_workqueue *workqueue_ref(grpc_workqueue *workqueue) {
  if (workqueue != NULL) {
    ref_by((grpc_fd *)workqueue, 2);
  }
  return workqueue;
}

static void workqueue_unref(grpc_exec_ctx *exec_ctx,
                            grpc_workqueue *workqueue) {
  if (workqueue != NULL) {
    unref_by((grpc_fd *)workqueue, 2);
  }
}
#endif

static void workqueue_wakeup(grpc_fd *fd) {
  GRPC_LOG_IF_ERROR("workqueue_enqueue",
                    grpc_wakeup_fd_wakeup(&fd->workqueue_wakeup_fd));
}

static void workqueue_enqueue(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                              grpc_error *error) {
  GPR_TIMER_BEGIN("workqueue.enqueue", 0);
  grpc_fd *fd = (grpc_fd *)(((char *)closure->scheduler) -
                            offsetof(grpc_fd, workqueue_scheduler));
  REF_BY(fd, 2, "workqueue_enqueue");
  gpr_atm last = gpr_atm_no_barrier_fetch_add(&fd->workqueue_item_count, 1);
  closure->error_data.error = error;
  gpr_mpscq_push(&fd->workqueue_items, &closure->next_data.atm_next);
  if (last == 0) {
    workqueue_wakeup(fd);
  }
  UNREF_BY(fd, 2, "workqueue_enqueue");
}

static void fd_invoke_workqueue(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  /* handle spurious wakeups */
  if (!gpr_spinlock_trylock(&fd->workqueue_read_mu)) return;
  gpr_mpscq_node *n = gpr_mpscq_pop(&fd->workqueue_items);
  gpr_spinlock_unlock(&fd->workqueue_read_mu);
  if (n != NULL) {
    if (gpr_atm_full_fetch_add(&fd->workqueue_item_count, -1) > 1) {
      workqueue_wakeup(fd);
    }
    grpc_closure *c = (grpc_closure *)n;
    grpc_error *error = c->error_data.error;
    c->cb(exec_ctx, c->cb_arg, error);
    GRPC_ERROR_UNREF(error);
  } else if (gpr_atm_no_barrier_load(&fd->workqueue_item_count) > 0) {
    /* n == NULL might mean there's work but it's not available to be popped
     * yet - try to ensure another workqueue wakes up to check shortly if so
     */
    workqueue_wakeup(fd);
  }
}

static grpc_closure_scheduler *workqueue_scheduler(grpc_workqueue *workqueue) {
  return &((grpc_fd *)workqueue)->workqueue_scheduler;
}

/*******************************************************************************
 * Pollset Definitions
 */
GPR_TLS_DECL(g_current_thread_pollset);
GPR_TLS_DECL(g_current_thread_worker);

/* Global state management */
static grpc_error *pollset_global_init(void) {
  gpr_tls_init(&g_current_thread_pollset);
  gpr_tls_init(&g_current_thread_worker);
  return grpc_wakeup_fd_init(&global_wakeup_fd);
}

static void pollset_global_shutdown(void) {
  grpc_wakeup_fd_destroy(&global_wakeup_fd);
  gpr_tls_destroy(&g_current_thread_pollset);
  gpr_tls_destroy(&g_current_thread_worker);
}

/* p->po.mu must be held before calling this function */
static grpc_error *pollset_kick(grpc_pollset *p,
                                grpc_pollset_worker *specific_worker) {
  if (specific_worker == NULL) {
    if (gpr_tls_get(&g_current_thread_pollset) != (intptr_t)p) {
      return grpc_wakeup_fd_wakeup(&p->pollset_wakeup);
    } else {
      return GRPC_ERROR_NONE;
    }
  } else if (gpr_tls_get(&g_current_thread_worker) ==
             (intptr_t)specific_worker) {
    return GRPC_ERROR_NONE;
  } else if (specific_worker == p->root_worker) {
    return grpc_wakeup_fd_wakeup(&p->pollset_wakeup);
  } else {
    gpr_cv_signal(&specific_worker->cv);
    return GRPC_ERROR_NONE;
  }
}

static grpc_error *kick_poller(void) {
  return grpc_wakeup_fd_wakeup(&global_wakeup_fd);
}

static void pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  pss_obj_init(&pollset->po);
  pollset->epfd = epoll_create1(EPOLL_CLOEXEC);
  if (pollset->epfd < 0) {
    GRPC_LOG_IF_ERROR("pollset_init", GRPC_OS_ERROR(errno, "epoll_create1"));
  } else {
    struct epoll_event ev = {.events = EPOLLIN | EPOLLET | EPOLLEXCLUSIVE,
                             .data.ptr = &global_wakeup_fd};
    if (epoll_ctl(pollset->epfd, EPOLL_CTL_ADD, global_wakeup_fd.read_fd,
                  &ev) != 0) {
      GRPC_LOG_IF_ERROR("pollset_init", GRPC_OS_ERROR(errno, "epoll_ctl"));
    }
  }
  pollset->num_pollers = 0;
  gpr_atm_no_barrier_store(&pollset->shutdown_atm, 0);
  pollset->shutdown_closure = NULL;
  if (GRPC_LOG_IF_ERROR("pollset_init",
                        grpc_wakeup_fd_init(&pollset->pollset_wakeup)) &&
      pollset->epfd >= 0) {
    struct epoll_event ev = {.events = EPOLLIN | EPOLLET,
                             .data.ptr = &pollset->pollset_wakeup};
    if (epoll_ctl(pollset->epfd, EPOLL_CTL_ADD, pollset->pollset_wakeup.read_fd,
                  &ev) != 0) {
      GRPC_LOG_IF_ERROR("pollset_init", GRPC_OS_ERROR(errno, "epoll_ctl"));
    }
  }
  pollset->root_worker = NULL;
  *mu = &pollset->po.mu;
}

/* Convert a timespec to milliseconds:
   - Very small or negative poll times are clamped to zero to do a non-blocking
     poll (which becomes spin polling)
   - Other small values are rounded up to one millisecond
   - Longer than a millisecond polls are rounded up to the next nearest
     millisecond to avoid spinning
   - Infinite timeouts are converted to -1 */
static int poll_deadline_to_millis_timeout(gpr_timespec deadline,
                                           gpr_timespec now) {
  gpr_timespec timeout;
  static const int64_t max_spin_polling_us = 10;
  if (gpr_time_cmp(deadline, gpr_inf_future(deadline.clock_type)) == 0) {
    return -1;
  }

  if (gpr_time_cmp(deadline, gpr_time_add(now, gpr_time_from_micros(
                                                   max_spin_polling_us,
                                                   GPR_TIMESPAN))) <= 0) {
    return 0;
  }
  timeout = gpr_time_sub(deadline, now);
  int millis = gpr_time_to_millis(gpr_time_add(
      timeout, gpr_time_from_nanos(GPR_NS_PER_MS - 1, GPR_TIMESPAN)));
  return millis >= 1 ? millis : 1;
}

static void fd_become_readable(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                               grpc_pollset *notifier) {
  grpc_lfev_set_ready(exec_ctx, &fd->read_closure);

  /* Note, it is possible that fd_become_readable might be called twice with
     different 'notifier's when an fd becomes readable and it is in two epoll
     sets (This can happen briefly during polling island merges). In such cases
     it does not really matter which notifer is set as the read_notifier_pollset
     (They would both point to the same polling island anyway) */
  /* Use release store to match with acquire load in fd_get_read_notifier */
  gpr_atm_rel_store(&fd->read_notifier_pollset, (gpr_atm)notifier);
}

static void fd_become_writable(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  grpc_lfev_set_ready(exec_ctx, &fd->write_closure);
}

/* pollset->po.mu lock must be held by the caller before calling this */
static void pollset_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                             grpc_closure *closure) {
  GPR_ASSERT(pollset->shutdown_closure == NULL);
  pollset->shutdown_closure = closure;
  if (pollset->num_pollers > 0) {
    struct epoll_event ev = {.events = EPOLLIN,
                             .data.ptr = &pollset->pollset_wakeup};
    epoll_ctl(pollset->epfd, EPOLL_CTL_MOD, pollset->pollset_wakeup.read_fd,
              &ev);
    GRPC_LOG_IF_ERROR("pollset_shutdown",
                      grpc_wakeup_fd_wakeup(&pollset->pollset_wakeup));
  } else {
    grpc_closure_sched(exec_ctx, closure, GRPC_ERROR_NONE);
  }
}

/* pollset_shutdown is guaranteed to be called before pollset_destroy. */
static void pollset_destroy(grpc_pollset *pollset) {
  gpr_mu_destroy(&pollset->po.mu);
  if (pollset->epfd >= 0) close(pollset->epfd);
  grpc_wakeup_fd_destroy(&pollset->pollset_wakeup);
}

#define MAX_EPOLL_EVENTS 100

static grpc_error *pollset_poll(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                                gpr_timespec now, gpr_timespec deadline) {
  struct epoll_event events[MAX_EPOLL_EVENTS];
  static const char *err_desc = "pollset_poll";

  if (pollset->epfd < 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "epoll fd failed to initialize");
  }

  GRPC_SCHEDULING_START_BLOCKING_REGION;
  int r = epoll_wait(pollset->epfd, events, MAX_EPOLL_EVENTS,
                     poll_deadline_to_millis_timeout(deadline, now));
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  if (r < 0) return GRPC_OS_ERROR(errno, "epoll_wait");

  grpc_error *error = GRPC_ERROR_NONE;
  for (int i = 0; i < r; i++) {
    void *data_ptr = events[i].data.ptr;
    if (data_ptr == &global_wakeup_fd) {
      grpc_timer_consume_kick();
      append_error(&error, grpc_wakeup_fd_consume_wakeup(&global_wakeup_fd),
                   err_desc);
    } else if (data_ptr == &pollset->pollset_wakeup) {
      /* once we start shutting down we stop consuming the wakeup:
         the fd is level triggered and non-exclusive, which should result in all
         pollers waking */
      if (gpr_atm_no_barrier_load(&pollset->shutdown_atm) == 0) {
        append_error(&error, grpc_wakeup_fd_consume_wakeup(&global_wakeup_fd),
                     err_desc);
      }
    } else {
      grpc_fd *fd = (grpc_fd *)(((intptr_t)data_ptr) & ~(intptr_t)1);
      bool is_workqueue = (((intptr_t)data_ptr) & 1) != 0;
      bool cancel = (events[i].events & (EPOLLERR | EPOLLHUP)) != 0;
      bool read_ev = (events[i].events & (EPOLLIN | EPOLLPRI)) != 0;
      bool write_ev = (events[i].events & EPOLLOUT) != 0;
      if (is_workqueue) {
        append_error(&error,
                     grpc_wakeup_fd_consume_wakeup(&fd->workqueue_wakeup_fd),
                     err_desc);
        fd_invoke_workqueue(exec_ctx, fd);
      } else {
        if (read_ev || cancel) {
          fd_become_readable(exec_ctx, fd, pollset);
        }
        if (write_ev || cancel) {
          fd_become_writable(exec_ctx, fd);
        }
      }
    }
  }

  return error;
}

/* Return true if this thread should poll */
static bool begin_worker(grpc_pollset *pollset, grpc_pollset_worker *worker,
                         grpc_pollset_worker **worker_hdl,
                         gpr_timespec deadline) {
  if (worker_hdl != NULL) {
    *worker_hdl = worker;
    worker->kicked = false;
    if (pollset->root_worker == NULL) {
      pollset->root_worker = worker;
      worker->next = worker->prev = worker;
      worker->initialized_cv = false;
    } else {
      worker->next = pollset->root_worker;
      worker->prev = worker->next->prev;
      worker->next->prev = worker->prev->next = worker;
      worker->initialized_cv = true;
      gpr_cv_init(&worker->cv);
      while (pollset->root_worker != worker) {
        if (gpr_cv_wait(&worker->cv, &pollset->po.mu, deadline)) return false;
        if (worker->kicked) return false;
      }
    }
  }
  return pollset->shutdown_closure == NULL;
}

static void end_worker(grpc_pollset *pollset, grpc_pollset_worker *worker,
                       grpc_pollset_worker **worker_hdl) {
  if (worker_hdl != NULL) {
    if (worker == pollset->root_worker) {
      if (worker == worker->next) {
        pollset->root_worker = NULL;
      } else {
        pollset->root_worker = worker->next;
        worker->prev->next = worker->next;
        worker->next->prev = worker->prev;
      }
    } else {
      worker->prev->next = worker->next;
      worker->next->prev = worker->prev;
    }
    if (worker->initialized_cv) {
      gpr_cv_destroy(&worker->cv);
    }
  }
}

/* pollset->po.mu lock must be held by the caller before calling this.
   The function pollset_work() may temporarily release the lock (pollset->po.mu)
   during the course of its execution but it will always re-acquire the lock and
   ensure that it is held by the time the function returns */
static grpc_error *pollset_work(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                                grpc_pollset_worker **worker_hdl,
                                gpr_timespec now, gpr_timespec deadline) {
  grpc_pollset_worker worker;
  grpc_error *error = GRPC_ERROR_NONE;
  if (begin_worker(pollset, &worker, worker_hdl, deadline)) {
    GPR_ASSERT(!pollset->shutdown_closure);
    pollset->num_pollers++;
    gpr_mu_unlock(&pollset->po.mu);
    error = pollset_poll(exec_ctx, pollset, now, deadline);
    grpc_exec_ctx_flush(exec_ctx);
    gpr_mu_lock(&pollset->po.mu);
    pollset->num_pollers--;
    if (pollset->num_pollers == 0 && pollset->shutdown_closure != NULL) {
      grpc_closure_sched(exec_ctx, pollset->shutdown_closure, GRPC_ERROR_NONE);
    }
  }
  end_worker(pollset, &worker, worker_hdl);
  return error;
}

static void pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_fd *fd) {
  grpc_error *error = GRPC_ERROR_NONE;
  static const char *err_desc = "pollset_add_fd";
  struct epoll_event ev_fd = {
      .events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLEXCLUSIVE, .data.ptr = fd};
  if (epoll_ctl(pollset->epfd, EPOLL_CTL_ADD, fd->fd, &ev_fd) != 0) {
    switch (errno) {
      case EEXIST: /* if this fd is already in the epoll set, the workqueue fd
                      must also be - just return */
        return;
      default:
        append_error(&error, GRPC_OS_ERROR(errno, "epoll_ctl"), err_desc);
    }
  }
  struct epoll_event ev_wq = {.events = EPOLLET | EPOLLIN | EPOLLEXCLUSIVE,
                              .data.ptr = fd};
  if (epoll_ctl(pollset->epfd, EPOLL_CTL_ADD, fd->workqueue_wakeup_fd.read_fd,
                &ev_wq) != 0) {
    switch (errno) {
      case EEXIST: /* if the workqueue fd is already in the epoll set we're ok -
                      no need to do anything special */
        break;
      default:
        append_error(&error, GRPC_OS_ERROR(errno, "epoll_ctl"), err_desc);
    }
  }
  GRPC_LOG_IF_ERROR("pollset_add_fd", error);
}

/*******************************************************************************
 * Pollset-set Definitions
 */

static grpc_pollset_set *pollset_set_create(void) {
  grpc_pollset_set *pss = gpr_zalloc(sizeof(*pss));
  pss_obj_init(&pss->po);
  gpr_ref_init(&pss->refs, 1);
  pss->roots[PSS_POLLSET_SET] = &pss->po;
  pss->po.pss_master = pss;
  pss->po.pss_next = pss->po.pss_prev = &pss->po;
  return pss;
}

static void pss_destroy(grpc_pollset_set *pss) {
  gpr_mu_destroy(&pss->po.mu);
  gpr_free(pss);
}

static grpc_pollset_set *pss_ref(grpc_pollset_set *pss) {
  gpr_ref(&pss->refs);
  return pss;
}

static void pss_unref(grpc_pollset_set *pss) {
  if (gpr_unref(&pss->refs)) pss_destroy(pss);
}

static void pollset_set_destroy(grpc_exec_ctx *exec_ctx,
                                grpc_pollset_set *pss) {
  pss_unref(pss);
}

static grpc_pollset_set *pss_ref_and_lock_master(
    grpc_pollset_set *master_or_slave) {
  pss_ref(master_or_slave);
  gpr_mu_lock(&master_or_slave->po.mu);
  while (master_or_slave != master_or_slave->po.pss_master) {
    grpc_pollset_set *master = pss_ref(master_or_slave->po.pss_master);
    gpr_mu_unlock(&master_or_slave->po.mu);
    pss_unref(master_or_slave);
    master_or_slave = master;
    gpr_mu_lock(&master_or_slave->po.mu);
  }
  return master_or_slave;
}

static void pss_broadcast_fd(grpc_exec_ctx *exec_ctx, grpc_pollset_set *dst,
                             pss_obj *obj) {
  grpc_fd *fd = (grpc_fd *)obj;
  if (dst->roots[PSS_POLLSET] == NULL) return;
  pss_obj *tgt = dst->roots[PSS_POLLSET];
  do {
    pollset_add_fd(exec_ctx, (grpc_pollset *)tgt, fd);
    tgt = tgt->pss_next;
  } while (tgt != dst->roots[PSS_POLLSET]);
}

static void pss_broadcast_pollset(grpc_exec_ctx *exec_ctx,
                                  grpc_pollset_set *dst, pss_obj *obj) {
  grpc_pollset *pollset = (grpc_pollset *)obj;
  if (dst->roots[PSS_FD] == NULL) return;
  pss_obj *tgt = dst->roots[PSS_FD];
  do {
    pollset_add_fd(exec_ctx, pollset, (grpc_fd *)tgt);
    tgt = tgt->pss_next;
  } while (tgt != dst->roots[PSS_FD]);
}

static pss_obj *pss_splice(pss_obj *p, pss_obj *q) {
  if (p == NULL) return q;
  if (q == NULL) return p;
  p->pss_next->pss_prev = q->pss_prev;
  q->pss_prev->pss_next = p->pss_next;
  p->pss_next = q;
  q->pss_prev = p;
  return p;
}

static void (*const broadcast[PSS_OBJ_TYPE_COUNT])(grpc_exec_ctx *exec_ctx,
                                                   grpc_pollset_set *dst,
                                                   pss_obj *obj) = {
    pss_broadcast_fd, pss_broadcast_pollset, NULL};

static void pss_merge_broadcast_and_patch(grpc_exec_ctx *exec_ctx,
                                          grpc_pollset_set *a,
                                          grpc_pollset_set *b,
                                          pss_obj_type type) {
  pss_obj *obj;
  if (a->roots[type] != NULL) {
    obj = a->roots[PSS_FD];
    do {
      broadcast[type](exec_ctx, b, obj);
      obj = obj->pss_next;
    } while (obj != a->roots[PSS_FD]);
  }
  if (b->roots[type] != NULL) {
    obj = b->roots[PSS_FD];
    do {
      broadcast[type](exec_ctx, a, obj);
      gpr_mu_lock(&obj->mu);
      obj->pss_master = a;
      gpr_mu_unlock(&obj->mu);
      obj = obj->pss_next;
    } while (obj != b->roots[PSS_FD]);
  }
  a->roots[type] = pss_splice(a->roots[type], b->roots[type]);
}

static void pss_merge(grpc_exec_ctx *exec_ctx, grpc_pollset_set *a,
                      grpc_pollset_set *b) {
  pss_ref(a);
  pss_ref(b);
  for (;;) {
    if (a == b) {
      pss_unref(a);
      pss_unref(b);
      return;
    } else if (a < b) {
      gpr_mu_lock(&a->po.mu);
      gpr_mu_lock(&b->po.mu);
    } else {
      gpr_mu_lock(&b->po.mu);
      gpr_mu_lock(&a->po.mu);
    }
    if (a != a->po.pss_master) {
      grpc_pollset_set *master = pss_ref(a->po.pss_master);
      gpr_mu_unlock(&a->po.mu);
      gpr_mu_unlock(&b->po.mu);
      pss_unref(a);
      a = master;
    } else if (b != b->po.pss_master) {
      grpc_pollset_set *master = pss_ref(b->po.pss_master);
      gpr_mu_unlock(&a->po.mu);
      gpr_mu_unlock(&b->po.mu);
      pss_unref(b);
      b = master;
    } else {
      /* a, b locked and are at their respective masters */
      pss_merge_broadcast_and_patch(exec_ctx, a, b, PSS_FD);
      pss_merge_broadcast_and_patch(exec_ctx, a, b, PSS_POLLSET);
      b->po.pss_master = a;
      gpr_mu_unlock(&a->po.mu);
      gpr_mu_unlock(&b->po.mu);
      return;
    }
  }
}

static void pss_add_obj(grpc_exec_ctx *exec_ctx, grpc_pollset_set *pss,
                        pss_obj *obj, pss_obj_type type) {
  pss = pss_ref_and_lock_master(pss);
  gpr_mu_lock(&obj->mu);
  if (obj->pss_master == pss) {
    /* obj is already a member -- just bump refcount */
    obj->pss_refs++;
    gpr_mu_unlock(&obj->mu);
    gpr_mu_unlock(&pss->po.mu);
    pss_unref(pss);
    return;
  } else if (obj->pss_master != NULL) {
    grpc_pollset_set *other_pss = pss_ref(obj->pss_master);
    obj->pss_refs++;
    gpr_mu_unlock(&obj->mu);
    gpr_mu_unlock(&pss->po.mu);
    pss_merge(exec_ctx, pss, other_pss);
    pss_unref(other_pss);
    pss_unref(pss);
  } else {
    GPR_ASSERT(obj->pss_refs == 0);
    obj->pss_refs = 1;
    obj->pss_master = pss;
    if (pss->roots[type] == NULL) {
      pss->roots[type] = obj;
      obj->pss_next = obj->pss_prev = obj;
    } else {
      obj->pss_next = pss->roots[type];
      obj->pss_prev = obj->pss_next->pss_prev;
      obj->pss_prev->pss_next = obj;
      obj->pss_next->pss_prev = obj;
    }
    gpr_mu_unlock(&obj->mu);
    switch (type) {
      case PSS_FD:
        pss_broadcast_fd(exec_ctx, pss, obj);
        break;
      case PSS_POLLSET:
        pss_broadcast_pollset(exec_ctx, pss, obj);
        break;
      case PSS_POLLSET_SET:
      case PSS_OBJ_TYPE_COUNT:
        GPR_UNREACHABLE_CODE(break);
    }
    gpr_mu_unlock(&pss->po.mu);
    pss_unref(pss);
  }
}

static void pss_del_obj(grpc_exec_ctx *exec_ctx, grpc_pollset_set *pss,
                        pss_obj *obj, pss_obj_type type) {
  pss = pss_ref_and_lock_master(pss);
  gpr_mu_lock(&obj->mu);
  obj->pss_refs--;
  if (obj->pss_refs == 0) {
    obj->pss_master = NULL;
    if (obj == pss->roots[type]) {
      pss->roots[type] = obj->pss_next;
    }
    if (obj->pss_next == obj) {
      pss->roots[type] = NULL;
    } else {
      obj->pss_next->pss_prev = obj->pss_prev;
      obj->pss_prev->pss_next = obj->pss_next;
    }
  }
  gpr_mu_unlock(&obj->mu);
  gpr_mu_unlock(&pss->po.mu);
  pss_unref(pss);
}

static void pollset_set_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset_set *pss,
                               grpc_fd *fd) {
  pss_add_obj(exec_ctx, pss, &fd->po, PSS_FD);
}

static void pollset_set_del_fd(grpc_exec_ctx *exec_ctx, grpc_pollset_set *pss,
                               grpc_fd *fd) {
  pss_del_obj(exec_ctx, pss, &fd->po, PSS_FD);
}

static void pollset_set_add_pollset(grpc_exec_ctx *exec_ctx,
                                    grpc_pollset_set *pss, grpc_pollset *ps) {
  pss_add_obj(exec_ctx, pss, &ps->po, PSS_POLLSET);
}

static void pollset_set_del_pollset(grpc_exec_ctx *exec_ctx,
                                    grpc_pollset_set *pss, grpc_pollset *ps) {
  pss_del_obj(exec_ctx, pss, &ps->po, PSS_POLLSET);
}

static void pollset_set_add_pollset_set(grpc_exec_ctx *exec_ctx,
                                        grpc_pollset_set *bag,
                                        grpc_pollset_set *item) {
  pss_add_obj(exec_ctx, bag, &item->po, PSS_POLLSET_SET);
}

static void pollset_set_del_pollset_set(grpc_exec_ctx *exec_ctx,
                                        grpc_pollset_set *bag,
                                        grpc_pollset_set *item) {
  pss_del_obj(exec_ctx, bag, &item->po, PSS_POLLSET_SET);
}

/*******************************************************************************
 * Event engine binding
 */

static void shutdown_engine(void) {
  fd_global_shutdown();
  pollset_global_shutdown();
}

static const grpc_event_engine_vtable vtable = {
    .pollset_size = sizeof(grpc_pollset),

    .fd_create = fd_create,
    .fd_wrapped_fd = fd_wrapped_fd,
    .fd_orphan = fd_orphan,
    .fd_shutdown = fd_shutdown,
    .fd_is_shutdown = fd_is_shutdown,
    .fd_notify_on_read = fd_notify_on_read,
    .fd_notify_on_write = fd_notify_on_write,
    .fd_get_read_notifier_pollset = fd_get_read_notifier_pollset,
    .fd_get_workqueue = fd_get_workqueue,

    .pollset_init = pollset_init,
    .pollset_shutdown = pollset_shutdown,
    .pollset_destroy = pollset_destroy,
    .pollset_work = pollset_work,
    .pollset_kick = pollset_kick,
    .pollset_add_fd = pollset_add_fd,

    .pollset_set_create = pollset_set_create,
    .pollset_set_destroy = pollset_set_destroy,
    .pollset_set_add_pollset = pollset_set_add_pollset,
    .pollset_set_del_pollset = pollset_set_del_pollset,
    .pollset_set_add_pollset_set = pollset_set_add_pollset_set,
    .pollset_set_del_pollset_set = pollset_set_del_pollset_set,
    .pollset_set_add_fd = pollset_set_add_fd,
    .pollset_set_del_fd = pollset_set_del_fd,

    .kick_poller = kick_poller,

    .workqueue_ref = workqueue_ref,
    .workqueue_unref = workqueue_unref,
    .workqueue_scheduler = workqueue_scheduler,

    .shutdown_engine = shutdown_engine,
};

/* It is possible that GLIBC has epoll but the underlying kernel doesn't.
 * Create a dummy epoll_fd to make sure epoll support is available */
static bool is_epollex_available(void) {
  int fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd < 0) {
    gpr_log(GPR_ERROR,
            "epoll_create1 failed with error: %d. Not using epollex polling "
            "engine.",
            fd);
    return false;
  }
  grpc_wakeup_fd wakeup;
  if (!GRPC_LOG_IF_ERROR("check_wakeupfd_for_epollex",
                         grpc_wakeup_fd_init(&wakeup))) {
    return false;
  }
  struct epoll_event ev = {
      /* choose events that should cause an error on
         EPOLLEXCLUSIVE enabled kernels - specifically the combination of
         EPOLLONESHOT and EPOLLEXCLUSIVE */
      .events = EPOLLET | EPOLLIN | EPOLLEXCLUSIVE | EPOLLONESHOT,
      .data.ptr = NULL};
  if (epoll_ctl(fd, EPOLL_CTL_ADD, wakeup.read_fd, &ev) != 0) {
    if (errno != EINVAL) {
      gpr_log(GPR_ERROR,
              "epoll_ctl with EPOLLEXCLUSIVE | EPOLLONESHOT failed with error: "
              "%d. Not using epollex polling engine.",
              errno);
      close(fd);
      grpc_wakeup_fd_destroy(&wakeup);
      return false;
    }
  } else {
    gpr_log(GPR_ERROR,
            "epoll_ctl with EPOLLEXCLUSIVE | EPOLLONESHOT succeeded. This is "
            "evidence of no EPOLLEXCLUSIVE support. Not using "
            "epollex polling engine.");
    close(fd);
    grpc_wakeup_fd_destroy(&wakeup);
    return false;
  }
  grpc_wakeup_fd_destroy(&wakeup);
  close(fd);
  return true;
}

const grpc_event_engine_vtable *grpc_init_epollex_linux(void) {
  if (!grpc_has_wakeup_fd()) {
    return NULL;
  }

  if (!is_epollex_available()) {
    return NULL;
  }

  fd_global_init();

  if (!GRPC_LOG_IF_ERROR("pollset_global_init", pollset_global_init())) {
    return NULL;
  }

  return &vtable;
}

#else /* defined(GRPC_LINUX_EPOLL) */
#if defined(GRPC_POSIX_SOCKET)
#include "src/core/lib/iomgr/ev_posix.h"
/* If GRPC_LINUX_EPOLL is not defined, it means epoll is not available. Return
 * NULL */
const grpc_event_engine_vtable *grpc_init_epollex_linux(void) { return NULL; }
#endif /* defined(GRPC_POSIX_SOCKET) */

#endif /* !defined(GRPC_LINUX_EPOLL) */
