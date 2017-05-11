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
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/tls.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/lockfree_event.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/lib/iomgr/workqueue.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/block_annotate.h"

static grpc_wakeup_fd global_wakeup_fd;
static int g_epfd;

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

typedef enum { UNKICKED, KICKED, DESIGNATED_POLLER } kick_state;

struct grpc_pollset_worker {
  kick_state kick_state;
  bool initialized_cv;
  grpc_pollset_worker *next;
  grpc_pollset_worker *prev;
  gpr_cv cv;
  grpc_closure_list schedule_on_end_work;
};

#define MAX_NEIGHBOURHOODS 1024

typedef struct pollset_neighbourhood {
  gpr_mu mu;
  grpc_pollset *active_root;
  char pad[GPR_CACHELINE_SIZE];
} pollset_neighbourhood;

struct grpc_pollset {
  gpr_mu mu;
  pollset_neighbourhood *neighbourhood;
  bool reassigning_neighbourhood;
  grpc_pollset_worker *root_worker;
  bool kicked_without_poller;
  bool seen_inactive;
  bool shutting_down;          /* Is the pollset shutting down ? */
  bool finish_shutdown_called; /* Is the 'finish_shutdown_locked()' called ? */
  grpc_closure *shutdown_closure; /* Called after after shutdown is complete */
  int begin_refs;

  grpc_pollset *next;
  grpc_pollset *prev;
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
  return (grpc_workqueue *)0xb0b51ed;
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

GPR_TLS_DECL(g_current_thread_pollset);
GPR_TLS_DECL(g_current_thread_worker);
static gpr_atm g_active_poller;
static pollset_neighbourhood *g_neighbourhoods;
static size_t g_num_neighbourhoods;
static gpr_mu g_wq_mu;
static grpc_closure_list g_wq_items;

/* Return true if first in list */
static bool worker_insert(grpc_pollset *pollset, grpc_pollset_worker *worker) {
  if (pollset->root_worker == NULL) {
    pollset->root_worker = worker;
    worker->next = worker->prev = worker;
    return true;
  } else {
    worker->next = pollset->root_worker;
    worker->prev = worker->next->prev;
    worker->next->prev = worker;
    worker->prev->next = worker;
    return false;
  }
}

/* Return true if last in list */
typedef enum { EMPTIED, NEW_ROOT, REMOVED } worker_remove_result;

static worker_remove_result worker_remove(grpc_pollset *pollset,
                                          grpc_pollset_worker *worker) {
  if (worker == pollset->root_worker) {
    if (worker == worker->next) {
      pollset->root_worker = NULL;
      return EMPTIED;
    } else {
      pollset->root_worker = worker->next;
      worker->prev->next = worker->next;
      worker->next->prev = worker->prev;
      return NEW_ROOT;
    }
  } else {
    worker->prev->next = worker->next;
    worker->next->prev = worker->prev;
    return REMOVED;
  }
}

static size_t choose_neighbourhood(void) {
  return (size_t)gpr_cpu_current_cpu() % g_num_neighbourhoods;
}

static grpc_error *pollset_global_init(void) {
  gpr_tls_init(&g_current_thread_pollset);
  gpr_tls_init(&g_current_thread_worker);
  gpr_atm_no_barrier_store(&g_active_poller, 0);
  global_wakeup_fd.read_fd = -1;
  grpc_error *err = grpc_wakeup_fd_init(&global_wakeup_fd);
  gpr_mu_init(&g_wq_mu);
  g_wq_items = (grpc_closure_list)GRPC_CLOSURE_LIST_INIT;
  if (err != GRPC_ERROR_NONE) return err;
  struct epoll_event ev = {.events = (uint32_t)(EPOLLIN | EPOLLET),
                           .data.ptr = &global_wakeup_fd};
  if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, global_wakeup_fd.read_fd, &ev) != 0) {
    return GRPC_OS_ERROR(errno, "epoll_ctl");
  }
  g_num_neighbourhoods = GPR_CLAMP(gpr_cpu_num_cores(), 1, MAX_NEIGHBOURHOODS);
  g_neighbourhoods =
      gpr_zalloc(sizeof(*g_neighbourhoods) * g_num_neighbourhoods);
  for (size_t i = 0; i < g_num_neighbourhoods; i++) {
    gpr_mu_init(&g_neighbourhoods[i].mu);
  }
  return GRPC_ERROR_NONE;
}

