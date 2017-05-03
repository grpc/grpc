/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/lib/iomgr/ev_epoll_thread_pool_linux.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/thd.h>
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

/* TODO: sreek - Move this to init.c and initialize this like other tracers. */
static int grpc_polling_trace = 0; /* Disabled by default */
#define GRPC_POLLING_TRACE(fmt, ...)       \
  if (grpc_polling_trace) {                \
    gpr_log(GPR_INFO, (fmt), __VA_ARGS__); \
  }

/* TODO: sreek: Right now, this wakes up all pollers. In future we should make
 * sure to wake up one polling thread (which can wake up other threads if
 * needed) */
static grpc_wakeup_fd global_wakeup_fd;

struct polling_island;

/*******************************************************************************
 * Fd Declarations
 */
struct grpc_fd {
  gpr_mu mu;
  struct polling_island *pi;

  int fd;
  /* refst format:
       bit 0    : 1=Active / 0=Orphaned
       bits 1-n : refcount
     Ref/Unref by two to avoid altering the orphaned bit */
  gpr_atm refst;

  /* The fd is either closed or we relinquished control of it. In either
     cases, this indicates that the 'fd' on this structure is no longer
     valid */
  bool orphaned;

  gpr_atm read_closure;
  gpr_atm write_closure;

  struct grpc_fd *freelist_next;
  grpc_closure *on_done_closure;

  grpc_iomgr_object iomgr_object;
};

static void fd_global_init(void);
static void fd_global_shutdown(void);

/*******************************************************************************
 * Polling island Declarations
 */

#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG

#define PI_ADD_REF(p, r) pi_add_ref_dbg((p), (r), __FILE__, __LINE__)
#define PI_UNREF(exec_ctx, p, r) \
  pi_unref_dbg((exec_ctx), (p), (r), __FILE__, __LINE__)

#else /* defined(GRPC_WORKQUEUE_REFCOUNT_DEBUG) */

#define PI_ADD_REF(p, r) pi_add_ref((p))
#define PI_UNREF(exec_ctx, p, r) pi_unref((exec_ctx), (p))

#endif /* !defined(GRPC_PI_REF_COUNT_DEBUG) */

/* This is also used as grpc_workqueue (by directly casting it) */
typedef struct polling_island {
  grpc_closure_scheduler workqueue_scheduler;

  /* Ref count. Use PI_ADD_REF() and PI_UNREF() macros to increment/decrement
     the refcount. Once the ref count becomes zero, this structure is destroyed
     which means we should ensure that there is never a scenario where a
     PI_ADD_REF() is racing with a PI_UNREF() that just made the ref_count
     zero. */
  gpr_atm ref_count;

  /* Number of threads currently polling on this island */
  gpr_atm poller_count;
  /* Mutex guarding the read end of the workqueue (must be held to pop from
   * workqueue_items) */
  gpr_mu workqueue_read_mu;
  /* Queue of closures to be executed */
  gpr_mpscq workqueue_items;
  /* Count of items in workqueue_items */
  gpr_atm workqueue_item_count;
  /* Wakeup fd used to wake pollers to check the contents of workqueue_items */
  grpc_wakeup_fd workqueue_wakeup_fd;

  /* Is the polling island shutdown */
  gpr_atm is_shutdown;

  /* The fd of the underlying epoll set */
  int epoll_fd;
} polling_island;

/*******************************************************************************
 * Pollset Declarations
 */
struct grpc_pollset_worker {
  gpr_cv kick_cv;

  struct grpc_pollset_worker *next;
  struct grpc_pollset_worker *prev;
};

struct grpc_pollset {
  gpr_mu mu;
  struct polling_island *pi;

  grpc_pollset_worker root_worker;
  bool kicked_without_pollers;

  bool shutting_down;          /* Is the pollset shutting down ? */
  bool finish_shutdown_called; /* Is the 'finish_shutdown_locked()' called ? */
  grpc_closure *shutdown_done; /* Called after after shutdown is complete */
};

/*******************************************************************************
 * Pollset-set Declarations
 */
struct grpc_pollset_set {
  void *no_op;
};

/*****************************************************************************
 * Dedicated polling threads and pollsets - Declarations
 */

size_t g_num_pi = 1;
struct polling_island **g_polling_islands = NULL;
size_t g_num_threads_per_pi = 1;
gpr_thd_id *g_poller_threads = NULL;

/* Used as read-notifier pollsets for fds. We won't be using read notifier
 * pollsets with this polling engine. So it does not matter what pollset we
 * return */
grpc_pollset g_read_notifier;

static void add_fd_to_dedicated_pi(grpc_fd *fd);
static bool init_dedicated_polling_islands();
static void shutdown_dedicated_polling_islands();
static void poller_thread_loop(void *arg);
static void start_dedicated_poller_threads();
static void shutdown_dedicated_poller_threads();

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
 * Polling island Definitions
 */

/* The wakeup fd that is used to wake up all threads in a Polling island. This
   is useful in the polling island merge operation where we need to wakeup all
   the threads currently polling the smaller polling island (so that they can
   start polling the new/merged polling island)

   NOTE: This fd is initialized to be readable and MUST NOT be consumed i.e the
   threads that woke up MUST NOT call grpc_wakeup_fd_consume_wakeup() */
