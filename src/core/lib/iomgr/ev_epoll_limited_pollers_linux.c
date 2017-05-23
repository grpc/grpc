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

#include "src/core/lib/iomgr/ev_epoll_limited_pollers_linux.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/tls.h>
#include <grpc/support/useful.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/lockfree_event.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/lib/iomgr/workqueue.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/block_annotate.h"
#include "src/core/lib/support/env.h"

#define GRPC_POLLING_TRACE(fmt, ...)        \
  if (GRPC_TRACER_ON(grpc_polling_trace)) { \
    gpr_log(GPR_INFO, (fmt), __VA_ARGS__);  \
  }

#define GRPC_POLLSET_KICK_BROADCAST ((grpc_pollset_worker *)1)

/* Uncomment the following to enable extra checks on poll_object operations */
/* #define PO_DEBUG */

/* The maximum number of polling threads per polling island. By default no
   limit */
static int g_max_pollers_per_pi = INT_MAX;

static int grpc_wakeup_signal = -1;
static bool is_grpc_wakeup_signal_initialized = false;

/* Implements the function defined in grpc_posix.h. This function might be
 * called before even calling grpc_init() to set either a different signal to
 * use. If signum == -1, then the use of signals is disabled */
static void grpc_use_signal(int signum) {
  grpc_wakeup_signal = signum;
  is_grpc_wakeup_signal_initialized = true;

  if (grpc_wakeup_signal < 0) {
    gpr_log(GPR_INFO,
            "Use of signals is disabled. Epoll engine will not be used");
  } else {
    gpr_log(GPR_INFO, "epoll engine will be using signal: %d",
            grpc_wakeup_signal);
  }
}

struct polling_island;

typedef enum {
  POLL_OBJ_FD,
  POLL_OBJ_POLLSET,
  POLL_OBJ_POLLSET_SET
} poll_obj_type;

typedef struct poll_obj {
#ifdef PO_DEBUG
  poll_obj_type obj_type;
#endif
  gpr_mu mu;
  struct polling_island *pi;
} poll_obj;

static const char *poll_obj_string(poll_obj_type po_type) {
  switch (po_type) {
    case POLL_OBJ_FD:
      return "fd";
    case POLL_OBJ_POLLSET:
      return "pollset";
    case POLL_OBJ_POLLSET_SET:
      return "pollset_set";
  }

  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

/*******************************************************************************
 * Fd Declarations
 */

#define FD_FROM_PO(po) ((grpc_fd *)(po))

struct grpc_fd {
  poll_obj po;

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

  /* The pollset that last noticed that the fd is readable. The actual type
   * stored in this is (grpc_pollset *) */
  gpr_atm read_notifier_pollset;

  grpc_iomgr_object iomgr_object;
};

/* Reference counting for fds */
// #define GRPC_FD_REF_COUNT_DEBUG
#ifdef GRPC_FD_REF_COUNT_DEBUG
static void fd_ref(grpc_fd *fd, const char *reason, const char *file, int line);
static void fd_unref(grpc_fd *fd, const char *reason, const char *file,
                     int line);
#define GRPC_FD_REF(fd, reason) fd_ref(fd, reason, __FILE__, __LINE__)
#define GRPC_FD_UNREF(fd, reason) fd_unref(fd, reason, __FILE__, __LINE__)
#else
static void fd_ref(grpc_fd *fd);
static void fd_unref(grpc_fd *fd);
#define GRPC_FD_REF(fd, reason) fd_ref(fd)
#define GRPC_FD_UNREF(fd, reason) fd_unref(fd)
#endif

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

typedef struct worker_node {
  struct worker_node *next;
  struct worker_node *prev;
} worker_node;

/* This is also used as grpc_workqueue (by directly casing it) */
typedef struct polling_island {
  grpc_closure_scheduler workqueue_scheduler;

  gpr_mu mu;
  /* Ref count. Use PI_ADD_REF() and PI_UNREF() macros to increment/decrement
     the refcount.
     Once the ref count becomes zero, this structure is destroyed which means
     we should ensure that there is never a scenario where a PI_ADD_REF() is
     racing with a PI_UNREF() that just made the ref_count zero. */
  gpr_atm ref_count;

  /* Pointer to the polling_island this merged into.
   * merged_to value is only set once in polling_island's lifetime (and that too
   * only if the island is merged with another island). Because of this, we can
   * use gpr_atm type here so that we can do atomic access on this and reduce
   * lock contention on 'mu' mutex.
   *
   * Note that if this field is not NULL (i.e not 0), all the remaining fields
   * (except mu and ref_count) are invalid and must be ignored. */
  gpr_atm merged_to;

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

  /* The list of workers waiting to do polling on this polling island */
  gpr_mu worker_list_mu;
  worker_node worker_list_head;

  /* The fd of the underlying epoll set */
  int epoll_fd;

  /* The file descriptors in the epoll set */
  size_t fd_cnt;
  size_t fd_capacity;
  grpc_fd **fds;
} polling_island;

/*******************************************************************************
 * Pollset Declarations
 */
#define WORKER_FROM_WORKER_LIST_NODE(p)          \
  (struct grpc_pollset_worker *)(((char *)(p)) - \
                                 offsetof(grpc_pollset_worker, pi_list_link))
struct grpc_pollset_worker {
  /* Thread id of this worker */
  pthread_t pt_id;

  /* Used to prevent a worker from getting kicked multiple times */
  gpr_atm is_kicked;

  struct grpc_pollset_worker *next;
  struct grpc_pollset_worker *prev;

  /* Indicates if it is this worker's turn to do epoll */
  gpr_atm is_polling_turn;

  /* Node in the polling island's worker list. */
  worker_node pi_list_link;
};

struct grpc_pollset {
  poll_obj po;

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
  poll_obj po;
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
static void polling_island_delete(grpc_exec_ctx *exec_ctx, polling_island *pi);
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
  /* If ref count went to zero, delete the polling island.
     Note that this deletion not be done under a lock. Once the ref count goes
     to zero, we are guaranteed that no one else holds a reference to the
     polling island (and that there is no racing pi_add_ref() call either).

     Also, if we are deleting the polling island and the merged_to field is
     non-empty, we should remove a ref to the merged_to polling island
   */
  if (1 == gpr_atm_full_fetch_add(&pi->ref_count, -1)) {
    polling_island *next = (polling_island *)gpr_atm_acq_load(&pi->merged_to);
    polling_island_delete(exec_ctx, pi);
    if (next != NULL) {
      PI_UNREF(exec_ctx, next, "pi_delete"); /* Recursive call */
    }
  }
}

static void worker_node_init(worker_node *node) {
  node->next = node->prev = node;
}

/* Not thread safe. Do under a list-level lock */
static void push_back_worker_node(worker_node *head, worker_node *node) {
  node->next = head;
  node->prev = head->prev;
  head->prev->next = node;
  head->prev = node;
}

/* Not thread safe. Do under a list-level lock */
static void remove_worker_node(worker_node *node) {
  node->next->prev = node->prev;
  node->prev->next = node->next;
  /* If node's next and prev point to itself, the node is considered detached
   * from the list*/
  node->next = node->prev = node;
}

/* Not thread safe. Do under a list-level lock */
static worker_node *pop_front_worker_node(worker_node *head) {
  worker_node *node = head->next;
  if (node != head) {
    remove_worker_node(node);
  } else {
    node = NULL;
  }

  return node;
}

/* Returns true if the node's next and prev are pointing to itself (which
   indicates that the node is not in the list */
static bool is_worker_node_detached(worker_node *node) {
  return (node->next == node->prev && node->next == node);
}

/* The caller is expected to hold pi->mu lock before calling this function
 */
static void polling_island_add_fds_locked(polling_island *pi, grpc_fd **fds,
                                          size_t fd_count, bool add_fd_refs,
                                          grpc_error **error) {
  int err;
  size_t i;
  struct epoll_event ev;
  char *err_msg;
  const char *err_desc = "polling_island_add_fds";

#ifdef GRPC_TSAN
  /* See the definition of g_epoll_sync for more context */
  gpr_atm_rel_store(&g_epoll_sync, (gpr_atm)0);
#endif /* defined(GRPC_TSAN) */

  for (i = 0; i < fd_count; i++) {
    ev.events = (uint32_t)(EPOLLIN | EPOLLOUT | EPOLLET);
    ev.data.ptr = fds[i];
    err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_ADD, fds[i]->fd, &ev);

    if (err < 0) {
      if (errno != EEXIST) {
        gpr_asprintf(
            &err_msg,
            "epoll_ctl (epoll_fd: %d) add fd: %d failed with error: %d (%s)",
            pi->epoll_fd, fds[i]->fd, errno, strerror(errno));
        append_error(error, GRPC_OS_ERROR(errno, err_msg), err_desc);
        gpr_free(err_msg);
      }

      continue;
    }

    if (pi->fd_cnt == pi->fd_capacity) {
      pi->fd_capacity = GPR_MAX(pi->fd_capacity + 8, pi->fd_cnt * 3 / 2);
      pi->fds = gpr_realloc(pi->fds, sizeof(grpc_fd *) * pi->fd_capacity);
    }

    pi->fds[pi->fd_cnt++] = fds[i];
    if (add_fd_refs) {
      GRPC_FD_REF(fds[i], "polling_island");
    }
  }
}

