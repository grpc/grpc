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

#include "src/core/lib/iomgr/ev_epoll1_linux.h"

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

/* TODO: sreek: Right now, this wakes up all pollers. In future we should make
 * sure to wake up one polling thread (which can wake up other threads if
 * needed) */
static grpc_wakeup_fd global_wakeup_fd;
static int g_epfd;
static bool g_timer_kick = false;

/*******************************************************************************
 * Fd Declarations
 */

struct grpc_fd {
  int fd;

  gpr_atm read_closure;
  gpr_atm write_closure;

  struct grpc_fd *freelist_next;

  /* The pollset that last noticed that the fd is readable. The actual type
   * stored in this is (grpc_pollset *) */
  gpr_atm read_notifier_pollset;

  grpc_iomgr_object iomgr_object;
};

static void fd_global_init(void);
static void fd_global_shutdown(void);

/*******************************************************************************
 * Pollset Declarations
 */

typedef struct pollset_worker_link {
  grpc_pollset_worker *next;
  grpc_pollset_worker *prev;
} pollset_worker_link;

typedef enum {
  PWL_POLLSET,
  PWL_POLLABLE,
  POLLSET_WORKER_LINK_COUNT
} pollset_worker_links;

struct grpc_pollset_worker {
  bool kicked;
  bool initialized_cv;
  pollset_worker_link links[POLLSET_WORKER_LINK_COUNT];
  gpr_cv cv;
};

struct grpc_pollset {
  grpc_pollset_worker *root_worker;
  bool kicked_without_poller;

  bool shutting_down;          /* Is the pollset shutting down ? */
  bool finish_shutdown_called; /* Is the 'finish_shutdown_locked()' called ? */
  grpc_closure *shutdown_closure; /* Called after after shutdown is complete */
};

/*******************************************************************************
 * Pollset-set Declarations
 */
struct grpc_pollset_set {};

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

static void fd_global_init(void) { gpr_mu_init(&fd_freelist_mu); }

static void fd_global_shutdown(void) {
  gpr_mu_lock(&fd_freelist_mu);
  gpr_mu_unlock(&fd_freelist_mu);
  while (fd_freelist != NULL) {
    grpc_fd *fd = fd_freelist;
    fd_freelist = fd_freelist->freelist_next;
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

  new_fd->fd = fd;
  grpc_lfev_init(&new_fd->read_closure);
  grpc_lfev_init(&new_fd->write_closure);
  gpr_atm_no_barrier_store(&new_fd->read_notifier_pollset, (gpr_atm)NULL);

  new_fd->freelist_next = NULL;

  char *fd_name;
  gpr_asprintf(&fd_name, "%s fd=%d", name, fd);
  grpc_iomgr_register_object(&new_fd->iomgr_object, fd_name);
#ifdef GRPC_FD_REF_COUNT_DEBUG
  gpr_log(GPR_DEBUG, "FD %d %p create %s", fd, (void *)new_fd, fd_name);
#endif
  gpr_free(fd_name);

  struct epoll_event ev = {.events = (uint32_t)(EPOLLIN | EPOLLOUT | EPOLLET),
                           .data.ptr = new_fd};
  if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
    gpr_log(GPR_ERROR, "epoll_ctl failed: %s", strerror(errno));
  }

  return new_fd;
}

static int fd_wrapped_fd(grpc_fd *fd) { return fd->fd; }

/* Might be called multiple times */
static void fd_shutdown(grpc_exec_ctx *exec_ctx, grpc_fd *fd, grpc_error *why) {
  if (grpc_lfev_set_shutdown(exec_ctx, &fd->read_closure,
                             GRPC_ERROR_REF(why))) {
    shutdown(fd->fd, SHUT_RDWR);
    grpc_lfev_set_shutdown(exec_ctx, &fd->write_closure, GRPC_ERROR_REF(why));
  }
  GRPC_ERROR_UNREF(why);
}