static grpc_wakeup_fd polling_island_wakeup_fd;

/* The polling island being polled right now.
   See comments in workqueue_maybe_wakeup for why this is tracked. */
static __thread polling_island *g_current_thread_polling_island;

/* Forward declaration */
static void polling_island_delete(polling_island *pi);
static void workqueue_enqueue(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                              grpc_error *error);

#ifdef GRPC_TSAN
/* Currently TSAN may incorrectly flag data races between epoll_ctl and
   epoll_wait for any grpc_fd structs that are added to the epoll set via
   epoll_ctl and are returned (within a very short window) via epoll_wait().

   To work-around this race, we establish a happens-before relation between
   the code just-before epoll_ctl() and the code after epoll_wait() by using
   this atomic */
gpr_atm g_epoll_sync;
#endif /* defined(GRPC_TSAN) */

static const grpc_closure_scheduler_vtable workqueue_scheduler_vtable = {
    workqueue_enqueue, workqueue_enqueue, "workqueue"};

static void pi_add_ref(polling_island *pi);
static void pi_unref(grpc_exec_ctx *exec_ctx, polling_island *pi);

#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
static void pi_add_ref_dbg(polling_island *pi, const char *reason,
                           const char *file, int line) {
  long old_cnt = gpr_atm_acq_load(&pi->ref_count);
  pi_add_ref(pi);
  gpr_log(GPR_DEBUG, "Add ref pi: %p, old: %ld -> new:%ld (%s) - (%s, %d)",
          (void *)pi, old_cnt, old_cnt + 1, reason, file, line);
}

static void pi_unref_dbg(grpc_exec_ctx *exec_ctx, polling_island *pi,
                         const char *reason, const char *file, int line) {
  long old_cnt = gpr_atm_acq_load(&pi->ref_count);
  pi_unref(exec_ctx, pi);
  gpr_log(GPR_DEBUG, "Unref pi: %p, old:%ld -> new:%ld (%s) - (%s, %d)",
          (void *)pi, old_cnt, (old_cnt - 1), reason, file, line);
}

static grpc_workqueue *workqueue_ref(grpc_workqueue *workqueue,
                                     const char *file, int line,
                                     const char *reason) {
  if (workqueue != NULL) {
    pi_add_ref_dbg((polling_island *)workqueue, reason, file, line);
  }
  return workqueue;
}

static void workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue,
                            const char *file, int line, const char *reason) {
  if (workqueue != NULL) {
    pi_unref_dbg(exec_ctx, (polling_island *)workqueue, reason, file, line);
  }
}
#else
static grpc_workqueue *workqueue_ref(grpc_workqueue *workqueue) {
  if (workqueue != NULL) {
    pi_add_ref((polling_island *)workqueue);
  }
  return workqueue;
}

static void workqueue_unref(grpc_exec_ctx *exec_ctx,
                            grpc_workqueue *workqueue) {
  if (workqueue != NULL) {
    pi_unref(exec_ctx, (polling_island *)workqueue);
  }
}
#endif

static void pi_add_ref(polling_island *pi) {
  gpr_atm_no_barrier_fetch_add(&pi->ref_count, 1);
}

static void pi_unref(grpc_exec_ctx *exec_ctx, polling_island *pi) {
  /* If ref count went to zero, delete the polling island. This deletion is
     not done under a lock since once the ref count goes to zero, we are
     guaranteed that no one else holds a reference to the polling island (and
     that there is no racing pi_add_ref() call either).*/
  if (1 == gpr_atm_full_fetch_add(&pi->ref_count, -1)) {
    polling_island_delete(pi);
  }
}

static void polling_island_add_fd_locked(polling_island *pi, grpc_fd *fd,
                                         grpc_error **error) {
  int err;
  struct epoll_event ev;
  char *err_msg;
  const char *err_desc = "polling_island_add_fd_locked";

#ifdef GRPC_TSAN
  /* See the definition of g_epoll_sync for more context */
  gpr_atm_rel_store(&g_epoll_sync, (gpr_atm)0);
#endif /* defined(GRPC_TSAN) */

  ev.events = (uint32_t)(EPOLLIN | EPOLLOUT | EPOLLET);
  ev.data.ptr = fd;
  err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_ADD, fd->fd, &ev);
  if (err < 0 && errno != EEXIST) {
    gpr_asprintf(
        &err_msg,
        "epoll_ctl (epoll_fd: %d) add fd: %d failed with error: %d (%s)",
        pi->epoll_fd, fd->fd, errno, strerror(errno));
    append_error(error, GRPC_OS_ERROR(errno, err_msg), err_desc);
    gpr_free(err_msg);
  }
}

static void polling_island_add_wakeup_fd_locked(polling_island *pi,
                                                grpc_wakeup_fd *wakeup_fd,
                                                grpc_error **error) {
  struct epoll_event ev;
  int err;
  char *err_msg;
  const char *err_desc = "polling_island_add_wakeup_fd";

  ev.events = (uint32_t)(EPOLLIN | EPOLLET);
  ev.data.ptr = wakeup_fd;
  err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_ADD,
                  GRPC_WAKEUP_FD_GET_READ_FD(wakeup_fd), &ev);
  if (err < 0 && errno != EEXIST) {
    gpr_asprintf(&err_msg,
                 "epoll_ctl (epoll_fd: %d) add wakeup fd: %d failed with "
                 "error: %d (%s)",
                 pi->epoll_fd, GRPC_WAKEUP_FD_GET_READ_FD(&global_wakeup_fd),
                 errno, strerror(errno));
    append_error(error, GRPC_OS_ERROR(errno, err_msg), err_desc);
    gpr_free(err_msg);
  }
}