static void pollset_global_shutdown(void) {
  gpr_tls_destroy(&g_current_thread_pollset);
  gpr_tls_destroy(&g_current_thread_worker);
  gpr_mu_destroy(&g_wq_mu);
  if (global_wakeup_fd.read_fd != -1) grpc_wakeup_fd_destroy(&global_wakeup_fd);
  for (size_t i = 0; i < g_num_neighbourhoods; i++) {
    gpr_mu_destroy(&g_neighbourhoods[i].mu);
  }
  gpr_free(g_neighbourhoods);
}

static void pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  gpr_mu_init(&pollset->mu);
  *mu = &pollset->mu;
  pollset->neighbourhood = &g_neighbourhoods[choose_neighbourhood()];
  pollset->seen_inactive = true;
}

static void pollset_destroy(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset) {
  gpr_mu_lock(&pollset->mu);
  if (!pollset->seen_inactive) {
    pollset_neighbourhood *neighbourhood = pollset->neighbourhood;
    gpr_mu_unlock(&pollset->mu);
  retry_lock_neighbourhood:
    gpr_mu_lock(&neighbourhood->mu);
    gpr_mu_lock(&pollset->mu);
    if (!pollset->seen_inactive) {
      if (pollset->neighbourhood != neighbourhood) {
        gpr_mu_unlock(&neighbourhood->mu);
        neighbourhood = pollset->neighbourhood;
        gpr_mu_unlock(&pollset->mu);
        goto retry_lock_neighbourhood;
      }
      pollset->prev->next = pollset->next;
      pollset->next->prev = pollset->prev;
      if (pollset == pollset->neighbourhood->active_root) {
        pollset->neighbourhood->active_root =
            pollset->next == pollset ? NULL : pollset->next;
      }
    }
    gpr_mu_unlock(&pollset->neighbourhood->mu);
  }
  gpr_mu_unlock(&pollset->mu);
  gpr_mu_destroy(&pollset->mu);
}

static grpc_error *pollset_kick_all(grpc_pollset *pollset) {
  grpc_error *error = GRPC_ERROR_NONE;
  if (pollset->root_worker != NULL) {
    grpc_pollset_worker *worker = pollset->root_worker;
    do {
      if (worker->initialized_cv) {
        worker->kick_state = KICKED;
        gpr_cv_signal(&worker->cv);
      } else {
        worker->kick_state = KICKED;
        append_error(&error, grpc_wakeup_fd_wakeup(&global_wakeup_fd),
                     "pollset_shutdown");
      }

      worker = worker->next;
    } while (worker != pollset->root_worker);
  }
  return error;
}