static void fd_orphan(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                      grpc_closure *on_done, int *release_fd,
                      const char *reason) {
  grpc_error *error = GRPC_ERROR_NONE;

  if (!grpc_lfev_is_shutdown(&fd->read_closure)) {
    fd_shutdown(exec_ctx, fd, GRPC_ERROR_CREATE_FROM_COPIED_STRING(reason));
  }

  /* If release_fd is not NULL, we should be relinquishing control of the file
     descriptor fd->fd (but we still own the grpc_fd structure). */
  if (release_fd != NULL) {
    *release_fd = fd->fd;
  } else {
    close(fd->fd);
  }

  grpc_closure_sched(exec_ctx, on_done, GRPC_ERROR_REF(error));

  grpc_iomgr_unregister_object(&fd->iomgr_object);
  grpc_lfev_destroy(&fd->read_closure);
  grpc_lfev_destroy(&fd->write_closure);

  gpr_mu_lock(&fd_freelist_mu);
  fd->freelist_next = fd_freelist;
  fd_freelist = fd;
  gpr_mu_unlock(&fd_freelist_mu);
}

static grpc_pollset *fd_get_read_notifier_pollset(grpc_exec_ctx *exec_ctx,
                                                  grpc_fd *fd) {
  gpr_atm notifier = gpr_atm_acq_load(&fd->read_notifier_pollset);
  return (grpc_pollset *)notifier;
}

static bool fd_is_shutdown(grpc_fd *fd) {
  return grpc_lfev_is_shutdown(&fd->read_closure);
}

static void fd_notify_on_read(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                              grpc_closure *closure) {
  grpc_lfev_notify_on(exec_ctx, &fd->read_closure, closure);
}

static void fd_notify_on_write(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                               grpc_closure *closure) {
  grpc_lfev_notify_on(exec_ctx, &fd->write_closure, closure);
}

static grpc_workqueue *fd_get_workqueue(grpc_fd *fd) {
  return NULL; /* TODO(ctiller): add a global workqueue */
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

/*******************************************************************************
 * Pollset Definitions
 */

/* Return true if first in list */
static bool worker_insert(grpc_pollset_worker **root, pollset_worker_links link,
                          grpc_pollset_worker *worker) {
  if (*root == NULL) {
    *root = worker;
    worker->links[link].next = worker->links[link].prev = worker;
    return true;
  } else {
    worker->links[link].next = *root;
    worker->links[link].prev = worker->links[link].next->links[link].prev;
    worker->links[link].next->links[link].prev = worker;
    worker->links[link].prev->links[link].next = worker;
    return false;
  }
}

/* Return true if last in list */
typedef enum { EMPTIED, NEW_ROOT, REMOVED } worker_remove_result;

static worker_remove_result worker_remove(grpc_pollset_worker **root,
                                          pollset_worker_links link,
                                          grpc_pollset_worker *worker) {
  if (worker == *root) {
    if (worker == worker->links[link].next) {
      *root = NULL;
      return EMPTIED;
    } else {
      *root = worker->links[link].next;
      worker->links[link].prev->links[link].next = worker->links[link].next;
      worker->links[link].next->links[link].prev = worker->links[link].prev;
      return NEW_ROOT;
    }
  } else {
    worker->links[link].prev->links[link].next = worker->links[link].next;
    worker->links[link].next->links[link].prev = worker->links[link].prev;
    return REMOVED;
  }
}

GPR_TLS_DECL(g_current_thread_pollset);
GPR_TLS_DECL(g_current_thread_worker);
static gpr_mu g_pollset_mu;
static grpc_pollset_worker *g_root_worker;

static grpc_error *pollset_global_init(void) {
  gpr_mu_init(&g_pollset_mu);
  gpr_tls_init(&g_current_thread_pollset);
  gpr_tls_init(&g_current_thread_worker);
  global_wakeup_fd.read_fd = -1;
  grpc_error *err = grpc_wakeup_fd_init(&global_wakeup_fd);
  if (err != GRPC_ERROR_NONE) return err;
  struct epoll_event ev = {.events = (uint32_t)(EPOLLIN | EPOLLET),
                           .data.ptr = &global_wakeup_fd};
  if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, global_wakeup_fd.read_fd, &ev) != 0) {
    return GRPC_OS_ERROR(errno, "epoll_ctl");
  }
  return GRPC_ERROR_NONE;
}