static void polling_island_remove_fd(polling_island *pi, grpc_fd *fd,
                                     bool is_fd_closed, grpc_error **error) {
  int err;
  char *err_msg;
  const char *err_desc = "polling_island_remove_fd";

  /* If fd is already closed, then it would have been automatically been removed
     from the epoll set */
  if (!is_fd_closed) {
    err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_DEL, fd->fd, NULL);
    if (err < 0 && errno != ENOENT) {
      gpr_asprintf(
          &err_msg,
          "epoll_ctl (epoll_fd: %d) del fd: %d failed with error: %d (%s)",
          pi->epoll_fd, fd->fd, errno, strerror(errno));
      append_error(error, GRPC_OS_ERROR(errno, err_msg), err_desc);
      gpr_free(err_msg);
    }
  }
}

/* Might return NULL in case of an error */
static polling_island *polling_island_create(grpc_error **error) {
  polling_island *pi = NULL;
  const char *err_desc = "polling_island_create";

  *error = GRPC_ERROR_NONE;

  pi = gpr_malloc(sizeof(*pi));
  pi->workqueue_scheduler.vtable = &workqueue_scheduler_vtable;
  pi->epoll_fd = -1;

  gpr_mu_init(&pi->workqueue_read_mu);
  gpr_mpscq_init(&pi->workqueue_items);
  gpr_atm_rel_store(&pi->workqueue_item_count, 0);

  gpr_atm_rel_store(&pi->ref_count, 0);
  gpr_atm_rel_store(&pi->poller_count, 0);

  gpr_atm_rel_store(&pi->is_shutdown, false);

  if (!append_error(error, grpc_wakeup_fd_init(&pi->workqueue_wakeup_fd),
                    err_desc)) {
    goto done;
  }

  pi->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

  if (pi->epoll_fd < 0) {
    append_error(error, GRPC_OS_ERROR(errno, "epoll_create1"), err_desc);
    goto done;
  }

  polling_island_add_wakeup_fd_locked(pi, &global_wakeup_fd, error);
  polling_island_add_wakeup_fd_locked(pi, &pi->workqueue_wakeup_fd, error);

done:
  if (*error != GRPC_ERROR_NONE) {
    polling_island_delete(pi);
    pi = NULL;
  }
  return pi;
}

static void polling_island_delete(polling_island *pi) {
  if (pi->epoll_fd >= 0) {
    close(pi->epoll_fd);
  }

  GPR_ASSERT(gpr_atm_no_barrier_load(&pi->workqueue_item_count) == 0);
  gpr_mu_destroy(&pi->workqueue_read_mu);
  gpr_mpscq_destroy(&pi->workqueue_items);
  grpc_wakeup_fd_destroy(&pi->workqueue_wakeup_fd);

  gpr_free(pi);
}

static void workqueue_maybe_wakeup(polling_island *pi) {
  /* If this thread is the current poller, then it may be that it's about to
     decrement the current poller count, so we need to look past this thread */
  bool is_current_poller = (g_current_thread_polling_island == pi);
  gpr_atm min_current_pollers_for_wakeup = is_current_poller ? 1 : 0;
  gpr_atm current_pollers = gpr_atm_no_barrier_load(&pi->poller_count);
  /* Only issue a wakeup if it's likely that some poller could come in and take
     it right now. Note that since we do an anticipatory mpscq_pop every poll
     loop, it's ok if we miss the wakeup here, as we'll get the work item when
     the next poller enters anyway. */
  if (current_pollers > min_current_pollers_for_wakeup) {
    GRPC_LOG_IF_ERROR("workqueue_wakeup_fd",
                      grpc_wakeup_fd_wakeup(&pi->workqueue_wakeup_fd));
  }
}

static void workqueue_enqueue(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                              grpc_error *error) {
  GPR_TIMER_BEGIN("workqueue.enqueue", 0);
  grpc_workqueue *workqueue = (grpc_workqueue *)closure->scheduler;
  /* take a ref to the workqueue: otherwise it can happen that whatever events
   * this kicks off ends up destroying the workqueue before this function
   * completes */
  GRPC_WORKQUEUE_REF(workqueue, "enqueue");
  polling_island *pi = (polling_island *)workqueue;
  gpr_atm last = gpr_atm_no_barrier_fetch_add(&pi->workqueue_item_count, 1);
  closure->error_data.error = error;
  gpr_mpscq_push(&pi->workqueue_items, &closure->next_data.atm_next);
  if (last == 0) {
    workqueue_maybe_wakeup(pi);
  }

  GRPC_WORKQUEUE_UNREF(exec_ctx, workqueue, "enqueue");
  GPR_TIMER_END("workqueue.enqueue", 0);
}

static grpc_closure_scheduler *workqueue_scheduler(grpc_workqueue *workqueue) {
  polling_island *pi = (polling_island *)workqueue;
  return workqueue == NULL ? grpc_schedule_on_exec_ctx
                           : &pi->workqueue_scheduler;
}