static void pollset_maybe_finish_shutdown(grpc_exec_ctx *exec_ctx,
                                          grpc_pollset *pollset) {
  if (pollset->shutdown_closure != NULL && pollset->root_worker == NULL &&
      pollset->begin_refs == 0) {
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
      gpr_mu_lock(&g_wq_mu);
      grpc_closure_list_move(&g_wq_items, &exec_ctx->closure_list);
      gpr_mu_unlock(&g_wq_mu);
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
  if (worker_hdl != NULL) *worker_hdl = worker;
  worker->initialized_cv = false;
  worker->kick_state = UNKICKED;
  worker->schedule_on_end_work = (grpc_closure_list)GRPC_CLOSURE_LIST_INIT;
  pollset->begin_refs++;

  if (pollset->seen_inactive) {
    // pollset has been observed to be inactive, we need to move back to the
    // active list
    bool is_reassigning = false;
    if (!pollset->reassigning_neighbourhood) {
      is_reassigning = true;
      pollset->reassigning_neighbourhood = true;
      pollset->neighbourhood = &g_neighbourhoods[choose_neighbourhood()];
    }
    pollset_neighbourhood *neighbourhood = pollset->neighbourhood;
    gpr_mu_unlock(&pollset->mu);
  // pollset unlocked: state may change (even worker->kick_state)
  retry_lock_neighbourhood:
    gpr_mu_lock(&neighbourhood->mu);
    gpr_mu_lock(&pollset->mu);
    if (pollset->seen_inactive) {
      if (neighbourhood != pollset->neighbourhood) {
        gpr_mu_unlock(&neighbourhood->mu);
        neighbourhood = pollset->neighbourhood;
        gpr_mu_unlock(&pollset->mu);
        goto retry_lock_neighbourhood;
      }
      pollset->seen_inactive = false;
      if (neighbourhood->active_root == NULL) {
        neighbourhood->active_root = pollset->next = pollset->prev = pollset;
        if (gpr_atm_no_barrier_cas(&g_active_poller, 0, (gpr_atm)worker)) {
          worker->kick_state = DESIGNATED_POLLER;
        }
      } else {
        pollset->next = neighbourhood->active_root;
        pollset->prev = pollset->next->prev;
        pollset->next->prev = pollset->prev->next = pollset;
      }
    }
    if (is_reassigning) {
      GPR_ASSERT(pollset->reassigning_neighbourhood);
      pollset->reassigning_neighbourhood = false;
    }
    gpr_mu_unlock(&neighbourhood->mu);
  }
  worker_insert(pollset, worker);
  pollset->begin_refs--;
  if (worker->kick_state == UNKICKED) {
    GPR_ASSERT(gpr_atm_no_barrier_load(&g_active_poller) != (gpr_atm)worker);
    worker->initialized_cv = true;
    gpr_cv_init(&worker->cv);
    while (worker->kick_state == UNKICKED &&
           pollset->shutdown_closure == NULL) {
      if (gpr_cv_wait(&worker->cv, &pollset->mu, deadline) &&
          worker->kick_state == UNKICKED) {
        worker->kick_state = KICKED;
      }
    }
    *now = gpr_now(now->clock_type);
  }

  return worker->kick_state == DESIGNATED_POLLER &&
         pollset->shutdown_closure == NULL;
}

static bool check_neighbourhood_for_available_poller(
    pollset_neighbourhood *neighbourhood) {
  bool found_worker = false;
  do {
    grpc_pollset *inspect = neighbourhood->active_root;
    if (inspect == NULL) {
      break;
    }
    gpr_mu_lock(&inspect->mu);
    GPR_ASSERT(!inspect->seen_inactive);
    grpc_pollset_worker *inspect_worker = inspect->root_worker;
    if (inspect_worker != NULL) {
      do {
        switch (inspect_worker->kick_state) {
          case UNKICKED:
            if (gpr_atm_no_barrier_cas(&g_active_poller, 0,
                                       (gpr_atm)inspect_worker)) {
              inspect_worker->kick_state = DESIGNATED_POLLER;
              if (inspect_worker->initialized_cv) {
                gpr_cv_signal(&inspect_worker->cv);
              }
            }
            // even if we didn't win the cas, there's a worker, we can stop
            found_worker = true;
            break;
          case KICKED:
            break;
          case DESIGNATED_POLLER:
            found_worker = true;  // ok, so someone else found the worker, but
                                  // we'll accept that
            break;
        }
        inspect_worker = inspect_worker->next;
      } while (inspect_worker != inspect->root_worker);
    }
    if (!found_worker) {
      inspect->seen_inactive = true;
      if (inspect == neighbourhood->active_root) {
        neighbourhood->active_root =
            inspect->next == inspect ? NULL : inspect->next;
      }
      inspect->next->prev = inspect->prev;
      inspect->prev->next = inspect->next;
      inspect->next = inspect->prev = NULL;
    }
    gpr_mu_unlock(&inspect->mu);
  } while (!found_worker);
  return found_worker;
}

static void end_worker(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                       grpc_pollset_worker *worker,
                       grpc_pollset_worker **worker_hdl) {
  if (worker_hdl != NULL) *worker_hdl = NULL;
  worker->kick_state = KICKED;
  grpc_closure_list_move(&worker->schedule_on_end_work,
                         &exec_ctx->closure_list);
  if (gpr_atm_no_barrier_load(&g_active_poller) == (gpr_atm)worker) {
    if (worker->next != worker && worker->next->kick_state == UNKICKED) {
      GPR_ASSERT(worker->next->initialized_cv);
      gpr_atm_no_barrier_store(&g_active_poller, (gpr_atm)worker->next);
      worker->next->kick_state = DESIGNATED_POLLER;
      gpr_cv_signal(&worker->next->cv);
      if (grpc_exec_ctx_has_work(exec_ctx)) {
        gpr_mu_unlock(&pollset->mu);
        grpc_exec_ctx_flush(exec_ctx);
        gpr_mu_lock(&pollset->mu);
      }
    } else {
      gpr_atm_no_barrier_store(&g_active_poller, 0);
      gpr_mu_unlock(&pollset->mu);
      size_t poller_neighbourhood_idx =
          (size_t)(pollset->neighbourhood - g_neighbourhoods);
      bool found_worker = false;
      bool scan_state[MAX_NEIGHBOURHOODS];
      for (size_t i = 0; !found_worker && i < g_num_neighbourhoods; i++) {
        pollset_neighbourhood *neighbourhood =
            &g_neighbourhoods[(poller_neighbourhood_idx + i) %
                              g_num_neighbourhoods];
        if (gpr_mu_trylock(&neighbourhood->mu)) {
          found_worker =
              check_neighbourhood_for_available_poller(neighbourhood);
          gpr_mu_unlock(&neighbourhood->mu);
          scan_state[i] = true;
        } else {
          scan_state[i] = false;
        }
      }
      for (size_t i = 0; !found_worker && i < g_num_neighbourhoods; i++) {
        if (scan_state[i]) continue;
        pollset_neighbourhood *neighbourhood =
            &g_neighbourhoods[(poller_neighbourhood_idx + i) %
                              g_num_neighbourhoods];
        gpr_mu_lock(&neighbourhood->mu);
        found_worker = check_neighbourhood_for_available_poller(neighbourhood);
        gpr_mu_unlock(&neighbourhood->mu);
      }
      grpc_exec_ctx_flush(exec_ctx);
      gpr_mu_lock(&pollset->mu);
    }
  } else if (grpc_exec_ctx_has_work(exec_ctx)) {
    gpr_mu_unlock(&pollset->mu);
    grpc_exec_ctx_flush(exec_ctx);
    gpr_mu_lock(&pollset->mu);
  }
  if (worker->initialized_cv) {
    gpr_cv_destroy(&worker->cv);
  }
  if (EMPTIED == worker_remove(pollset, worker)) {
    pollset_maybe_finish_shutdown(exec_ctx, pollset);
  }
  GPR_ASSERT(gpr_atm_no_barrier_load(&g_active_poller) != (gpr_atm)worker);
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
  gpr_tls_set(&g_current_thread_pollset, (intptr_t)pollset);
  if (begin_worker(pollset, &worker, worker_hdl, &now, deadline)) {
    gpr_tls_set(&g_current_thread_worker, (intptr_t)&worker);
    GPR_ASSERT(!pollset->shutdown_closure);
    GPR_ASSERT(!pollset->seen_inactive);
    gpr_mu_unlock(&pollset->mu);
    append_error(&error, pollset_epoll(exec_ctx, pollset, now, deadline),
                 err_desc);
    gpr_mu_lock(&pollset->mu);
    gpr_tls_set(&g_current_thread_worker, 0);
  }
  end_worker(exec_ctx, pollset, &worker, worker_hdl);
  gpr_tls_set(&g_current_thread_pollset, 0);
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
      grpc_pollset_worker *next_worker = root_worker->next;
      if (root_worker == next_worker &&
          root_worker == (grpc_pollset_worker *)gpr_atm_no_barrier_load(
                             &g_active_poller)) {
        root_worker->kick_state = KICKED;
        return grpc_wakeup_fd_wakeup(&global_wakeup_fd);
      } else if (next_worker->kick_state == UNKICKED) {
        GPR_ASSERT(next_worker->initialized_cv);
        next_worker->kick_state = KICKED;
        gpr_cv_signal(&next_worker->cv);
        return GRPC_ERROR_NONE;
      } else {
        return GRPC_ERROR_NONE;
      }
    } else {
      return GRPC_ERROR_NONE;
    }
  } else if (specific_worker->kick_state == KICKED) {
    return GRPC_ERROR_NONE;
  } else if (gpr_tls_get(&g_current_thread_worker) ==
             (intptr_t)specific_worker) {
    specific_worker->kick_state = KICKED;
    return GRPC_ERROR_NONE;
  } else if (specific_worker ==
             (grpc_pollset_worker *)gpr_atm_no_barrier_load(&g_active_poller)) {
    specific_worker->kick_state = KICKED;
    return grpc_wakeup_fd_wakeup(&global_wakeup_fd);
  } else if (specific_worker->initialized_cv) {
    specific_worker->kick_state = KICKED;
    gpr_cv_signal(&specific_worker->cv);
    return GRPC_ERROR_NONE;
  } else {
    specific_worker->kick_state = KICKED;
    return GRPC_ERROR_NONE;
  }
}