static void pollset_global_shutdown(void) {
  gpr_mu_destroy(&g_pollset_mu);
  gpr_tls_destroy(&g_current_thread_pollset);
  gpr_tls_destroy(&g_current_thread_worker);
  if (global_wakeup_fd.read_fd != -1) grpc_wakeup_fd_destroy(&global_wakeup_fd);
}

static void pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  *mu = &g_pollset_mu;
}

static grpc_error *pollset_kick_all(grpc_pollset *pollset) {
  grpc_error *error = GRPC_ERROR_NONE;
  if (pollset->root_worker != NULL) {
    grpc_pollset_worker *worker = pollset->root_worker;
    do {
      if (worker->initialized_cv) {
        worker->kicked = true;
        gpr_cv_signal(&worker->cv);
      } else {
        append_error(&error, grpc_wakeup_fd_wakeup(&global_wakeup_fd),
                     "pollset_shutdown");
      }

      worker = worker->links[PWL_POLLSET].next;
    } while (worker != pollset->root_worker);
  }
  return error;
}

static void pollset_maybe_finish_shutdown(grpc_exec_ctx *exec_ctx,
                                          grpc_pollset *pollset) {
  if (pollset->shutdown_closure != NULL && pollset->root_worker == NULL) {
    grpc_closure_sched(exec_ctx, pollset->shutdown_closure, GRPC_ERROR_NONE);
    pollset->shutdown_closure = NULL;
  }
}

static void pollset_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                             grpc_closure *closure) {
  GPR_ASSERT(pollset->shutdown_closure == NULL);
  pollset->shutdown_closure = closure;
  GRPC_LOG_IF_ERROR("pollset_shutdown", pollset_kick_all(pollset));
  pollset_maybe_finish_shutdown(exec_ctx, pollset);
}

static void pollset_destroy(grpc_pollset *pollset) {}

#define MAX_EPOLL_EVENTS 100

static int poll_deadline_to_millis_timeout(gpr_timespec deadline,
                                           gpr_timespec now) {
  gpr_timespec timeout;
  if (gpr_time_cmp(deadline, gpr_inf_future(deadline.clock_type)) == 0) {
    return -1;
  }

  if (gpr_time_cmp(deadline, now) <= 0) {
    return 0;
  }

  static const gpr_timespec round_up = {
      .clock_type = GPR_TIMESPAN, .tv_sec = 0, .tv_nsec = GPR_NS_PER_MS - 1};
  timeout = gpr_time_sub(deadline, now);
  int millis = gpr_time_to_millis(gpr_time_add(timeout, round_up));
  return millis >= 1 ? millis : 1;
}

static grpc_error *pollset_epoll(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                                 gpr_timespec now, gpr_timespec deadline) {
  struct epoll_event events[MAX_EPOLL_EVENTS];
  static const char *err_desc = "pollset_poll";

  int timeout = poll_deadline_to_millis_timeout(deadline, now);

  if (timeout != 0) {
    GRPC_SCHEDULING_START_BLOCKING_REGION;
  }
  int r;
  do {
    r = epoll_wait(g_epfd, events, MAX_EPOLL_EVENTS, timeout);
  } while (r < 0 && errno == EINTR);
  if (timeout != 0) {
    GRPC_SCHEDULING_END_BLOCKING_REGION;
  }

  if (r < 0) return GRPC_OS_ERROR(errno, "epoll_wait");

  grpc_error *error = GRPC_ERROR_NONE;
  for (int i = 0; i < r; i++) {
    void *data_ptr = events[i].data.ptr;
    if (data_ptr == &global_wakeup_fd) {
      if (g_timer_kick) {
        g_timer_kick = false;
        grpc_timer_consume_kick();
      }
      append_error(&error, grpc_wakeup_fd_consume_wakeup(&global_wakeup_fd),
                   err_desc);
    } else {
      grpc_fd *fd = (grpc_fd *)(data_ptr);
      bool cancel = (events[i].events & (EPOLLERR | EPOLLHUP)) != 0;
      bool read_ev = (events[i].events & (EPOLLIN | EPOLLPRI)) != 0;
      bool write_ev = (events[i].events & EPOLLOUT) != 0;
      if (read_ev || cancel) {
        fd_become_readable(exec_ctx, fd, pollset);
      }
      if (write_ev || cancel) {
        fd_become_writable(exec_ctx, fd);
      }
    }
  }

  return error;
}