static grpc_error *polling_island_global_init() {
  grpc_error *error = GRPC_ERROR_NONE;

  error = grpc_wakeup_fd_init(&polling_island_wakeup_fd);
  if (error == GRPC_ERROR_NONE) {
    error = grpc_wakeup_fd_wakeup(&polling_island_wakeup_fd);
  }

  return error;
}

static void polling_island_global_shutdown() {
  grpc_wakeup_fd_destroy(&polling_island_wakeup_fd);
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

// #define GRPC_FD_REF_COUNT_DEBUG
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
    gpr_mu_destroy(&fd->mu);
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
    gpr_mu_init(&new_fd->mu);
  }

  /* Note: It is not really needed to get the new_fd->mu lock here. If this
   * is a newly created fd (or an fd we got from the freelist), no one else
   * would be holding a lock to it anyway. */
  gpr_mu_lock(&new_fd->mu);
  new_fd->pi = NULL;

  gpr_atm_rel_store(&new_fd->refst, (gpr_atm)1);
  new_fd->fd = fd;
  new_fd->orphaned = false;
  grpc_lfev_init(&new_fd->read_closure);
  grpc_lfev_init(&new_fd->write_closure);

  new_fd->freelist_next = NULL;
  new_fd->on_done_closure = NULL;

  gpr_mu_unlock(&new_fd->mu);

  char *fd_name;
  gpr_asprintf(&fd_name, "%s fd=%d", name, fd);
  grpc_iomgr_register_object(&new_fd->iomgr_object, fd_name);
#ifdef GRPC_FD_REF_COUNT_DEBUG
  gpr_log(GPR_DEBUG, "FD %d %p create %s", fd, (void *)new_fd, fd_name);
#endif
  gpr_free(fd_name);

  /* Associate the fd with one of the dedicated pi */
  add_fd_to_dedicated_pi(new_fd);
  return new_fd;
}

static int fd_wrapped_fd(grpc_fd *fd) {
  int ret_fd = -1;
  gpr_mu_lock(&fd->mu);
  if (!fd->orphaned) {
    ret_fd = fd->fd;
  }
  gpr_mu_unlock(&fd->mu);

  return ret_fd;
}

static void fd_orphan(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                      grpc_closure *on_done, int *release_fd,
                      const char *reason) {
  bool is_fd_closed = false;
  grpc_error *error = GRPC_ERROR_NONE;
  polling_island *unref_pi = NULL;

  gpr_mu_lock(&fd->mu);
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

  /* Remove the active status but keep referenced. We want this grpc_fd struct
     to be alive (and not added to freelist) until the end of this function */
  REF_BY(fd, 1, reason);

  /* Remove the fd from the polling island */
  if (fd->pi != NULL) {
    polling_island *pi = fd->pi;
    polling_island_remove_fd(pi, fd, is_fd_closed, &error);
    unref_pi = fd->pi;
    fd->pi = NULL;
  }

  grpc_closure_sched(exec_ctx, fd->on_done_closure, GRPC_ERROR_REF(error));

  gpr_mu_unlock(&fd->mu);
  UNREF_BY(fd, 2, reason); /* Drop the reference */
  if (unref_pi != NULL) {
    /* Unref stale polling island here, outside the fd lock above.
       The polling island owns a workqueue which owns an fd, and unreffing
       inside the lock can cause an eventual lock loop that makes TSAN very
       unhappy. */
    PI_UNREF(exec_ctx, unref_pi, "fd_orphan");
  }
  GRPC_LOG_IF_ERROR("fd_orphan", GRPC_ERROR_REF(error));
  GRPC_ERROR_UNREF(error);
}

/* This polling engine doesn't really need the read notifier functionality. So
 * it just returns a dummy read notifier pollset */