/* The caller is expected to hold pi->mu before calling this */
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
                 pi->epoll_fd, GRPC_WAKEUP_FD_GET_READ_FD(wakeup_fd), errno,
                 strerror(errno));
    append_error(error, GRPC_OS_ERROR(errno, err_msg), err_desc);
    gpr_free(err_msg);
  }
}

/* The caller is expected to hold pi->mu lock before calling this function */
static void polling_island_remove_all_fds_locked(polling_island *pi,
                                                 bool remove_fd_refs,
                                                 grpc_error **error) {
  int err;
  size_t i;
  char *err_msg;
  const char *err_desc = "polling_island_remove_fds";

  for (i = 0; i < pi->fd_cnt; i++) {
    err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_DEL, pi->fds[i]->fd, NULL);
    if (err < 0 && errno != ENOENT) {
      gpr_asprintf(&err_msg,
                   "epoll_ctl (epoll_fd: %d) delete fds[%zu]: %d failed with "
                   "error: %d (%s)",
                   pi->epoll_fd, i, pi->fds[i]->fd, errno, strerror(errno));
      append_error(error, GRPC_OS_ERROR(errno, err_msg), err_desc);
      gpr_free(err_msg);
    }

    if (remove_fd_refs) {
      GRPC_FD_UNREF(pi->fds[i], "polling_island");
    }
  }

  pi->fd_cnt = 0;
}

/* The caller is expected to hold pi->mu lock before calling this function */
static void polling_island_remove_fd_locked(polling_island *pi, grpc_fd *fd,
                                            bool is_fd_closed,
                                            grpc_error **error) {
  int err;
  size_t i;
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

  for (i = 0; i < pi->fd_cnt; i++) {
    if (pi->fds[i] == fd) {
      pi->fds[i] = pi->fds[--pi->fd_cnt];
      GRPC_FD_UNREF(fd, "polling_island");
      break;
    }
  }
}

/* Might return NULL in case of an error */
static polling_island *polling_island_create(grpc_exec_ctx *exec_ctx,
                                             grpc_fd *initial_fd,
                                             grpc_error **error) {
  polling_island *pi = NULL;
  const char *err_desc = "polling_island_create";

  *error = GRPC_ERROR_NONE;

  pi = gpr_malloc(sizeof(*pi));
  pi->workqueue_scheduler.vtable = &workqueue_scheduler_vtable;
  gpr_mu_init(&pi->mu);
  pi->fd_cnt = 0;
  pi->fd_capacity = 0;
  pi->fds = NULL;
  pi->epoll_fd = -1;

  gpr_mu_init(&pi->workqueue_read_mu);
  gpr_mpscq_init(&pi->workqueue_items);
  gpr_atm_rel_store(&pi->workqueue_item_count, 0);

  gpr_atm_rel_store(&pi->ref_count, 0);
  gpr_atm_rel_store(&pi->poller_count, 0);
  gpr_atm_rel_store(&pi->merged_to, (gpr_atm)NULL);

  gpr_mu_init(&pi->worker_list_mu);
  worker_node_init(&pi->worker_list_head);

  if (!append_error(error, grpc_wakeup_fd_init(&pi->workqueue_wakeup_fd),
                    err_desc)) {
    goto done;
  }

  pi->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

  if (pi->epoll_fd < 0) {
    append_error(error, GRPC_OS_ERROR(errno, "epoll_create1"), err_desc);
    goto done;
  }

  polling_island_add_wakeup_fd_locked(pi, &pi->workqueue_wakeup_fd, error);

  if (initial_fd != NULL) {
    polling_island_add_fds_locked(pi, &initial_fd, 1, true, error);
  }

done:
  if (*error != GRPC_ERROR_NONE) {
    polling_island_delete(exec_ctx, pi);
    pi = NULL;
  }
  return pi;
}