static bool begin_worker(grpc_pollset *pollset, grpc_pollset_worker *worker,
                         grpc_pollset_worker **worker_hdl, gpr_timespec *now,
                         gpr_timespec deadline) {
  bool do_poll = true;
  if (worker_hdl != NULL) *worker_hdl = worker;
  worker->initialized_cv = false;
  worker->kicked = false;

  worker_insert(&pollset->root_worker, PWL_POLLSET, worker);
  if (!worker_insert(&g_root_worker, PWL_POLLABLE, worker)) {
    worker->initialized_cv = true;
    gpr_cv_init(&worker->cv);
    while (do_poll && g_root_worker != worker) {
      if (gpr_cv_wait(&worker->cv, &g_pollset_mu, deadline)) {
        do_poll = false;
      } else if (worker->kicked) {
        do_poll = false;
      }
    }
    *now = gpr_now(now->clock_type);
  }

  return do_poll && pollset->shutdown_closure == NULL;
}

static void end_worker(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                       grpc_pollset_worker *worker,
                       grpc_pollset_worker **worker_hdl) {
  if (NEW_ROOT == worker_remove(&g_root_worker, PWL_POLLABLE, worker)) {
    gpr_cv_signal(&g_root_worker->cv);
  }
  if (worker->initialized_cv) {
    gpr_cv_destroy(&worker->cv);
  }
  if (EMPTIED == worker_remove(&pollset->root_worker, PWL_POLLSET, worker)) {
    pollset_maybe_finish_shutdown(exec_ctx, pollset);
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
  static const char *err_desc = "pollset_work";
  if (pollset->kicked_without_poller) {
    pollset->kicked_without_poller = false;
    return GRPC_ERROR_NONE;
  }
  if (begin_worker(pollset, &worker, worker_hdl, &now, deadline)) {
    gpr_tls_set(&g_current_thread_pollset, (intptr_t)pollset);
    gpr_tls_set(&g_current_thread_worker, (intptr_t)&worker);
    GPR_ASSERT(!pollset->shutdown_closure);
    gpr_mu_unlock(&g_pollset_mu);
    append_error(&error, pollset_epoll(exec_ctx, pollset, now, deadline),
                 err_desc);
    grpc_exec_ctx_flush(exec_ctx);
    gpr_mu_lock(&g_pollset_mu);
    gpr_tls_set(&g_current_thread_pollset, 0);
    gpr_tls_set(&g_current_thread_worker, 0);
    pollset_maybe_finish_shutdown(exec_ctx, pollset);
  }
  end_worker(exec_ctx, pollset, &worker, worker_hdl);
  return error;
}

static grpc_error *pollset_kick(grpc_pollset *pollset,
                                grpc_pollset_worker *specific_worker) {
  if (specific_worker == NULL) {
    if (gpr_tls_get(&g_current_thread_pollset) != (intptr_t)pollset) {
      grpc_pollset_worker *root_worker = pollset->root_worker;
      if (root_worker == NULL) {
        pollset->kicked_without_poller = true;
        return GRPC_ERROR_NONE;
      }
      grpc_pollset_worker *next_worker = root_worker->links[PWL_POLLSET].next;
      if (root_worker == next_worker && root_worker == g_root_worker) {
        root_worker->kicked = true;
        return grpc_wakeup_fd_wakeup(&global_wakeup_fd);
      } else {
        next_worker->kicked = true;
        gpr_cv_signal(&next_worker->cv);
        return GRPC_ERROR_NONE;
      }
    } else {
      return GRPC_ERROR_NONE;
    }
  } else if (specific_worker->kicked) {
    return GRPC_ERROR_NONE;
  } else if (gpr_tls_get(&g_current_thread_worker) ==
             (intptr_t)specific_worker) {
    specific_worker->kicked = true;
    return GRPC_ERROR_NONE;
  } else if (specific_worker == g_root_worker) {
    specific_worker->kicked = true;
    return grpc_wakeup_fd_wakeup(&global_wakeup_fd);
  } else {
    specific_worker->kicked = true;
    gpr_cv_signal(&specific_worker->cv);
    return GRPC_ERROR_NONE;
  }
}

static void pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_fd *fd) {}