static grpc_pollset *fd_get_read_notifier_pollset(grpc_exec_ctx *exec_ctx,
                                                  grpc_fd *fd) {
  return &g_read_notifier;
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

static grpc_workqueue *fd_get_workqueue(grpc_fd *fd) { return NULL; }

/*******************************************************************************
 * Pollset Definitions
 */
/* TODO: sreek - Not needed anymore */
GPR_TLS_DECL(g_current_thread_pollset);
GPR_TLS_DECL(g_current_thread_worker);

static void pollset_worker_init(grpc_pollset_worker *worker) {
  worker->next = worker->prev = NULL;
  gpr_cv_init(&worker->kick_cv);
}

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

static grpc_error *pollset_worker_kick(grpc_pollset_worker *worker) {
  gpr_cv_signal(&worker->kick_cv);
  return GRPC_ERROR_NONE;
}

/* Return 1 if the pollset has active threads in pollset_work (pollset must
 * be locked) */
static int pollset_has_workers(grpc_pollset *p) {
  return p->root_worker.next != &p->root_worker;
}

static void remove_worker(grpc_pollset *p, grpc_pollset_worker *worker) {
  worker->prev->next = worker->next;
  worker->next->prev = worker->prev;
}

static grpc_pollset_worker *pop_front_worker(grpc_pollset *p) {
  if (pollset_has_workers(p)) {
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

/* p->mu must be held before calling this function */
static grpc_error *pollset_kick(grpc_pollset *p,
                                grpc_pollset_worker *specific_worker) {
  GPR_TIMER_BEGIN("pollset_kick", 0);
  grpc_error *error = GRPC_ERROR_NONE;
  const char *err_desc = "Kick Failure";
  grpc_pollset_worker *worker = specific_worker;
  if (worker != NULL) {
    if (worker == GRPC_POLLSET_KICK_BROADCAST) {
      if (pollset_has_workers(p)) {
        GPR_TIMER_BEGIN("pollset_kick.broadcast", 0);
        for (worker = p->root_worker.next; worker != &p->root_worker;
             worker = worker->next) {
          if (gpr_tls_get(&g_current_thread_worker) != (intptr_t)worker) {
            append_error(&error, pollset_worker_kick(worker), err_desc);
          }
        }
        GPR_TIMER_END("pollset_kick.broadcast", 0);
      } else {
        p->kicked_without_pollers = true;
      }
    } else {
      GPR_TIMER_MARK("kicked_specifically", 0);
      if (gpr_tls_get(&g_current_thread_worker) != (intptr_t)worker) {
        append_error(&error, pollset_worker_kick(worker), err_desc);
      }
    }
  } else if (gpr_tls_get(&g_current_thread_pollset) != (intptr_t)p) {
    /* Since worker == NULL, it means that we can kick "any" worker on this
       pollset 'p'. If 'p' happens to be the same pollset this thread is
       currently polling (i.e in pollset_work() function), then there is no need
       to kick any other worker since the current thread can just absorb the
       kick. This is the reason why we enter this case only when
       g_current_thread_pollset is != p */

    GPR_TIMER_MARK("kick_anonymous", 0);
    worker = pop_front_worker(p);
    if (worker != NULL) {
      GPR_TIMER_MARK("finally_kick", 0);
      push_back_worker(p, worker);
      append_error(&error, pollset_worker_kick(worker), err_desc);
    } else {
      GPR_TIMER_MARK("kicked_no_pollers", 0);
      p->kicked_without_pollers = true;
    }
  }

  GPR_TIMER_END("pollset_kick", 0);
  GRPC_LOG_IF_ERROR("pollset_kick", GRPC_ERROR_REF(error));
  return error;
}

static grpc_error *kick_poller(void) {
  return grpc_wakeup_fd_wakeup(&global_wakeup_fd);
}

static void pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  gpr_mu_init(&pollset->mu);
  *mu = &pollset->mu;
  pollset->pi = NULL;

  pollset->root_worker.next = pollset->root_worker.prev = &pollset->root_worker;
  pollset->kicked_without_pollers = false;

  pollset->shutting_down = false;
  pollset->finish_shutdown_called = false;
  pollset->shutdown_done = NULL;
}

static void fd_become_readable(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  grpc_lfev_set_ready(exec_ctx, &fd->read_closure);
}

static void fd_become_writable(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  grpc_lfev_set_ready(exec_ctx, &fd->write_closure);
}

static void pollset_release_polling_island(grpc_exec_ctx *exec_ctx,
                                           grpc_pollset *ps, char *reason) {
  if (ps->pi != NULL) {
    PI_UNREF(exec_ctx, ps->pi, reason);
  }
  ps->pi = NULL;
}

static void finish_shutdown_locked(grpc_exec_ctx *exec_ctx,
                                   grpc_pollset *pollset) {
  /* The pollset cannot have any workers if we are at this stage */
  GPR_ASSERT(!pollset_has_workers(pollset));

  pollset->finish_shutdown_called = true;

  /* Release the ref and set pollset->pi to NULL */
  pollset_release_polling_island(exec_ctx, pollset, "ps_shutdown");
  grpc_closure_sched(exec_ctx, pollset->shutdown_done, GRPC_ERROR_NONE);
}

/* pollset->mu lock must be held by the caller before calling this */
static void pollset_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                             grpc_closure *closure) {
  GPR_TIMER_BEGIN("pollset_shutdown", 0);
  GPR_ASSERT(!pollset->shutting_down);
  pollset->shutting_down = true;
  pollset->shutdown_done = closure;
  pollset_kick(pollset, GRPC_POLLSET_KICK_BROADCAST);

  /* If the pollset has any workers, we cannot call finish_shutdown_locked()
     because it would release the underlying polling island. In such a case, we
     let the last worker call finish_shutdown_locked() from pollset_work() */
  if (!pollset_has_workers(pollset)) {
    GPR_ASSERT(!pollset->finish_shutdown_called);
    GPR_TIMER_MARK("pollset_shutdown.finish_shutdown_locked", 0);
    finish_shutdown_locked(exec_ctx, pollset);
  }
  GPR_TIMER_END("pollset_shutdown", 0);
}

/* pollset_shutdown is guaranteed to be called before pollset_destroy. So other
 * than destroying the mutexes, there is nothing special that needs to be done
 * here */
static void pollset_destroy(grpc_pollset *pollset) {
  GPR_ASSERT(!pollset_has_workers(pollset));
  gpr_mu_destroy(&pollset->mu);
}

static bool maybe_do_workqueue_work(grpc_exec_ctx *exec_ctx,
                                    polling_island *pi) {
  if (gpr_mu_trylock(&pi->workqueue_read_mu)) {
    gpr_mpscq_node *n = gpr_mpscq_pop(&pi->workqueue_items);
    gpr_mu_unlock(&pi->workqueue_read_mu);
    if (n != NULL) {
      if (gpr_atm_full_fetch_add(&pi->workqueue_item_count, -1) > 1) {
        workqueue_maybe_wakeup(pi);
      }
      grpc_closure *c = (grpc_closure *)n;
      grpc_error *error = c->error_data.error;
#ifndef NDEBUG
      c->scheduled = false;
#endif
      c->cb(exec_ctx, c->cb_arg, error);
      GRPC_ERROR_UNREF(error);
      return true;
    } else if (gpr_atm_no_barrier_load(&pi->workqueue_item_count) > 0) {
      /* n == NULL might mean there's work but it's not available to be popped
       * yet - try to ensure another workqueue wakes up to check shortly if so
       */
      workqueue_maybe_wakeup(pi);
    }
  }
  return false;
}

#define GRPC_EPOLL_MAX_EVENTS 100
static void do_epoll_wait(grpc_exec_ctx *exec_ctx, int epoll_fd,
                          polling_island *pi, grpc_error **error) {
  struct epoll_event ep_ev[GRPC_EPOLL_MAX_EVENTS];
  int ep_rv;
  char *err_msg;
  const char *err_desc = "do_epoll_wait";

  int timeout_ms = -1;

  GRPC_SCHEDULING_START_BLOCKING_REGION;
  ep_rv = epoll_wait(epoll_fd, ep_ev, GRPC_EPOLL_MAX_EVENTS, timeout_ms);
  GRPC_SCHEDULING_END_BLOCKING_REGION;

  if (ep_rv < 0) {
    gpr_asprintf(&err_msg,
                 "epoll_wait() epoll fd: %d failed with error: %d (%s)",
                 epoll_fd, errno, strerror(errno));
    append_error(error, GRPC_OS_ERROR(errno, err_msg), err_desc);
  }

#ifdef GRPC_TSAN
  /* See the definition of g_poll_sync for more details */
  gpr_atm_acq_load(&g_epoll_sync);
#endif /* defined(GRPC_TSAN) */

  for (int i = 0; i < ep_rv; ++i) {
    void *data_ptr = ep_ev[i].data.ptr;
    if (data_ptr == &global_wakeup_fd) {
      grpc_timer_consume_kick();
      append_error(error, grpc_wakeup_fd_consume_wakeup(&global_wakeup_fd),
                   err_desc);
    } else if (data_ptr == &pi->workqueue_wakeup_fd) {
      append_error(error,
                   grpc_wakeup_fd_consume_wakeup(&pi->workqueue_wakeup_fd),
                   err_desc);
      maybe_do_workqueue_work(exec_ctx, pi);
    } else if (data_ptr == &polling_island_wakeup_fd) {
      gpr_atm_rel_store(&pi->is_shutdown, 1);
      gpr_log(GPR_INFO, "pollset poller: shutdown set");
    } else {
      grpc_fd *fd = data_ptr;
      int cancel = ep_ev[i].events & (EPOLLERR | EPOLLHUP);
      int read_ev = ep_ev[i].events & (EPOLLIN | EPOLLPRI);
      int write_ev = ep_ev[i].events & EPOLLOUT;
      if (read_ev || cancel) {
        fd_become_readable(exec_ctx, fd);
      }
      if (write_ev || cancel) {
        fd_become_writable(exec_ctx, fd);
      }
    }
  }
}

static void polling_island_work(grpc_exec_ctx *exec_ctx, polling_island *pi,
                                grpc_error **error) {
  int epoll_fd = -1;
  GPR_TIMER_BEGIN("polling_island_work", 0);

  /* Since epoll_fd is immutable, it is safe to read it without a lock on the
     polling island. */
  epoll_fd = pi->epoll_fd;

  /* Add an extra ref so that the island does not get destroyed (which means
     the epoll_fd won't be closed) while we are are doing an epoll_wait() on the
     epoll_fd */
  PI_ADD_REF(pi, "ps_work");

  /* If we get some workqueue work to do, it might end up completing an item on
     the completion queue, so there's no need to poll... so we skip that and
     redo the complete loop to verify */
  if (!maybe_do_workqueue_work(exec_ctx, pi)) {
    gpr_atm_no_barrier_fetch_add(&pi->poller_count, 1);
    g_current_thread_polling_island = pi;

    do_epoll_wait(exec_ctx, epoll_fd, pi, error);

    g_current_thread_polling_island = NULL;
    gpr_atm_no_barrier_fetch_add(&pi->poller_count, -1);
  }

  /* Before leaving, release the extra ref we added to the polling island. It
     is important to use "pi" here (i.e our old copy of pollset->pi
     that we got before releasing the polling island lock). This is because
     pollset->pi pointer might get udpated in other parts of the
     code when there is an island merge while we are doing epoll_wait() above */
  PI_UNREF(exec_ctx, pi, "ps_work");

  GPR_TIMER_END("polling_island_work", 0);
}

/* pollset->mu lock must be held by the caller before calling this.
   The function pollset_work() may temporarily release the lock (pollset->mu)
   during the course of its execution but it will always re-acquire the lock and
   ensure that it is held by the time the function returns */
static grpc_error *pollset_work(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                                grpc_pollset_worker **worker_hdl,
                                gpr_timespec now, gpr_timespec deadline) {
  GPR_TIMER_BEGIN("pollset_work", 0);
  grpc_error *error = GRPC_ERROR_NONE;

  grpc_pollset_worker worker;
  pollset_worker_init(&worker);

  if (worker_hdl) *worker_hdl = &worker;

  gpr_tls_set(&g_current_thread_pollset, (intptr_t)pollset);
  gpr_tls_set(&g_current_thread_worker, (intptr_t)&worker);

  if (pollset->kicked_without_pollers) {
    /* If the pollset was kicked without pollers, pretend that the current
       worker got the kick and skip polling. A kick indicates that there is some
       work that needs attention like an event on the completion queue or an
       alarm */
    GPR_TIMER_MARK("pollset_work.kicked_without_pollers", 0);
    pollset->kicked_without_pollers = 0;
  } else if (!pollset->shutting_down) {
    push_front_worker(pollset, &worker);

    gpr_cv_wait(&worker.kick_cv, &pollset->mu,
                gpr_convert_clock_type(deadline, GPR_CLOCK_REALTIME));
    /* pollset->mu locked here */

    remove_worker(pollset, &worker);
  }

  /* If we are the last worker on the pollset (i.e pollset_has_workers() is
     false at this point) and the pollset is shutting down, we may have to
     finish the shutdown process by calling finish_shutdown_locked().
     See pollset_shutdown() for more details.

     Note: Continuing to access pollset here is safe; it is the caller's
     responsibility to not destroy a pollset when it has outstanding calls to
     pollset_work() */
  if (pollset->shutting_down && !pollset_has_workers(pollset) &&
      !pollset->finish_shutdown_called) {
    GPR_TIMER_MARK("pollset_work.finish_shutdown_locked", 0);
    finish_shutdown_locked(exec_ctx, pollset);

    gpr_mu_unlock(&pollset->mu);
    grpc_exec_ctx_flush(exec_ctx);
    gpr_mu_lock(&pollset->mu);
  }

  if (worker_hdl) *worker_hdl = NULL;

  gpr_tls_set(&g_current_thread_pollset, (intptr_t)0);
  gpr_tls_set(&g_current_thread_worker, (intptr_t)0);

  GPR_TIMER_END("pollset_work", 0);

  GRPC_LOG_IF_ERROR("pollset_work", GRPC_ERROR_REF(error));
  return error;
}

static void pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_fd *fd) {
  /* Nothing to do */
}