static void polling_island_delete(grpc_exec_ctx *exec_ctx, polling_island *pi) {
  GPR_ASSERT(pi->fd_cnt == 0);

  if (pi->epoll_fd >= 0) {
    close(pi->epoll_fd);
  }
  GPR_ASSERT(gpr_atm_no_barrier_load(&pi->workqueue_item_count) == 0);
  gpr_mu_destroy(&pi->workqueue_read_mu);
  gpr_mpscq_destroy(&pi->workqueue_items);
  gpr_mu_destroy(&pi->mu);
  grpc_wakeup_fd_destroy(&pi->workqueue_wakeup_fd);
  gpr_mu_destroy(&pi->worker_list_mu);
  GPR_ASSERT(is_worker_node_detached(&pi->worker_list_head));

  gpr_free(pi->fds);
  gpr_free(pi);
}

/* Attempts to gets the last polling island in the linked list (liked by the
 * 'merged_to' field). Since this does not lock the polling island, there are no
 * guarantees that the island returned is the last island */
static polling_island *polling_island_maybe_get_latest(polling_island *pi) {
  polling_island *next = (polling_island *)gpr_atm_acq_load(&pi->merged_to);
  while (next != NULL) {
    pi = next;
    next = (polling_island *)gpr_atm_acq_load(&pi->merged_to);
  }

  return pi;
}

/* Gets the lock on the *latest* polling island i.e the last polling island in
   the linked list (linked by the 'merged_to' field). Call gpr_mu_unlock on the
   returned polling island's mu.
   Usage: To lock/unlock polling island "pi", do the following:
      polling_island *pi_latest = polling_island_lock(pi);
      ...
      ... critical section ..
      ...
      gpr_mu_unlock(&pi_latest->mu); // NOTE: use pi_latest->mu. NOT pi->mu */
static polling_island *polling_island_lock(polling_island *pi) {
  polling_island *next = NULL;

  while (true) {
    next = (polling_island *)gpr_atm_acq_load(&pi->merged_to);
    if (next == NULL) {
      /* Looks like 'pi' is the last node in the linked list but unless we check
         this by holding the pi->mu lock, we cannot be sure (i.e without the
         pi->mu lock, we don't prevent island merges).
         To be absolutely sure, check once more by holding the pi->mu lock */
      gpr_mu_lock(&pi->mu);
      next = (polling_island *)gpr_atm_acq_load(&pi->merged_to);
      if (next == NULL) {
        /* pi is infact the last node and we have the pi->mu lock. we're done */
        break;
      }

      /* pi->merged_to is not NULL i.e pi isn't the last node anymore. pi->mu
       * isn't the lock we are interested in. Continue traversing the list */
      gpr_mu_unlock(&pi->mu);
    }

    pi = next;
  }

  return pi;
}

/* Gets the lock on the *latest* polling islands in the linked lists pointed by
   *p and *q (and also updates *p and *q to point to the latest polling islands)

   This function is needed because calling the following block of code to obtain
   locks on polling islands (*p and *q) is prone to deadlocks.
     {
       polling_island_lock(*p, true);
       polling_island_lock(*q, true);
     }

   Usage/example:
     polling_island *p1;
     polling_island *p2;
     ..
     polling_island_lock_pair(&p1, &p2);
     ..
     .. Critical section with both p1 and p2 locked
     ..
     // Release locks: Always call polling_island_unlock_pair() to release locks
     polling_island_unlock_pair(p1, p2);
*/
static void polling_island_lock_pair(polling_island **p, polling_island **q) {
  polling_island *pi_1 = *p;
  polling_island *pi_2 = *q;
  polling_island *next_1 = NULL;
  polling_island *next_2 = NULL;

  /* The algorithm is simple:
      - Go to the last polling islands in the linked lists *pi_1 and *pi_2 (and
        keep updating pi_1 and pi_2)
      - Then obtain locks on the islands by following a lock order rule of
        locking polling_island with lower address first
           Special case: Before obtaining the locks, check if pi_1 and pi_2 are
           pointing to the same island. If that is the case, we can just call
           polling_island_lock()
      - After obtaining both the locks, double check that the polling islands
        are still the last polling islands in their respective linked lists
        (this is because there might have been polling island merges before
        we got the lock)
      - If the polling islands are the last islands, we are done. If not,
        release the locks and continue the process from the first step */
  while (true) {
    next_1 = (polling_island *)gpr_atm_acq_load(&pi_1->merged_to);
    while (next_1 != NULL) {
      pi_1 = next_1;
      next_1 = (polling_island *)gpr_atm_acq_load(&pi_1->merged_to);
    }

    next_2 = (polling_island *)gpr_atm_acq_load(&pi_2->merged_to);
    while (next_2 != NULL) {
      pi_2 = next_2;
      next_2 = (polling_island *)gpr_atm_acq_load(&pi_2->merged_to);
    }

    if (pi_1 == pi_2) {
      pi_1 = pi_2 = polling_island_lock(pi_1);
      break;
    }

    if (pi_1 < pi_2) {
      gpr_mu_lock(&pi_1->mu);
      gpr_mu_lock(&pi_2->mu);
    } else {
      gpr_mu_lock(&pi_2->mu);
      gpr_mu_lock(&pi_1->mu);
    }

    next_1 = (polling_island *)gpr_atm_acq_load(&pi_1->merged_to);
    next_2 = (polling_island *)gpr_atm_acq_load(&pi_2->merged_to);
    if (next_1 == NULL && next_2 == NULL) {
      break;
    }

    gpr_mu_unlock(&pi_1->mu);
    gpr_mu_unlock(&pi_2->mu);
  }

  *p = pi_1;
  *q = pi_2;
}