static grpc_error *kick_poller(void) {
  gpr_mu_lock(&g_pollset_mu);
  g_timer_kick = true;
  gpr_mu_unlock(&g_pollset_mu);
  return grpc_wakeup_fd_wakeup(&global_wakeup_fd);
}

/*******************************************************************************
 * Workqueue Definitions
 */

#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
static grpc_workqueue *workqueue_ref(grpc_workqueue *workqueue,
                                     const char *file, int line,
                                     const char *reason) {
  return workqueue;
}

static void workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue,
                            const char *file, int line, const char *reason) {}
#else
static grpc_workqueue *workqueue_ref(grpc_workqueue *workqueue) {
  return workqueue;
}

static void workqueue_unref(grpc_exec_ctx *exec_ctx,
                            grpc_workqueue *workqueue) {}
#endif

static grpc_closure_scheduler *workqueue_scheduler(grpc_workqueue *workqueue) {
  return grpc_schedule_on_exec_ctx;
}

/*******************************************************************************
 * Pollset-set Definitions
 */

static grpc_pollset_set *pollset_set_create(void) {
  return (grpc_pollset_set *)((intptr_t)0xdeafbeef);
}

static void pollset_set_destroy(grpc_exec_ctx *exec_ctx,
                                grpc_pollset_set *pss) {}

static void pollset_set_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset_set *pss,
                               grpc_fd *fd) {}

static void pollset_set_del_fd(grpc_exec_ctx *exec_ctx, grpc_pollset_set *pss,
                               grpc_fd *fd) {}

static void pollset_set_add_pollset(grpc_exec_ctx *exec_ctx,
                                    grpc_pollset_set *pss, grpc_pollset *ps) {}

static void pollset_set_del_pollset(grpc_exec_ctx *exec_ctx,
                                    grpc_pollset_set *pss, grpc_pollset *ps) {}

static void pollset_set_add_pollset_set(grpc_exec_ctx *exec_ctx,
                                        grpc_pollset_set *bag,
                                        grpc_pollset_set *item) {}

static void pollset_set_del_pollset_set(grpc_exec_ctx *exec_ctx,
                                        grpc_pollset_set *bag,
                                        grpc_pollset_set *item) {}

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
const grpc_event_engine_vtable *grpc_init_epoll1_linux(bool explicit_request) {
  if (!grpc_has_wakeup_fd()) {
    return NULL;
  }

  g_epfd = epoll_create1(EPOLL_CLOEXEC);
  if (g_epfd < 0) {
    gpr_log(GPR_ERROR, "epoll unavailable");
    return NULL;
  }

  fd_global_init();

  if (!GRPC_LOG_IF_ERROR("pollset_global_init", pollset_global_init())) {
    close(g_epfd);
    fd_global_shutdown();
    return NULL;
  }

  return &vtable;
}

#else /* defined(GRPC_LINUX_EPOLL) */
#if defined(GRPC_POSIX_SOCKET)
#include "src/core/lib/iomgr/ev_posix.h"
/* If GRPC_LINUX_EPOLL is not defined, it means epoll is not available. Return
 * NULL */
const grpc_event_engine_vtable *grpc_init_epoll1_linux(bool explicit_request) {
  return NULL;
}
#endif /* defined(GRPC_POSIX_SOCKET) */
#endif /* !defined(GRPC_LINUX_EPOLL) */