/*******************************************************************************
 * Pollset-set Definitions
 */
grpc_pollset_set g_dummy_pollset_set;
static grpc_pollset_set *pollset_set_create(void) {
  return &g_dummy_pollset_set;
}

static void pollset_set_destroy(grpc_exec_ctx *exec_ctx,
                                grpc_pollset_set *pss) {
  /* Nothing to do */
}

static void pollset_set_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset_set *pss,
                               grpc_fd *fd) {
  /* Nothing to do */
}

static void pollset_set_del_fd(grpc_exec_ctx *exec_ctx, grpc_pollset_set *pss,
                               grpc_fd *fd) {
  /* Nothing to do */
}

static void pollset_set_add_pollset(grpc_exec_ctx *exec_ctx,
                                    grpc_pollset_set *pss, grpc_pollset *ps) {
  /* Nothing to do */
}

static void pollset_set_del_pollset(grpc_exec_ctx *exec_ctx,
                                    grpc_pollset_set *pss, grpc_pollset *ps) {
  /* Nothing to do */
}

static void pollset_set_add_pollset_set(grpc_exec_ctx *exec_ctx,
                                        grpc_pollset_set *bag,
                                        grpc_pollset_set *item) {
  /* Nothing to do */
}

static void pollset_set_del_pollset_set(grpc_exec_ctx *exec_ctx,
                                        grpc_pollset_set *bag,
                                        grpc_pollset_set *item) {
  /* Nothing to do */
}