static void pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_fd *fd) {}

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

static void wq_sched(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                     grpc_error *error) {
  // find a neighbourhood to wakeup
  bool scheduled = false;
  size_t initial_neighbourhood = choose_neighbourhood();
  for (size_t i = 0; !scheduled && i < g_num_neighbourhoods; i++) {
    pollset_neighbourhood *neighbourhood =
        &g_neighbourhoods[(initial_neighbourhood + i) % g_num_neighbourhoods];
    if (gpr_mu_trylock(&neighbourhood->mu)) {
      if (neighbourhood->active_root != NULL) {
        grpc_pollset *inspect = neighbourhood->active_root;
        do {
          if (gpr_mu_trylock(&inspect->mu)) {
            if (inspect->root_worker != NULL) {
              grpc_pollset_worker *inspect_worker = inspect->root_worker;
              do {
                if (inspect_worker->kick_state == UNKICKED) {
                  inspect_worker->kick_state = KICKED;
                  grpc_closure_list_append(
                      &inspect_worker->schedule_on_end_work, closure, error);
                  if (inspect_worker->initialized_cv) {
                    gpr_cv_signal(&inspect_worker->cv);
                  }
                  scheduled = true;
                }
                inspect_worker = inspect_worker->next;
              } while (!scheduled && inspect_worker != inspect->root_worker);
            }
            gpr_mu_unlock(&inspect->mu);
          }
          inspect = inspect->next;
        } while (!scheduled && inspect != neighbourhood->active_root);
      }
      gpr_mu_unlock(&neighbourhood->mu);
    }
  }
  if (!scheduled) {
    gpr_mu_lock(&g_wq_mu);
    grpc_closure_list_append(&g_wq_items, closure, error);
    gpr_mu_unlock(&g_wq_mu);
    GRPC_LOG_IF_ERROR("workqueue_scheduler",
                      grpc_wakeup_fd_wakeup(&global_wakeup_fd));
  }
}

static const grpc_closure_scheduler_vtable
    singleton_workqueue_scheduler_vtable = {wq_sched, wq_sched,
                                            "epoll1_workqueue"};

static grpc_closure_scheduler singleton_workqueue_scheduler = {
    &singleton_workqueue_scheduler_vtable};

static grpc_closure_scheduler *workqueue_scheduler(grpc_workqueue *workqueue) {
  return &singleton_workqueue_scheduler;
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

    .workqueue_ref = workqueue_ref,
    .workqueue_unref = workqueue_unref,
    .workqueue_scheduler = workqueue_scheduler,

    .shutdown_engine = shutdown_engine,
};

/* It is possible that GLIBC has epoll but the underlying kernel doesn't.
 * Create a dummy epoll_fd to make sure epoll support is available */
const grpc_event_engine_vtable *grpc_init_epoll1_linux(bool explicit_request) {
  /* TODO(ctiller): temporary, until this stabilizes */
  if (!explicit_request) return NULL;

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