static void polling_island_unlock_pair(polling_island *p, polling_island *q) {
  if (p == q) {
    gpr_mu_unlock(&p->mu);
  } else {
    gpr_mu_unlock(&p->mu);
    gpr_mu_unlock(&q->mu);
  }
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

static void workqueue_move_items_to_parent(polling_island *q) {
  polling_island *p = (polling_island *)gpr_atm_no_barrier_load(&q->merged_to);
  if (p == NULL) {
    return;
  }
  gpr_mu_lock(&q->workqueue_read_mu);
  int num_added = 0;
  while (gpr_atm_no_barrier_load(&q->workqueue_item_count) > 0) {
    gpr_mpscq_node *n = gpr_mpscq_pop(&q->workqueue_items);
    if (n != NULL) {
      gpr_atm_no_barrier_fetch_add(&q->workqueue_item_count, -1);
      gpr_atm_no_barrier_fetch_add(&p->workqueue_item_count, 1);
      gpr_mpscq_push(&p->workqueue_items, n);
      num_added++;
    }
  }
  gpr_mu_unlock(&q->workqueue_read_mu);
  if (num_added > 0) {
    workqueue_maybe_wakeup(p);
  }
  workqueue_move_items_to_parent(p);
}

static polling_island *polling_island_merge(polling_island *p,
                                            polling_island *q,
                                            grpc_error **error) {
  /* Get locks on both the polling islands */
  polling_island_lock_pair(&p, &q);

  if (p != q) {
    /* Make sure that p points to the polling island with fewer fds than q */
    if (p->fd_cnt > q->fd_cnt) {
      GPR_SWAP(polling_island *, p, q);
    }

    /* Merge p with q i.e move all the fds from p (The one with fewer fds) to q
       Note that the refcounts on the fds being moved will not change here.
       This is why the last param in the following two functions is 'false') */
    polling_island_add_fds_locked(q, p->fds, p->fd_cnt, false, error);
    polling_island_remove_all_fds_locked(p, false, error);

    /* Wakeup all the pollers (if any) on p so that they pickup this change */
    polling_island_add_wakeup_fd_locked(p, &polling_island_wakeup_fd, error);

    /* Add the 'merged_to' link from p --> q */
    gpr_atm_rel_store(&p->merged_to, (gpr_atm)q);
    PI_ADD_REF(q, "pi_merge"); /* To account for the new incoming ref from p */

    workqueue_move_items_to_parent(p);
  }
  /* else if p == q, nothing needs to be done */

  polling_island_unlock_pair(p, q);

  /* Return the merged polling island (Note that no merge would have happened
     if p == q which is ok) */
  return q;
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
  workqueue_move_items_to_parent(pi);
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

/* Increment refcount by two to avoid changing the orphan bit */
#ifdef GRPC_FD_REF_COUNT_DEBUG
static void fd_ref(grpc_fd *fd, const char *reason, const char *file,
                   int line) {
  ref_by(fd, 2, reason, file, line);
}

static void fd_unref(grpc_fd *fd, const char *reason, const char *file,
                     int line) {
  unref_by(fd, 2, reason, file, line);
}
#else
static void fd_ref(grpc_fd *fd) { ref_by(fd, 2); }
static void fd_unref(grpc_fd *fd) { unref_by(fd, 2); }
#endif

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
    gpr_mu_init(&new_fd->po.mu);
  }

  /* Note: It is not really needed to get the new_fd->po.mu lock here. If this
   * is a newly created fd (or an fd we got from the freelist), no one else
   * would be holding a lock to it anyway. */
  gpr_mu_lock(&new_fd->po.mu);
  new_fd->po.pi = NULL;
#ifdef PO_DEBUG
  new_fd->po.obj_type = POLL_OBJ_FD;
#endif

  gpr_atm_rel_store(&new_fd->refst, (gpr_atm)1);
  new_fd->fd = fd;
  new_fd->orphaned = false;
  grpc_lfev_init(&new_fd->read_closure);
  grpc_lfev_init(&new_fd->write_closure);
  gpr_atm_no_barrier_store(&new_fd->read_notifier_pollset, (gpr_atm)NULL);

  new_fd->freelist_next = NULL;
  new_fd->on_done_closure = NULL;

  gpr_mu_unlock(&new_fd->po.mu);

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
  polling_island *unref_pi = NULL;

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

  /* Remove the active status but keep referenced. We want this grpc_fd struct
     to be alive (and not added to freelist) until the end of this function */
  REF_BY(fd, 1, reason);

  /* Remove the fd from the polling island:
     - Get a lock on the latest polling island (i.e the last island in the
       linked list pointed by fd->po.pi). This is the island that
       would actually contain the fd
     - Remove the fd from the latest polling island
     - Unlock the latest polling island
     - Set fd->po.pi to NULL (but remove the ref on the polling island
       before doing this.) */
  if (fd->po.pi != NULL) {
    polling_island *pi_latest = polling_island_lock(fd->po.pi);
    polling_island_remove_fd_locked(pi_latest, fd, is_fd_closed, &error);
    gpr_mu_unlock(&pi_latest->mu);

    unref_pi = fd->po.pi;
    fd->po.pi = NULL;
  }

  grpc_closure_sched(exec_ctx, fd->on_done_closure, GRPC_ERROR_REF(error));

  gpr_mu_unlock(&fd->po.mu);
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

static grpc_workqueue *fd_get_workqueue(grpc_fd *fd) {
  gpr_mu_lock(&fd->po.mu);
  grpc_workqueue *workqueue =
      GRPC_WORKQUEUE_REF((grpc_workqueue *)fd->po.pi, "fd_get_workqueue");
  gpr_mu_unlock(&fd->po.mu);
  return workqueue;
}

/*******************************************************************************
 * Pollset Definitions
 */
GPR_TLS_DECL(g_current_thread_pollset);
GPR_TLS_DECL(g_current_thread_worker);
static __thread bool g_initialized_sigmask;
static __thread sigset_t g_orig_sigmask;
static __thread sigset_t g_wakeup_sig_set;

static void sig_handler(int sig_num) {
#ifdef GRPC_EPOLL_DEBUG
  gpr_log(GPR_INFO, "Received signal %d", sig_num);
#endif
}

static void pollset_worker_init(grpc_pollset_worker *worker) {
  worker->pt_id = pthread_self();
  worker->next = worker->prev = NULL;
  gpr_atm_no_barrier_store(&worker->is_kicked, (gpr_atm)0);
  gpr_atm_no_barrier_store(&worker->is_polling_turn, (gpr_atm)0);
  worker_node_init(&worker->pi_list_link);
}

static void poller_kick_init() { signal(grpc_wakeup_signal, sig_handler); }

/* Global state management */
static grpc_error *pollset_global_init(void) {
  gpr_tls_init(&g_current_thread_pollset);
  gpr_tls_init(&g_current_thread_worker);
  poller_kick_init();
  return GRPC_ERROR_NONE;
}

static void pollset_global_shutdown(void) {
  gpr_tls_destroy(&g_current_thread_pollset);
  gpr_tls_destroy(&g_current_thread_worker);
}

static grpc_error *worker_kick(grpc_pollset_worker *worker,
                               gpr_atm *is_kicked) {
  grpc_error *err = GRPC_ERROR_NONE;

  /* Kick the worker only if it was not already kicked */
  if (gpr_atm_no_barrier_cas(is_kicked, (gpr_atm)0, (gpr_atm)1)) {
    GRPC_POLLING_TRACE(
        "pollset_worker_kick: Kicking worker: %p (thread id: %ld)",
        (void *)worker, (long int)worker->pt_id);
    int err_num = pthread_kill(worker->pt_id, grpc_wakeup_signal);
    if (err_num != 0) {
      err = GRPC_OS_ERROR(err_num, "pthread_kill");
    }
  }
  return err;
}

static grpc_error *pollset_worker_kick(grpc_pollset_worker *worker) {
  return worker_kick(worker, &worker->is_kicked);
}

static grpc_error *poller_kick(grpc_pollset_worker *worker) {
  return worker_kick(worker, &worker->is_polling_turn);
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

static void pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  gpr_mu_init(&pollset->po.mu);
  *mu = &pollset->po.mu;
  pollset->po.pi = NULL;
#ifdef PO_DEBUG
  pollset->po.obj_type = POLL_OBJ_POLLSET;
#endif

  pollset->root_worker.next = pollset->root_worker.prev = &pollset->root_worker;
  pollset->kicked_without_pollers = false;

  pollset->shutting_down = false;
  pollset->finish_shutdown_called = false;
  pollset->shutdown_done = NULL;
}

/* Convert millis to timespec (clock-type is assumed to be GPR_TIMESPAN) */
static struct timespec millis_to_timespec(int millis) {
  struct timespec linux_ts;
  gpr_timespec gpr_ts;

  if (millis == -1) {
    gpr_ts = gpr_inf_future(GPR_TIMESPAN);
  } else {
    gpr_ts = gpr_time_from_millis(millis, GPR_TIMESPAN);
  }

  linux_ts.tv_sec = (time_t)gpr_ts.tv_sec;
  linux_ts.tv_nsec = gpr_ts.tv_nsec;
  return linux_ts;
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

static void pollset_release_polling_island(grpc_exec_ctx *exec_ctx,
                                           grpc_pollset *ps, char *reason) {
  if (ps->po.pi != NULL) {
    PI_UNREF(exec_ctx, ps->po.pi, reason);
  }
  ps->po.pi = NULL;
}

static void finish_shutdown_locked(grpc_exec_ctx *exec_ctx,
                                   grpc_pollset *pollset) {
  /* The pollset cannot have any workers if we are at this stage */
  GPR_ASSERT(!pollset_has_workers(pollset));

  pollset->finish_shutdown_called = true;

  /* Release the ref and set pollset->po.pi to NULL */
  pollset_release_polling_island(exec_ctx, pollset, "ps_shutdown");
  grpc_closure_sched(exec_ctx, pollset->shutdown_done, GRPC_ERROR_NONE);
}

/* pollset->po.mu lock must be held by the caller before calling this */
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
static void pollset_destroy(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset) {
  GPR_ASSERT(!pollset_has_workers(pollset));
  gpr_mu_destroy(&pollset->po.mu);
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

/* NOTE: This function may modify 'now' */
static bool acquire_polling_lease(grpc_pollset_worker *worker,
                                  polling_island *pi, gpr_timespec deadline,
                                  gpr_timespec *now) {
  bool is_lease_acquired = false;

  gpr_mu_lock(&pi->worker_list_mu);  //  LOCK
  long num_pollers = gpr_atm_no_barrier_load(&pi->poller_count);

  if (num_pollers >= g_max_pollers_per_pi) {
    push_back_worker_node(&pi->worker_list_head, &worker->pi_list_link);
    gpr_mu_unlock(&pi->worker_list_mu);  // UNLOCK

    bool is_timeout = false;
    int ret;
    int timeout_ms = poll_deadline_to_millis_timeout(deadline, *now);
    if (timeout_ms == -1) {
      ret = sigwaitinfo(&g_wakeup_sig_set, NULL);
    } else {
      struct timespec sigwait_timeout = millis_to_timespec(timeout_ms);
      GRPC_SCHEDULING_START_BLOCKING_REGION;
      ret = sigtimedwait(&g_wakeup_sig_set, NULL, &sigwait_timeout);
      GRPC_SCHEDULING_END_BLOCKING_REGION;
    }

    if (ret == -1) {
      if (errno == EAGAIN) {
        is_timeout = true;
      } else {
        /* NOTE: This should not happen. If we see these log messages, it means
           we are most likely doing something incorrect in the setup * needed
           for sigwaitinfo/sigtimedwait */
        gpr_log(GPR_ERROR,
                "sigtimedwait failed with retcode: %d (timeout_ms: %d)", errno,
                timeout_ms);
      }
    }

    /* Did the worker come out of sigtimedwait due to a thread that just
       exited epoll and kicking it (in release_polling_lease function). */
    bool is_polling_turn = gpr_atm_acq_load(&worker->is_polling_turn);

    /* Did the worker come out of sigtimedwait due to a thread alerting it that
       some completion event was (likely) available in the completion queue */
    bool is_kicked = gpr_atm_no_barrier_load(&worker->is_kicked);

    if (is_kicked || is_timeout) {
      *now = deadline; /* Essentially make the epoll timeout = 0 */
    } else if (is_polling_turn) {
      *now = gpr_now(GPR_CLOCK_MONOTONIC); /* Reduce the epoll timeout */
    }

    gpr_mu_lock(&pi->worker_list_mu);  // LOCK
    /* The node might have already been removed from the list by the poller
       that kicked this. However it is safe to call 'remove_worker_node' on
       an already detached node */
    remove_worker_node(&worker->pi_list_link);
    /* It is important to read the num_pollers again under the lock so that we
     * have the latest num_pollers value that doesn't change while we are doing
     * the "(num_pollers < g_max_pollers_per_pi)" a a few lines below */
    num_pollers = gpr_atm_no_barrier_load(&pi->poller_count);
  }

  if (num_pollers < g_max_pollers_per_pi) {
    gpr_atm_no_barrier_fetch_add(&pi->poller_count, 1);
    is_lease_acquired = true;
  }

  gpr_mu_unlock(&pi->worker_list_mu);  // UNLOCK
  return is_lease_acquired;
}

static void release_polling_lease(polling_island *pi, grpc_error **error) {
  gpr_mu_lock(&pi->worker_list_mu);

  gpr_atm_no_barrier_fetch_add(&pi->poller_count, -1);
  worker_node *node = pop_front_worker_node(&pi->worker_list_head);
  if (node != NULL) {
    grpc_pollset_worker *next_worker = WORKER_FROM_WORKER_LIST_NODE(node);
    append_error(error, poller_kick(next_worker), "poller kick error");
  }

  gpr_mu_unlock(&pi->worker_list_mu);
}

#define GRPC_EPOLL_MAX_EVENTS 100
static void pollset_do_epoll_pwait(grpc_exec_ctx *exec_ctx, int epoll_fd,
                                   grpc_pollset *pollset, polling_island *pi,
                                   grpc_pollset_worker *worker,
                                   gpr_timespec now, gpr_timespec deadline,
                                   sigset_t *sig_mask, grpc_error **error) {
  /* Only g_max_pollers_per_pi threads can be doing polling in parallel.
     If we cannot get a lease, we cannot continue to do epoll_pwait() */
  if (!acquire_polling_lease(worker, pi, deadline, &now)) {
    return;
  }

  struct epoll_event ep_ev[GRPC_EPOLL_MAX_EVENTS];
  int ep_rv;
  char *err_msg;
  const char *err_desc = "pollset_work_and_unlock";

  /* timeout_ms is the time between 'now' and 'deadline' */
  int timeout_ms = poll_deadline_to_millis_timeout(deadline, now);

  GRPC_SCHEDULING_START_BLOCKING_REGION;
  ep_rv =
      epoll_pwait(epoll_fd, ep_ev, GRPC_EPOLL_MAX_EVENTS, timeout_ms, sig_mask);
  GRPC_SCHEDULING_END_BLOCKING_REGION;

  /* Give back the lease right away so that some other thread can enter */
  release_polling_lease(pi, error);

  if (ep_rv < 0) {
    if (errno != EINTR) {
      gpr_asprintf(&err_msg,
                   "epoll_wait() epoll fd: %d failed with error: %d (%s)",
                   epoll_fd, errno, strerror(errno));
      append_error(error, GRPC_OS_ERROR(errno, err_msg), err_desc);
    } else {
      /* We were interrupted. Save an interation by doing a zero timeout
         epoll_wait to see if there are any other events of interest */
      GRPC_POLLING_TRACE("pollset_work: pollset: %p, worker: %p received kick",
                         (void *)pollset, (void *)worker);
      ep_rv = epoll_wait(epoll_fd, ep_ev, GRPC_EPOLL_MAX_EVENTS, 0);
    }
  }

#ifdef GRPC_TSAN
  /* See the definition of g_poll_sync for more details */
  gpr_atm_acq_load(&g_epoll_sync);
#endif /* defined(GRPC_TSAN) */

  for (int i = 0; i < ep_rv; ++i) {
    void *data_ptr = ep_ev[i].data.ptr;
    if (data_ptr == &pi->workqueue_wakeup_fd) {
      append_error(error,
                   grpc_wakeup_fd_consume_wakeup(&pi->workqueue_wakeup_fd),
                   err_desc);
      maybe_do_workqueue_work(exec_ctx, pi);
    } else if (data_ptr == &polling_island_wakeup_fd) {
      GRPC_POLLING_TRACE(
          "pollset_work: pollset: %p, worker: %p polling island (epoll_fd: "
          "%d) got merged",
          (void *)pollset, (void *)worker, epoll_fd);
      /* This means that our polling island is merged with a different
         island. We do not have to do anything here since the subsequent call
         to the function pollset_work_and_unlock() will pick up the correct
         epoll_fd */
    } else {
      grpc_fd *fd = data_ptr;
      int cancel = ep_ev[i].events & (EPOLLERR | EPOLLHUP);
      int read_ev = ep_ev[i].events & (EPOLLIN | EPOLLPRI);
      int write_ev = ep_ev[i].events & EPOLLOUT;
      if (read_ev || cancel) {
        fd_become_readable(exec_ctx, fd, pollset);
      }
      if (write_ev || cancel) {
        fd_become_writable(exec_ctx, fd);
      }
    }
  }
}

/* Note: sig_mask contains the signal mask to use *during* epoll_wait() */
static void pollset_work_and_unlock(grpc_exec_ctx *exec_ctx,
                                    grpc_pollset *pollset,
                                    grpc_pollset_worker *worker,
                                    gpr_timespec now, gpr_timespec deadline,
                                    sigset_t *sig_mask, grpc_error **error) {
  int epoll_fd = -1;
  polling_island *pi = NULL;
  GPR_TIMER_BEGIN("pollset_work_and_unlock", 0);

  /* We need to get the epoll_fd to wait on. The epoll_fd is in inside the
     latest polling island pointed by pollset->po.pi

     Since epoll_fd is immutable, it is safe to read it without a lock on the
     polling island. There is however a possibility that the polling island from
     which we got the epoll_fd, got merged with another island in the meantime.
     This is okay because in such a case, we will wakeup right-away from
     epoll_pwait() (because any merge will poison the old polling island's epoll
     set 'polling_island_wakeup_fd') and then pick up the latest polling_island
     the next time this function - pollset_work_and_unlock()) is called */

  if (pollset->po.pi == NULL) {
    pollset->po.pi = polling_island_create(exec_ctx, NULL, error);
    if (pollset->po.pi == NULL) {
      GPR_TIMER_END("pollset_work_and_unlock", 0);
      return; /* Fatal error. Cannot continue */
    }

    PI_ADD_REF(pollset->po.pi, "ps");
    GRPC_POLLING_TRACE("pollset_work: pollset: %p created new pi: %p",
                       (void *)pollset, (void *)pollset->po.pi);
  }

  pi = polling_island_maybe_get_latest(pollset->po.pi);
  epoll_fd = pi->epoll_fd;

  /* Update the pollset->po.pi since the island being pointed by
     pollset->po.pi maybe older than the one pointed by pi) */
  if (pollset->po.pi != pi) {
    /* Always do PI_ADD_REF before PI_UNREF because PI_UNREF may cause the
       polling island to be deleted */
    PI_ADD_REF(pi, "ps");
    PI_UNREF(exec_ctx, pollset->po.pi, "ps");
    pollset->po.pi = pi;
  }

  /* Add an extra ref so that the island does not get destroyed (which means
     the epoll_fd won't be closed) while we are are doing an epoll_wait() on the
     epoll_fd */
  PI_ADD_REF(pi, "ps_work");
  gpr_mu_unlock(&pollset->po.mu);

  /* If we get some workqueue work to do, it might end up completing an item on
     the completion queue, so there's no need to poll... so we skip that and
     redo the complete loop to verify */
  if (!maybe_do_workqueue_work(exec_ctx, pi)) {
    g_current_thread_polling_island = pi;
    pollset_do_epoll_pwait(exec_ctx, epoll_fd, pollset, pi, worker, now,
                           deadline, sig_mask, error);
    g_current_thread_polling_island = NULL;
  }

  GPR_ASSERT(pi != NULL);

  /* Before leaving, release the extra ref we added to the polling island. It
     is important to use "pi" here (i.e our old copy of pollset->po.pi
     that we got before releasing the polling island lock). This is because
     pollset->po.pi pointer might get udpated in other parts of the
     code when there is an island merge while we are doing epoll_wait() above */
  PI_UNREF(exec_ctx, pi, "ps_work");

  GPR_TIMER_END("pollset_work_and_unlock", 0);
}

/* pollset->po.mu lock must be held by the caller before calling this.
   The function pollset_work() may temporarily release the lock (pollset->po.mu)
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
    /* We use the posix-signal with number 'grpc_wakeup_signal' for waking up
       (i.e 'kicking') a worker in the pollset. A 'kick' is a way to inform the
       worker that there is some pending work that needs immediate attention
       (like an event on the completion queue, or a polling island merge that
       results in a new epoll-fd to wait on) and that the worker should not
       spend time waiting in epoll_pwait().

       A worker can be kicked anytime from the point it is added to the pollset
       via push_front_worker() (or push_back_worker()) to the point it is
       removed via remove_worker().
       If the worker is kicked before/during it calls epoll_pwait(), it should
       immediately exit from epoll_wait(). If the worker is kicked after it
       returns from epoll_wait(), then nothing really needs to be done.

       To accomplish this, we mask 'grpc_wakeup_signal' on this thread at all
       times *except* when it is in epoll_pwait(). This way, the worker never
       misses acting on a kick */

    if (!g_initialized_sigmask) {
      sigemptyset(&g_wakeup_sig_set);
      sigaddset(&g_wakeup_sig_set, grpc_wakeup_signal);
      pthread_sigmask(SIG_BLOCK, &g_wakeup_sig_set, &g_orig_sigmask);
      sigdelset(&g_orig_sigmask, grpc_wakeup_signal);
      g_initialized_sigmask = true;
      /* new_mask:       The new thread mask which blocks 'grpc_wakeup_signal'.
                         This is the mask used at all times *except during
                         epoll_wait()*"
         g_orig_sigmask: The thread mask which allows 'grpc_wakeup_signal' and
                         this is the mask to use *during epoll_wait()*

         The new_mask is set on the worker before it is added to the pollset
         (i.e before it can be kicked) */
    }

    push_front_worker(pollset, &worker); /* Add worker to pollset */

    pollset_work_and_unlock(exec_ctx, pollset, &worker, now, deadline,
                            &g_orig_sigmask, &error);
    grpc_exec_ctx_flush(exec_ctx);

    gpr_mu_lock(&pollset->po.mu);

    /* Note: There is no need to reset worker.is_kicked to 0 since we are no
       longer going to use this worker */
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

    gpr_mu_unlock(&pollset->po.mu);
    grpc_exec_ctx_flush(exec_ctx);
    gpr_mu_lock(&pollset->po.mu);
  }

  if (worker_hdl) *worker_hdl = NULL;

  gpr_tls_set(&g_current_thread_pollset, (intptr_t)0);
  gpr_tls_set(&g_current_thread_worker, (intptr_t)0);

  GPR_TIMER_END("pollset_work", 0);

  GRPC_LOG_IF_ERROR("pollset_work", GRPC_ERROR_REF(error));
  return error;
}

static void add_poll_object(grpc_exec_ctx *exec_ctx, poll_obj *bag,
                            poll_obj_type bag_type, poll_obj *item,
                            poll_obj_type item_type) {
  GPR_TIMER_BEGIN("add_poll_object", 0);

#ifdef PO_DEBUG
  GPR_ASSERT(item->obj_type == item_type);
  GPR_ASSERT(bag->obj_type == bag_type);
#endif

  grpc_error *error = GRPC_ERROR_NONE;
  polling_island *pi_new = NULL;

  gpr_mu_lock(&bag->mu);
  gpr_mu_lock(&item->mu);

retry:
  /*
   * 1) If item->pi and bag->pi are both non-NULL and equal, do nothing
   * 2) If item->pi and bag->pi are both NULL, create a new polling island (with
   *    a refcount of 2) and point item->pi and bag->pi to the new island
   * 3) If exactly one of item->pi or bag->pi is NULL, update it to point to
   *    the other's non-NULL pi
   * 4) Finally if item->pi and bag-pi are non-NULL and not-equal, merge the
   *    polling islands and update item->pi and bag->pi to point to the new
   *    island
   */

  /* Early out if we are trying to add an 'fd' to a 'bag' but the fd is already
   * orphaned */
  if (item_type == POLL_OBJ_FD && (FD_FROM_PO(item))->orphaned) {
    gpr_mu_unlock(&item->mu);
    gpr_mu_unlock(&bag->mu);
    return;
  }

  if (item->pi == bag->pi) {
    pi_new = item->pi;
    if (pi_new == NULL) {
      /* GPR_ASSERT(item->pi == bag->pi == NULL) */

      /* If we are adding an fd to a bag (i.e pollset or pollset_set), then
       * we need to do some extra work to make TSAN happy */
      if (item_type == POLL_OBJ_FD) {
        /* Unlock before creating a new polling island: the polling island will
           create a workqueue which creates a file descriptor, and holding an fd
           lock here can eventually cause a loop to appear to TSAN (making it
           unhappy). We don't think it's a real loop (there's an epoch point
           where that loop possibility disappears), but the advantages of
           keeping TSAN happy outweigh any performance advantage we might have
           by keeping the lock held. */
        gpr_mu_unlock(&item->mu);
        pi_new = polling_island_create(exec_ctx, FD_FROM_PO(item), &error);
        gpr_mu_lock(&item->mu);

        /* Need to reverify any assumptions made between the initial lock and
           getting to this branch: if they've changed, we need to throw away our
           work and figure things out again. */
        if (item->pi != NULL) {
          GRPC_POLLING_TRACE(
              "add_poll_object: Raced creating new polling island. pi_new: %p "
              "(fd: %d, %s: %p)",
              (void *)pi_new, FD_FROM_PO(item)->fd, poll_obj_string(bag_type),
              (void *)bag);
          /* No need to lock 'pi_new' here since this is a new polling island
             and no one has a reference to it yet */
          polling_island_remove_all_fds_locked(pi_new, true, &error);

          /* Ref and unref so that the polling island gets deleted during unref
           */
          PI_ADD_REF(pi_new, "dance_of_destruction");
          PI_UNREF(exec_ctx, pi_new, "dance_of_destruction");
          goto retry;
        }
      } else {
        pi_new = polling_island_create(exec_ctx, NULL, &error);
      }

      GRPC_POLLING_TRACE(
          "add_poll_object: Created new polling island. pi_new: %p (%s: %p, "
          "%s: %p)",
          (void *)pi_new, poll_obj_string(item_type), (void *)item,
          poll_obj_string(bag_type), (void *)bag);
    } else {
      GRPC_POLLING_TRACE(
          "add_poll_object: Same polling island. pi: %p (%s, %s)",
          (void *)pi_new, poll_obj_string(item_type),
          poll_obj_string(bag_type));
    }
  } else if (item->pi == NULL) {
    /* GPR_ASSERT(bag->pi != NULL) */
    /* Make pi_new point to latest pi*/
    pi_new = polling_island_lock(bag->pi);

    if (item_type == POLL_OBJ_FD) {
      grpc_fd *fd = FD_FROM_PO(item);
      polling_island_add_fds_locked(pi_new, &fd, 1, true, &error);
    }

    gpr_mu_unlock(&pi_new->mu);
    GRPC_POLLING_TRACE(
        "add_poll_obj: item->pi was NULL. pi_new: %p (item(%s): %p, "
        "bag(%s): %p)",
        (void *)pi_new, poll_obj_string(item_type), (void *)item,
        poll_obj_string(bag_type), (void *)bag);
  } else if (bag->pi == NULL) {
    /* GPR_ASSERT(item->pi != NULL) */
    /* Make pi_new to point to latest pi */
    pi_new = polling_island_lock(item->pi);
    gpr_mu_unlock(&pi_new->mu);
    GRPC_POLLING_TRACE(
        "add_poll_obj: bag->pi was NULL. pi_new: %p (item(%s): %p, "
        "bag(%s): %p)",
        (void *)pi_new, poll_obj_string(item_type), (void *)item,
        poll_obj_string(bag_type), (void *)bag);
  } else {
    pi_new = polling_island_merge(item->pi, bag->pi, &error);
    GRPC_POLLING_TRACE(
        "add_poll_obj: polling islands merged. pi_new: %p (item(%s): %p, "
        "bag(%s): %p)",
        (void *)pi_new, poll_obj_string(item_type), (void *)item,
        poll_obj_string(bag_type), (void *)bag);
  }

  /* At this point, pi_new is the polling island that both item->pi and bag->pi
     MUST be pointing to */

  if (item->pi != pi_new) {
    PI_ADD_REF(pi_new, poll_obj_string(item_type));
    if (item->pi != NULL) {
      PI_UNREF(exec_ctx, item->pi, poll_obj_string(item_type));
    }
    item->pi = pi_new;
  }

  if (bag->pi != pi_new) {
    PI_ADD_REF(pi_new, poll_obj_string(bag_type));
    if (bag->pi != NULL) {
      PI_UNREF(exec_ctx, bag->pi, poll_obj_string(bag_type));
    }
    bag->pi = pi_new;
  }

  gpr_mu_unlock(&item->mu);
  gpr_mu_unlock(&bag->mu);

  GRPC_LOG_IF_ERROR("add_poll_object", error);
  GPR_TIMER_END("add_poll_object", 0);
}

static void pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_fd *fd) {
  add_poll_object(exec_ctx, &pollset->po, POLL_OBJ_POLLSET, &fd->po,
                  POLL_OBJ_FD);
}