/*******************************************************************************
 * Event engine binding
 */

static void shutdown_engine(void) {
  shutdown_dedicated_poller_threads();
  shutdown_dedicated_polling_islands();
  fd_global_shutdown();
  pollset_global_shutdown();
  polling_island_global_shutdown();
  gpr_log(GPR_INFO, "ev-epoll-threadpool engine shutdown complete");
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

/*****************************************************************************
 * Dedicated polling threads and pollsets - Definitions
 */
static void add_fd_to_dedicated_pi(grpc_fd *fd) {
  GPR_ASSERT(fd->pi == NULL);
  GPR_TIMER_BEGIN("add_fd_to_dedicated_pi", 0);

  grpc_error *error = GRPC_ERROR_NONE;
  size_t idx = ((size_t)rand()) % g_num_pi;
  polling_island *pi = g_polling_islands[idx];

  gpr_mu_lock(&fd->mu);

  if (fd->orphaned) {
    gpr_mu_unlock(&fd->mu);
    return; /* Early out */
  }

  polling_island_add_fd_locked(pi, fd, &error);
  PI_ADD_REF(pi, "fd");
  fd->pi = pi;

  GRPC_POLLING_TRACE("add_fd_to_dedicated_pi (fd: %d, pi idx = %ld)", fd->fd,
                     idx);
  gpr_mu_unlock(&fd->mu);

  GRPC_LOG_IF_ERROR("add_fd_to_dedicated_pi", error);
  GPR_TIMER_END("add_fd_to_dedicated_pi", 0);
}

static bool init_dedicated_polling_islands() {
  grpc_error *error = GRPC_ERROR_NONE;
  bool is_success = true;

  g_polling_islands =
      (polling_island **)malloc(g_num_pi * sizeof(polling_island *));

  for (size_t i = 0; i < g_num_pi; i++) {
    g_polling_islands[i] = polling_island_create(&error);
    if (g_polling_islands[i] == NULL) {
      gpr_log(GPR_ERROR, "Error in creating a dedicated polling island");
      g_num_pi = i; /* Helps cleanup */
      shutdown_dedicated_polling_islands();
      is_success = false;
      goto done;
    }

    PI_ADD_REF(g_polling_islands[i], "init_dedicated_polling_islands");
  }

  gpr_mu *mu;
  pollset_init(&g_read_notifier, &mu);

done:
  GRPC_LOG_IF_ERROR("init_dedicated_polling_islands", error);
  return is_success;
}

static void shutdown_dedicated_polling_islands() {
  if (!g_polling_islands) {
    return;
  }

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  for (size_t i = 0; i < g_num_pi; i++) {
    PI_UNREF(&exec_ctx, g_polling_islands[i],
             "shutdown_dedicated_polling_islands");
  }
  grpc_exec_ctx_finish(&exec_ctx);

  gpr_free(g_polling_islands);
  g_polling_islands = NULL;
  pollset_destroy(&g_read_notifier);
}

static void poller_thread_loop(void *arg) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_error *error = GRPC_ERROR_NONE;
  polling_island *pi = (polling_island *)arg;

  while (!gpr_atm_acq_load(&pi->is_shutdown)) {
    polling_island_work(&exec_ctx, pi, &error);
    grpc_exec_ctx_flush(&exec_ctx);
  }

  grpc_exec_ctx_finish(&exec_ctx);
  GRPC_LOG_IF_ERROR("poller_thread_loop", error);
}

/* g_polling_islands MUST be initialized before calling this */
static void start_dedicated_poller_threads() {
  GPR_ASSERT(g_polling_islands);

  gpr_log(GPR_INFO, "Starting poller threads");

  /* One thread per pollset */
  g_poller_threads = (gpr_thd_id *)malloc(g_num_pi * sizeof(gpr_thd_id));
  gpr_thd_options options = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&options);

  for (size_t i = 0; i < g_num_pi; i++) {
    gpr_thd_new(&g_poller_threads[i], poller_thread_loop,
                (void *)g_polling_islands[i], &options);
  }
}

static void shutdown_dedicated_poller_threads() {
  GPR_ASSERT(g_poller_threads);
  GPR_ASSERT(g_polling_islands);
  grpc_error *error = GRPC_ERROR_NONE;

  gpr_log(GPR_INFO, "Shutting down pollers");

  polling_island *pi = NULL;
  for (size_t i = 0; i < g_num_pi; i++) {
    pi = g_polling_islands[i];
    polling_island_add_wakeup_fd_locked(pi, &polling_island_wakeup_fd, &error);
  }

  for (size_t i = 0; i < g_num_pi; i++) {
    gpr_thd_join(g_poller_threads[i]);
  }

  gpr_log(GPR_ERROR, "polling island delete called");
  GRPC_LOG_IF_ERROR("shutdown_dedicated_poller_threads", error);
  gpr_free(g_poller_threads);
  g_poller_threads = NULL;
}

/****************************************************************************/

/* It is possible that GLIBC has epoll but the underlying kernel doesn't.
 * Create a dummy epoll_fd to make sure epoll support is available */
static bool is_epoll_available() {
  int fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd < 0) {
    gpr_log(
        GPR_ERROR,
        "epoll_create1 failed with error: %d. Not using epoll polling engine",
        fd);
    return false;
  }
  close(fd);
  return true;
}

const grpc_event_engine_vtable *grpc_init_epoll_thread_pool_linux(void) {
  if (!grpc_has_wakeup_fd()) {
    return NULL;
  }

  if (!is_epoll_available()) {
    return NULL;
  }

  fd_global_init();

  if (!GRPC_LOG_IF_ERROR("pollset_global_init", pollset_global_init())) {
    return NULL;
  }

  if (!GRPC_LOG_IF_ERROR("polling_island_global_init",
                         polling_island_global_init())) {
    return NULL;
  }

  if (!init_dedicated_polling_islands()) {
    return NULL;
  }

  /* TODO (sreek): Maynot be a good idea to start threads here (especially if
   * this engine doesn't get picked. Consider introducing an engine_init
   * function in the vtable */
  start_dedicated_poller_threads();
  return &vtable;
}

#else /* defined(GRPC_LINUX_EPOLL) */
#if defined(GRPC_POSIX_SOCKET)
#include "src/core/lib/iomgr/ev_posix.h"
/* If GRPC_LINUX_EPOLL is not defined, it means epoll is not available. Return
 * NULL */
const grpc_event_engine_vtable *grpc_init_epoll_linux(void) { return NULL; }
#endif /* defined(GRPC_POSIX_SOCKET) */
#endif /* !defined(GRPC_LINUX_EPOLL) */