/*******************************************************************************
 * Pollset-set Definitions
 */

static grpc_pollset_set *pollset_set_create(void) {
  grpc_pollset_set *pss = gpr_malloc(sizeof(*pss));
  gpr_mu_init(&pss->po.mu);
  pss->po.pi = NULL;
#ifdef PO_DEBUG
  pss->po.obj_type = POLL_OBJ_POLLSET_SET;
#endif
  return pss;
}

static void pollset_set_destroy(grpc_exec_ctx *exec_ctx,
                                grpc_pollset_set *pss) {
  gpr_mu_destroy(&pss->po.mu);

  if (pss->po.pi != NULL) {
    PI_UNREF(exec_ctx, pss->po.pi, "pss_destroy");
  }

  gpr_free(pss);
}

static void pollset_set_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset_set *pss,
                               grpc_fd *fd) {
  add_poll_object(exec_ctx, &pss->po, POLL_OBJ_POLLSET_SET, &fd->po,
                  POLL_OBJ_FD);
}

static void pollset_set_del_fd(grpc_exec_ctx *exec_ctx, grpc_pollset_set *pss,
                               grpc_fd *fd) {
  /* Nothing to do */
}

static void pollset_set_add_pollset(grpc_exec_ctx *exec_ctx,
                                    grpc_pollset_set *pss, grpc_pollset *ps) {
  add_poll_object(exec_ctx, &pss->po, POLL_OBJ_POLLSET_SET, &ps->po,
                  POLL_OBJ_POLLSET);
}

static void pollset_set_del_pollset(grpc_exec_ctx *exec_ctx,
                                    grpc_pollset_set *pss, grpc_pollset *ps) {
  /* Nothing to do */
}

static void pollset_set_add_pollset_set(grpc_exec_ctx *exec_ctx,
                                        grpc_pollset_set *bag,
                                        grpc_pollset_set *item) {
  add_poll_object(exec_ctx, &bag->po, POLL_OBJ_POLLSET_SET, &item->po,
                  POLL_OBJ_POLLSET_SET);
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
  fd_global_shutdown();
  pollset_global_shutdown();
  polling_island_global_shutdown();
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

/* This is mainly for testing purposes. Checks to see if environment variable
 * GRPC_MAX_POLLERS_PER_PI is set and if so, assigns that value to
 * g_max_pollers_per_pi (any negative value is considered INT_MAX) */
static void set_max_pollers_per_island() {
  char *s = gpr_getenv("GRPC_MAX_POLLERS_PER_PI");
  if (s) {
    g_max_pollers_per_pi = (int)strtol(s, NULL, 10);
    if (g_max_pollers_per_pi < 0) {
      g_max_pollers_per_pi = INT_MAX;
    }
  } else {
    g_max_pollers_per_pi = INT_MAX;
  }

  gpr_log(GPR_INFO, "Max number of pollers per polling island: %d",
          g_max_pollers_per_pi);
}

const grpc_event_engine_vtable *grpc_init_epoll_limited_pollers_linux(
    bool explicitly_requested) {
  if (!explicitly_requested) {
    return NULL;
  }

  /* If use of signals is disabled, we cannot use epoll engine*/
  if (is_grpc_wakeup_signal_initialized && grpc_wakeup_signal < 0) {
    return NULL;
  }

  if (!grpc_has_wakeup_fd()) {
    return NULL;
  }

  if (!is_epoll_available()) {
    return NULL;
  }

  if (!is_grpc_wakeup_signal_initialized) {
    grpc_use_signal(SIGRTMIN + 6);
  }

  set_max_pollers_per_island();

  fd_global_init();

  if (!GRPC_LOG_IF_ERROR("pollset_global_init", pollset_global_init())) {
    return NULL;
  }

  if (!GRPC_LOG_IF_ERROR("polling_island_global_init",
                         polling_island_global_init())) {
    return NULL;
  }

  return &vtable;
}

#else /* defined(GRPC_LINUX_EPOLL) */
#if defined(GRPC_POSIX_SOCKET)
#include "src/core/lib/iomgr/ev_posix.h"
/* If GRPC_LINUX_EPOLL is not defined, it means epoll is not available. Return
 * NULL */
const grpc_event_engine_vtable *grpc_init_epoll_limited_pollers_linux(
    bool explicitly_requested) {
  return NULL;
}
#endif /* defined(GRPC_POSIX_SOCKET) */
#endif /* !defined(GRPC_LINUX_EPOLL) */
