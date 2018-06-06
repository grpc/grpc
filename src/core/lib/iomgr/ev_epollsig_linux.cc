/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#include <grpc/grpc_posix.h>
#include <grpc/support/log.h>

/* This polling engine is only relevant on linux kernels supporting epoll() */
#ifdef GRPC_LINUX_EPOLL_CREATE1

#include "src/core/lib/iomgr/ev_epollsig_linux.h"

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
#include <grpc/support/string_util.h>

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/tls.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/iomgr/block_annotate.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/lockfree_event.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/lib/profiling/timers.h"

#define GRPC_POLLSET_KICK_BROADCAST ((grpc_pollset_worker*)1)

#define GRPC_POLLING_TRACE(...)       \
  if (grpc_polling_trace.enabled()) { \
    gpr_log(GPR_INFO, __VA_ARGS__);   \
  }

static int grpc_wakeup_signal = -1;
static bool is_grpc_wakeup_signal_initialized = false;

/* Implements the function defined in grpc_posix.h. This function might be
 * called before even calling grpc_init() to set either a different signal to
 * use. If signum == -1, then the use of signals is disabled */
void grpc_use_signal(int signum) {
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
#ifndef NDEBUG
  poll_obj_type obj_type;
#endif
  gpr_mu mu;
  struct polling_island* pi;
} poll_obj;

const char* poll_obj_string(poll_obj_type po_type) {
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

#define FD_FROM_PO(po) ((grpc_fd*)(po))

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

  grpc_core::ManualConstructor<grpc_core::LockfreeEvent> read_closure;
  grpc_core::ManualConstructor<grpc_core::LockfreeEvent> write_closure;

  struct grpc_fd* freelist_next;
  grpc_closure* on_done_closure;

  /* The pollset that last noticed that the fd is readable. The actual type
   * stored in this is (grpc_pollset *) */
  gpr_atm read_notifier_pollset;

  grpc_iomgr_object iomgr_object;
};

/* Reference counting for fds */
#ifndef NDEBUG
static void fd_ref(grpc_fd* fd, const char* reason, const char* file, int line);
static void fd_unref(grpc_fd* fd, const char* reason, const char* file,
                     int line);
#define GRPC_FD_REF(fd, reason) fd_ref(fd, reason, __FILE__, __LINE__)
#define GRPC_FD_UNREF(fd, reason) fd_unref(fd, reason, __FILE__, __LINE__)
#else
static void fd_ref(grpc_fd* fd);
static void fd_unref(grpc_fd* fd);
#define GRPC_FD_REF(fd, reason) fd_ref(fd)
#define GRPC_FD_UNREF(fd, reason) fd_unref(fd)
#endif

static void fd_global_init(void);
static void fd_global_shutdown(void);

/*******************************************************************************
 * Polling island Declarations
 */

#ifndef NDEBUG

#define PI_ADD_REF(p, r) pi_add_ref_dbg((p), (r), __FILE__, __LINE__)
#define PI_UNREF(p, r) pi_unref_dbg((p), (r), __FILE__, __LINE__)

#else

#define PI_ADD_REF(p, r) pi_add_ref((p))
#define PI_UNREF(p, r) pi_unref((p))

#endif

/* This is also used as grpc_workqueue (by directly casing it) */
typedef struct polling_island {
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

  /* The fd of the underlying epoll set */
  int epoll_fd;

  /* The file descriptors in the epoll set */
  size_t fd_cnt;
  size_t fd_capacity;
  grpc_fd** fds;
} polling_island;

/*******************************************************************************
 * Pollset Declarations
 */
struct grpc_pollset_worker {
  /* Thread id of this worker */
  pthread_t pt_id;

  /* Used to prevent a worker from getting kicked multiple times */
  gpr_atm is_kicked;
  struct grpc_pollset_worker* next;
  struct grpc_pollset_worker* prev;
};

struct grpc_pollset {
  poll_obj po;

  grpc_pollset_worker root_worker;
  bool kicked_without_pollers;

  bool shutting_down;          /* Is the pollset shutting down ? */
  bool finish_shutdown_called; /* Is the 'finish_shutdown_locked()' called ? */
  grpc_closure* shutdown_done; /* Called after after shutdown is complete */
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

static bool append_error(grpc_error** composite, grpc_error* error,
                         const char* desc) {
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
static __thread polling_island* g_current_thread_polling_island;

/* Forward declaration */
static void polling_island_delete(polling_island* pi);

#ifdef GRPC_TSAN
/* Currently TSAN may incorrectly flag data races between epoll_ctl and
   epoll_wait for any grpc_fd structs that are added to the epoll set via
   epoll_ctl and are returned (within a very short window) via epoll_wait().

   To work-around this race, we establish a happens-before relation between
   the code just-before epoll_ctl() and the code after epoll_wait() by using
   this atomic */
gpr_atm g_epoll_sync;
#endif /* defined(GRPC_TSAN) */

static void pi_add_ref(polling_island* pi);
static void pi_unref(polling_island* pi);

#ifndef NDEBUG
static void pi_add_ref_dbg(polling_island* pi, const char* reason,
                           const char* file, int line) {
  if (grpc_polling_trace.enabled()) {
    gpr_atm old_cnt = gpr_atm_acq_load(&pi->ref_count);
    gpr_log(GPR_INFO,
            "Add ref pi: %p, old:%" PRIdPTR " -> new:%" PRIdPTR
            " (%s) - (%s, %d)",
            pi, old_cnt, old_cnt + 1, reason, file, line);
  }
  pi_add_ref(pi);
}

static void pi_unref_dbg(polling_island* pi, const char* reason,
                         const char* file, int line) {
  if (grpc_polling_trace.enabled()) {
    gpr_atm old_cnt = gpr_atm_acq_load(&pi->ref_count);
    gpr_log(GPR_INFO,
            "Unref pi: %p, old:%" PRIdPTR " -> new:%" PRIdPTR
            " (%s) - (%s, %d)",
            pi, old_cnt, (old_cnt - 1), reason, file, line);
  }
  pi_unref(pi);
}
#endif

static void pi_add_ref(polling_island* pi) {
  gpr_atm_no_barrier_fetch_add(&pi->ref_count, 1);
}

static void pi_unref(polling_island* pi) {
  /* If ref count went to zero, delete the polling island.
     Note that this deletion not be done under a lock. Once the ref count goes
     to zero, we are guaranteed that no one else holds a reference to the
     polling island (and that there is no racing pi_add_ref() call either).

     Also, if we are deleting the polling island and the merged_to field is
     non-empty, we should remove a ref to the merged_to polling island
   */
  if (1 == gpr_atm_full_fetch_add(&pi->ref_count, -1)) {
    polling_island* next = (polling_island*)gpr_atm_acq_load(&pi->merged_to);
    polling_island_delete(pi);
    if (next != nullptr) {
      PI_UNREF(next, "pi_delete"); /* Recursive call */
    }
  }
}

/* The caller is expected to hold pi->mu lock before calling this function */
static void polling_island_add_fds_locked(polling_island* pi, grpc_fd** fds,
                                          size_t fd_count, bool add_fd_refs,
                                          grpc_error** error) {
  int err;
  size_t i;
  struct epoll_event ev;
  char* err_msg;
  const char* err_desc = "polling_island_add_fds";

#ifdef GRPC_TSAN
  /* See the definition of g_epoll_sync for more context */
  gpr_atm_rel_store(&g_epoll_sync, (gpr_atm)0);
#endif /* defined(GRPC_TSAN) */

  for (i = 0; i < fd_count; i++) {
    ev.events = static_cast<uint32_t>(EPOLLIN | EPOLLOUT | EPOLLET);
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
      pi->fds = static_cast<grpc_fd**>(
          gpr_realloc(pi->fds, sizeof(grpc_fd*) * pi->fd_capacity));
    }

    pi->fds[pi->fd_cnt++] = fds[i];
    if (add_fd_refs) {
      GRPC_FD_REF(fds[i], "polling_island");
    }
  }
}

/* The caller is expected to hold pi->mu before calling this */
static void polling_island_add_wakeup_fd_locked(polling_island* pi,
                                                grpc_wakeup_fd* wakeup_fd,
                                                grpc_error** error) {
  struct epoll_event ev;
  int err;
  char* err_msg;
  const char* err_desc = "polling_island_add_wakeup_fd";

  ev.events = static_cast<uint32_t>(EPOLLIN | EPOLLET);
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
static void polling_island_remove_all_fds_locked(polling_island* pi,
                                                 bool remove_fd_refs,
                                                 grpc_error** error) {
  int err;
  size_t i;
  char* err_msg;
  const char* err_desc = "polling_island_remove_fds";

  for (i = 0; i < pi->fd_cnt; i++) {
    err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_DEL, pi->fds[i]->fd, nullptr);
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
static void polling_island_remove_fd_locked(polling_island* pi, grpc_fd* fd,
                                            bool is_fd_closed,
                                            grpc_error** error) {
  int err;
  size_t i;
  char* err_msg;
  const char* err_desc = "polling_island_remove_fd";

  /* If fd is already closed, then it would have been automatically been removed
     from the epoll set */
  if (!is_fd_closed) {
    err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_DEL, fd->fd, nullptr);
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
static polling_island* polling_island_create(grpc_fd* initial_fd,
                                             grpc_error** error) {
  polling_island* pi = nullptr;
  const char* err_desc = "polling_island_create";

  *error = GRPC_ERROR_NONE;

  pi = static_cast<polling_island*>(gpr_malloc(sizeof(*pi)));
  gpr_mu_init(&pi->mu);
  pi->fd_cnt = 0;
  pi->fd_capacity = 0;
  pi->fds = nullptr;
  pi->epoll_fd = -1;

  gpr_atm_rel_store(&pi->ref_count, 0);
  gpr_atm_rel_store(&pi->poller_count, 0);
  gpr_atm_rel_store(&pi->merged_to, (gpr_atm) nullptr);

  pi->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

  if (pi->epoll_fd < 0) {
    append_error(error, GRPC_OS_ERROR(errno, "epoll_create1"), err_desc);
    goto done;
  }

  if (initial_fd != nullptr) {
    polling_island_add_fds_locked(pi, &initial_fd, 1, true, error);
  }

done:
  if (*error != GRPC_ERROR_NONE) {
    polling_island_delete(pi);
    pi = nullptr;
  }
  return pi;
}

static void polling_island_delete(polling_island* pi) {
  GPR_ASSERT(pi->fd_cnt == 0);

  if (pi->epoll_fd >= 0) {
    close(pi->epoll_fd);
  }
  gpr_mu_destroy(&pi->mu);
  gpr_free(pi->fds);
  gpr_free(pi);
}

/* Attempts to gets the last polling island in the linked list (liked by the
 * 'merged_to' field). Since this does not lock the polling island, there are no
 * guarantees that the island returned is the last island */
static polling_island* polling_island_maybe_get_latest(polling_island* pi) {
  polling_island* next = (polling_island*)gpr_atm_acq_load(&pi->merged_to);
  while (next != nullptr) {
    pi = next;
    next = (polling_island*)gpr_atm_acq_load(&pi->merged_to);
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
static polling_island* polling_island_lock(polling_island* pi) {
  polling_island* next = nullptr;

  while (true) {
    next = (polling_island*)gpr_atm_acq_load(&pi->merged_to);
    if (next == nullptr) {
      /* Looks like 'pi' is the last node in the linked list but unless we check
         this by holding the pi->mu lock, we cannot be sure (i.e without the
         pi->mu lock, we don't prevent island merges).
         To be absolutely sure, check once more by holding the pi->mu lock */
      gpr_mu_lock(&pi->mu);
      next = (polling_island*)gpr_atm_acq_load(&pi->merged_to);
      if (next == nullptr) {
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
static void polling_island_lock_pair(polling_island** p, polling_island** q) {
  polling_island* pi_1 = *p;
  polling_island* pi_2 = *q;
  polling_island* next_1 = nullptr;
  polling_island* next_2 = nullptr;

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
    next_1 = (polling_island*)gpr_atm_acq_load(&pi_1->merged_to);
    while (next_1 != nullptr) {
      pi_1 = next_1;
      next_1 = (polling_island*)gpr_atm_acq_load(&pi_1->merged_to);
    }

    next_2 = (polling_island*)gpr_atm_acq_load(&pi_2->merged_to);
    while (next_2 != nullptr) {
      pi_2 = next_2;
      next_2 = (polling_island*)gpr_atm_acq_load(&pi_2->merged_to);
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

    next_1 = (polling_island*)gpr_atm_acq_load(&pi_1->merged_to);
    next_2 = (polling_island*)gpr_atm_acq_load(&pi_2->merged_to);
    if (next_1 == nullptr && next_2 == nullptr) {
      break;
    }

    gpr_mu_unlock(&pi_1->mu);
    gpr_mu_unlock(&pi_2->mu);
  }

  *p = pi_1;
  *q = pi_2;
}

static void polling_island_unlock_pair(polling_island* p, polling_island* q) {
  if (p == q) {
    gpr_mu_unlock(&p->mu);
  } else {
    gpr_mu_unlock(&p->mu);
    gpr_mu_unlock(&q->mu);
  }
}

static polling_island* polling_island_merge(polling_island* p,
                                            polling_island* q,
                                            grpc_error** error) {
  /* Get locks on both the polling islands */
  polling_island_lock_pair(&p, &q);

  if (p != q) {
    /* Make sure that p points to the polling island with fewer fds than q */
    if (p->fd_cnt > q->fd_cnt) {
      GPR_SWAP(polling_island*, p, q);
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
  }
  /* else if p == q, nothing needs to be done */

  polling_island_unlock_pair(p, q);

  /* Return the merged polling island (Note that no merge would have happened
     if p == q which is ok) */
  return q;
}

static grpc_error* polling_island_global_init() {
  grpc_error* error = GRPC_ERROR_NONE;

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

static grpc_fd* fd_freelist = nullptr;
static gpr_mu fd_freelist_mu;

#ifndef NDEBUG
#define REF_BY(fd, n, reason) ref_by(fd, n, reason, __FILE__, __LINE__)
#define UNREF_BY(fd, n, reason) unref_by(fd, n, reason, __FILE__, __LINE__)
static void ref_by(grpc_fd* fd, int n, const char* reason, const char* file,
                   int line) {
  if (grpc_trace_fd_refcount.enabled()) {
    gpr_log(GPR_DEBUG,
            "FD %d %p   ref %d %" PRIdPTR " -> %" PRIdPTR " [%s; %s:%d]",
            fd->fd, fd, n, gpr_atm_no_barrier_load(&fd->refst),
            gpr_atm_no_barrier_load(&fd->refst) + n, reason, file, line);
  }
#else
#define REF_BY(fd, n, reason) ref_by(fd, n)
#define UNREF_BY(fd, n, reason) unref_by(fd, n)
static void ref_by(grpc_fd* fd, int n) {
#endif
  GPR_ASSERT(gpr_atm_no_barrier_fetch_add(&fd->refst, n) > 0);
}

#ifndef NDEBUG
static void unref_by(grpc_fd* fd, int n, const char* reason, const char* file,
                     int line) {
  if (grpc_trace_fd_refcount.enabled()) {
    gpr_log(GPR_DEBUG,
            "FD %d %p unref %d %" PRIdPTR " -> %" PRIdPTR " [%s; %s:%d]",
            fd->fd, fd, n, gpr_atm_no_barrier_load(&fd->refst),
            gpr_atm_no_barrier_load(&fd->refst) - n, reason, file, line);
  }
#else
static void unref_by(grpc_fd* fd, int n) {
#endif
  gpr_atm old = gpr_atm_full_fetch_add(&fd->refst, -n);
  if (old == n) {
    /* Add the fd to the freelist */
    gpr_mu_lock(&fd_freelist_mu);
    fd->freelist_next = fd_freelist;
    fd_freelist = fd;
    grpc_iomgr_unregister_object(&fd->iomgr_object);

    fd->read_closure->DestroyEvent();
    fd->write_closure->DestroyEvent();

    gpr_mu_unlock(&fd_freelist_mu);
  } else {
    GPR_ASSERT(old > n);
  }
}

/* Increment refcount by two to avoid changing the orphan bit */
#ifndef NDEBUG
static void fd_ref(grpc_fd* fd, const char* reason, const char* file,
                   int line) {
  ref_by(fd, 2, reason, file, line);
}

static void fd_unref(grpc_fd* fd, const char* reason, const char* file,
                     int line) {
  unref_by(fd, 2, reason, file, line);
}
#else
static void fd_ref(grpc_fd* fd) { ref_by(fd, 2); }
static void fd_unref(grpc_fd* fd) { unref_by(fd, 2); }
#endif

static void fd_global_init(void) { gpr_mu_init(&fd_freelist_mu); }

static void fd_global_shutdown(void) {
  gpr_mu_lock(&fd_freelist_mu);
  gpr_mu_unlock(&fd_freelist_mu);
  while (fd_freelist != nullptr) {
    grpc_fd* fd = fd_freelist;
    fd_freelist = fd_freelist->freelist_next;
    gpr_mu_destroy(&fd->po.mu);
    gpr_free(fd);
  }
  gpr_mu_destroy(&fd_freelist_mu);
}

static grpc_fd* fd_create(int fd, const char* name) {
  grpc_fd* new_fd = nullptr;

  gpr_mu_lock(&fd_freelist_mu);
  if (fd_freelist != nullptr) {
    new_fd = fd_freelist;
    fd_freelist = fd_freelist->freelist_next;
  }
  gpr_mu_unlock(&fd_freelist_mu);

  if (new_fd == nullptr) {
    new_fd = static_cast<grpc_fd*>(gpr_malloc(sizeof(grpc_fd)));
    gpr_mu_init(&new_fd->po.mu);
    new_fd->read_closure.Init();
    new_fd->write_closure.Init();
  }

  /* Note: It is not really needed to get the new_fd->po.mu lock here. If this
   * is a newly created fd (or an fd we got from the freelist), no one else
   * would be holding a lock to it anyway. */
  gpr_mu_lock(&new_fd->po.mu);
  new_fd->po.pi = nullptr;
#ifndef NDEBUG
  new_fd->po.obj_type = POLL_OBJ_FD;
#endif

  gpr_atm_rel_store(&new_fd->refst, (gpr_atm)1);
  new_fd->fd = fd;
  new_fd->orphaned = false;
  new_fd->read_closure->InitEvent();
  new_fd->write_closure->InitEvent();
  gpr_atm_no_barrier_store(&new_fd->read_notifier_pollset, (gpr_atm)NULL);

  new_fd->freelist_next = nullptr;
  new_fd->on_done_closure = nullptr;

  gpr_mu_unlock(&new_fd->po.mu);

  char* fd_name;
  gpr_asprintf(&fd_name, "%s fd=%d", name, fd);
  grpc_iomgr_register_object(&new_fd->iomgr_object, fd_name);
  gpr_free(fd_name);
  return new_fd;
}

static int fd_wrapped_fd(grpc_fd* fd) {
  int ret_fd = -1;
  gpr_mu_lock(&fd->po.mu);
  if (!fd->orphaned) {
    ret_fd = fd->fd;
  }
  gpr_mu_unlock(&fd->po.mu);

  return ret_fd;
}

static void fd_orphan(grpc_fd* fd, grpc_closure* on_done, int* release_fd,
                      bool already_closed, const char* reason) {
  grpc_error* error = GRPC_ERROR_NONE;
  polling_island* unref_pi = nullptr;

  gpr_mu_lock(&fd->po.mu);
  fd->on_done_closure = on_done;

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
  if (fd->po.pi != nullptr) {
    polling_island* pi_latest = polling_island_lock(fd->po.pi);
    polling_island_remove_fd_locked(pi_latest, fd, already_closed, &error);
    gpr_mu_unlock(&pi_latest->mu);

    unref_pi = fd->po.pi;
    fd->po.pi = nullptr;
  }

  /* If release_fd is not NULL, we should be relinquishing control of the file
     descriptor fd->fd (but we still own the grpc_fd structure). */
  if (release_fd != nullptr) {
    *release_fd = fd->fd;
  } else {
    close(fd->fd);
  }

  fd->orphaned = true;

  GRPC_CLOSURE_SCHED(fd->on_done_closure, GRPC_ERROR_REF(error));

  gpr_mu_unlock(&fd->po.mu);
  UNREF_BY(fd, 2, reason); /* Drop the reference */
  if (unref_pi != nullptr) {
    /* Unref stale polling island here, outside the fd lock above.
       The polling island owns a workqueue which owns an fd, and unreffing
       inside the lock can cause an eventual lock loop that makes TSAN very
       unhappy. */
    PI_UNREF(unref_pi, "fd_orphan");
  }
  if (error != GRPC_ERROR_NONE) {
    const char* msg = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "fd_orphan: %s", msg);
  }
  GRPC_ERROR_UNREF(error);
}

static grpc_pollset* fd_get_read_notifier_pollset(grpc_fd* fd) {
  gpr_atm notifier = gpr_atm_acq_load(&fd->read_notifier_pollset);
  return (grpc_pollset*)notifier;
}

static bool fd_is_shutdown(grpc_fd* fd) {
  return fd->read_closure->IsShutdown();
}

/* Might be called multiple times */
static void fd_shutdown(grpc_fd* fd, grpc_error* why) {
  if (fd->read_closure->SetShutdown(GRPC_ERROR_REF(why))) {
    shutdown(fd->fd, SHUT_RDWR);
    fd->write_closure->SetShutdown(GRPC_ERROR_REF(why));
  }
  GRPC_ERROR_UNREF(why);
}

static void fd_notify_on_read(grpc_fd* fd, grpc_closure* closure) {
  fd->read_closure->NotifyOn(closure);
}

static void fd_notify_on_write(grpc_fd* fd, grpc_closure* closure) {
  fd->write_closure->NotifyOn(closure);
}

/*******************************************************************************
 * Pollset Definitions
 */
GPR_TLS_DECL(g_current_thread_pollset);
GPR_TLS_DECL(g_current_thread_worker);
static __thread bool g_initialized_sigmask;
static __thread sigset_t g_orig_sigmask;

static void sig_handler(int sig_num) {
#ifdef GRPC_EPOLL_DEBUG
  gpr_log(GPR_INFO, "Received signal %d", sig_num);
#endif
}

static void poller_kick_init() { signal(grpc_wakeup_signal, sig_handler); }

/* Global state management */
static grpc_error* pollset_global_init(void) {
  gpr_tls_init(&g_current_thread_pollset);
  gpr_tls_init(&g_current_thread_worker);
  poller_kick_init();
  return GRPC_ERROR_NONE;
}

static void pollset_global_shutdown(void) {
  gpr_tls_destroy(&g_current_thread_pollset);
  gpr_tls_destroy(&g_current_thread_worker);
}

static grpc_error* pollset_worker_kick(grpc_pollset_worker* worker) {
  grpc_error* err = GRPC_ERROR_NONE;

  /* Kick the worker only if it was not already kicked */
  if (gpr_atm_no_barrier_cas(&worker->is_kicked, static_cast<gpr_atm>(0),
                             static_cast<gpr_atm>(1))) {
    GRPC_POLLING_TRACE(
        "pollset_worker_kick: Kicking worker: %p (thread id: %ld)",
        (void*)worker, (long int)worker->pt_id);
    int err_num = pthread_kill(worker->pt_id, grpc_wakeup_signal);
    if (err_num != 0) {
      err = GRPC_OS_ERROR(err_num, "pthread_kill");
    }
  }
  return err;
}

/* Return 1 if the pollset has active threads in pollset_work (pollset must
 * be locked) */
static int pollset_has_workers(grpc_pollset* p) {
  return p->root_worker.next != &p->root_worker;
}

static void remove_worker(grpc_pollset* p, grpc_pollset_worker* worker) {
  worker->prev->next = worker->next;
  worker->next->prev = worker->prev;
}

static grpc_pollset_worker* pop_front_worker(grpc_pollset* p) {
  if (pollset_has_workers(p)) {
    grpc_pollset_worker* w = p->root_worker.next;
    remove_worker(p, w);
    return w;
  } else {
    return nullptr;
  }
}

static void push_back_worker(grpc_pollset* p, grpc_pollset_worker* worker) {
  worker->next = &p->root_worker;
  worker->prev = worker->next->prev;
  worker->prev->next = worker->next->prev = worker;
}

static void push_front_worker(grpc_pollset* p, grpc_pollset_worker* worker) {
  worker->prev = &p->root_worker;
  worker->next = worker->prev->next;
  worker->prev->next = worker->next->prev = worker;
}

/* p->mu must be held before calling this function */
static grpc_error* pollset_kick(grpc_pollset* p,
                                grpc_pollset_worker* specific_worker) {
  GPR_TIMER_SCOPE("pollset_kick", 0);
  grpc_error* error = GRPC_ERROR_NONE;
  GRPC_STATS_INC_POLLSET_KICK();
  const char* err_desc = "Kick Failure";
  grpc_pollset_worker* worker = specific_worker;
  if (worker != nullptr) {
    if (worker == GRPC_POLLSET_KICK_BROADCAST) {
      if (pollset_has_workers(p)) {
        GPR_TIMER_SCOPE("pollset_kick.broadcast", 0);
        for (worker = p->root_worker.next; worker != &p->root_worker;
             worker = worker->next) {
          if (gpr_tls_get(&g_current_thread_worker) != (intptr_t)worker) {
            append_error(&error, pollset_worker_kick(worker), err_desc);
          }
        }
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
    if (worker != nullptr) {
      GPR_TIMER_MARK("finally_kick", 0);
      push_back_worker(p, worker);
      append_error(&error, pollset_worker_kick(worker), err_desc);
    } else {
      GPR_TIMER_MARK("kicked_no_pollers", 0);
      p->kicked_without_pollers = true;
    }
  }

  GRPC_LOG_IF_ERROR("pollset_kick", GRPC_ERROR_REF(error));
  return error;
}

static void pollset_init(grpc_pollset* pollset, gpr_mu** mu) {
  gpr_mu_init(&pollset->po.mu);
  *mu = &pollset->po.mu;
  pollset->po.pi = nullptr;
#ifndef NDEBUG
  pollset->po.obj_type = POLL_OBJ_POLLSET;
#endif

  pollset->root_worker.next = pollset->root_worker.prev = &pollset->root_worker;
  pollset->kicked_without_pollers = false;

  pollset->shutting_down = false;
  pollset->finish_shutdown_called = false;
  pollset->shutdown_done = nullptr;
}

static int poll_deadline_to_millis_timeout(grpc_millis millis) {
  if (millis == GRPC_MILLIS_INF_FUTURE) return -1;
  grpc_millis delta = millis - grpc_core::ExecCtx::Get()->Now();
  if (delta > INT_MAX)
    return INT_MAX;
  else if (delta < 0)
    return 0;
  else
    return static_cast<int>(delta);
}

static void fd_become_readable(grpc_fd* fd, grpc_pollset* notifier) {
  fd->read_closure->SetReady();

  /* Note, it is possible that fd_become_readable might be called twice with
     different 'notifier's when an fd becomes readable and it is in two epoll
     sets (This can happen briefly during polling island merges). In such cases
     it does not really matter which notifer is set as the read_notifier_pollset
     (They would both point to the same polling island anyway) */
  /* Use release store to match with acquire load in fd_get_read_notifier */
  gpr_atm_rel_store(&fd->read_notifier_pollset, (gpr_atm)notifier);
}

static void fd_become_writable(grpc_fd* fd) { fd->write_closure->SetReady(); }

static void pollset_release_polling_island(grpc_pollset* ps,
                                           const char* reason) {
  if (ps->po.pi != nullptr) {
    PI_UNREF(ps->po.pi, reason);
  }
  ps->po.pi = nullptr;
}

static void finish_shutdown_locked(grpc_pollset* pollset) {
  /* The pollset cannot have any workers if we are at this stage */
  GPR_ASSERT(!pollset_has_workers(pollset));

  pollset->finish_shutdown_called = true;

  /* Release the ref and set pollset->po.pi to NULL */
  pollset_release_polling_island(pollset, "ps_shutdown");
  GRPC_CLOSURE_SCHED(pollset->shutdown_done, GRPC_ERROR_NONE);
}

/* pollset->po.mu lock must be held by the caller before calling this */
static void pollset_shutdown(grpc_pollset* pollset, grpc_closure* closure) {
  GPR_TIMER_SCOPE("pollset_shutdown", 0);
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
    finish_shutdown_locked(pollset);
  }
}

/* pollset_shutdown is guaranteed to be called before pollset_destroy. So other
 * than destroying the mutexes, there is nothing special that needs to be done
 * here */
static void pollset_destroy(grpc_pollset* pollset) {
  GPR_ASSERT(!pollset_has_workers(pollset));
  gpr_mu_destroy(&pollset->po.mu);
}

#define GRPC_EPOLL_MAX_EVENTS 100
/* Note: sig_mask contains the signal mask to use *during* epoll_wait() */
static void pollset_work_and_unlock(grpc_pollset* pollset,
                                    grpc_pollset_worker* worker, int timeout_ms,
                                    sigset_t* sig_mask, grpc_error** error) {
  GPR_TIMER_SCOPE("pollset_work_and_unlock", 0);
  struct epoll_event ep_ev[GRPC_EPOLL_MAX_EVENTS];
  int epoll_fd = -1;
  int ep_rv;
  polling_island* pi = nullptr;
  char* err_msg;
  const char* err_desc = "pollset_work_and_unlock";

  /* We need to get the epoll_fd to wait on. The epoll_fd is in inside the
     latest polling island pointed by pollset->po.pi

     Since epoll_fd is immutable, we can read it without obtaining the polling
     island lock. There is however a possibility that the polling island (from
     which we got the epoll_fd) got merged with another island while we are
     in this function. This is still okay because in such a case, we will wakeup
     right-away from epoll_wait() and pick up the latest polling_island the next
     this function (i.e pollset_work_and_unlock()) is called */

  if (pollset->po.pi == nullptr) {
    pollset->po.pi = polling_island_create(nullptr, error);
    if (pollset->po.pi == nullptr) {
      return; /* Fatal error. We cannot continue */
    }

    PI_ADD_REF(pollset->po.pi, "ps");
    GRPC_POLLING_TRACE("pollset_work: pollset: %p created new pi: %p",
                       (void*)pollset, (void*)pollset->po.pi);
  }

  pi = polling_island_maybe_get_latest(pollset->po.pi);
  epoll_fd = pi->epoll_fd;

  /* Update the pollset->po.pi since the island being pointed by
     pollset->po.pi maybe older than the one pointed by pi) */
  if (pollset->po.pi != pi) {
    /* Always do PI_ADD_REF before PI_UNREF because PI_UNREF may cause the
       polling island to be deleted */
    PI_ADD_REF(pi, "ps");
    PI_UNREF(pollset->po.pi, "ps");
    pollset->po.pi = pi;
  }

  /* Add an extra ref so that the island does not get destroyed (which means
     the epoll_fd won't be closed) while we are are doing an epoll_wait() on the
     epoll_fd */
  PI_ADD_REF(pi, "ps_work");
  gpr_mu_unlock(&pollset->po.mu);

  gpr_atm_no_barrier_fetch_add(&pi->poller_count, 1);
  g_current_thread_polling_island = pi;

  GRPC_SCHEDULING_START_BLOCKING_REGION;
  GRPC_STATS_INC_SYSCALL_POLL();
  ep_rv =
      epoll_pwait(epoll_fd, ep_ev, GRPC_EPOLL_MAX_EVENTS, timeout_ms, sig_mask);
  GRPC_SCHEDULING_END_BLOCKING_REGION;
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
                         (void*)pollset, (void*)worker);
      ep_rv = epoll_wait(epoll_fd, ep_ev, GRPC_EPOLL_MAX_EVENTS, 0);
    }
  }

#ifdef GRPC_TSAN
  /* See the definition of g_poll_sync for more details */
  gpr_atm_acq_load(&g_epoll_sync);
#endif /* defined(GRPC_TSAN) */

  for (int i = 0; i < ep_rv; ++i) {
    void* data_ptr = ep_ev[i].data.ptr;
    if (data_ptr == &polling_island_wakeup_fd) {
      GRPC_POLLING_TRACE(
          "pollset_work: pollset: %p, worker: %p polling island (epoll_fd: "
          "%d) got merged",
          (void*)pollset, (void*)worker, epoll_fd);
      /* This means that our polling island is merged with a different
         island. We do not have to do anything here since the subsequent call
         to the function pollset_work_and_unlock() will pick up the correct
         epoll_fd */
    } else {
      grpc_fd* fd = static_cast<grpc_fd*>(data_ptr);
      int cancel = ep_ev[i].events & (EPOLLERR | EPOLLHUP);
      int read_ev = ep_ev[i].events & (EPOLLIN | EPOLLPRI);
      int write_ev = ep_ev[i].events & EPOLLOUT;
      if (read_ev || cancel) {
        fd_become_readable(fd, pollset);
      }
      if (write_ev || cancel) {
        fd_become_writable(fd);
      }
    }
  }

  g_current_thread_polling_island = nullptr;
  gpr_atm_no_barrier_fetch_add(&pi->poller_count, -1);

  GPR_ASSERT(pi != nullptr);

  /* Before leaving, release the extra ref we added to the polling island. It
     is important to use "pi" here (i.e our old copy of pollset->po.pi
     that we got before releasing the polling island lock). This is because
     pollset->po.pi pointer might get udpated in other parts of the
     code when there is an island merge while we are doing epoll_wait() above */
  PI_UNREF(pi, "ps_work");
}

/* pollset->po.mu lock must be held by the caller before calling this.
   The function pollset_work() may temporarily release the lock (pollset->po.mu)
   during the course of its execution but it will always re-acquire the lock and
   ensure that it is held by the time the function returns */
static grpc_error* pollset_work(grpc_pollset* pollset,
                                grpc_pollset_worker** worker_hdl,
                                grpc_millis deadline) {
  GPR_TIMER_SCOPE("pollset_work", 0);
  grpc_error* error = GRPC_ERROR_NONE;
  int timeout_ms = poll_deadline_to_millis_timeout(deadline);

  sigset_t new_mask;

  grpc_pollset_worker worker;
  worker.next = worker.prev = nullptr;
  worker.pt_id = pthread_self();
  gpr_atm_no_barrier_store(&worker.is_kicked, (gpr_atm)0);

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
      sigemptyset(&new_mask);
      sigaddset(&new_mask, grpc_wakeup_signal);
      pthread_sigmask(SIG_BLOCK, &new_mask, &g_orig_sigmask);
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

    pollset_work_and_unlock(pollset, &worker, timeout_ms, &g_orig_sigmask,
                            &error);
    grpc_core::ExecCtx::Get()->Flush();

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
    finish_shutdown_locked(pollset);

    gpr_mu_unlock(&pollset->po.mu);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(&pollset->po.mu);
  }

  if (worker_hdl) *worker_hdl = nullptr;

  gpr_tls_set(&g_current_thread_pollset, (intptr_t)0);
  gpr_tls_set(&g_current_thread_worker, (intptr_t)0);

  GRPC_LOG_IF_ERROR("pollset_work", GRPC_ERROR_REF(error));
  return error;
}

static void add_poll_object(poll_obj* bag, poll_obj_type bag_type,
                            poll_obj* item, poll_obj_type item_type) {
  GPR_TIMER_SCOPE("add_poll_object", 0);

#ifndef NDEBUG
  GPR_ASSERT(item->obj_type == item_type);
  GPR_ASSERT(bag->obj_type == bag_type);
#endif

  grpc_error* error = GRPC_ERROR_NONE;
  polling_island* pi_new = nullptr;

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
    if (pi_new == nullptr) {
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
        pi_new = polling_island_create(FD_FROM_PO(item), &error);
        gpr_mu_lock(&item->mu);

        /* Need to reverify any assumptions made between the initial lock and
           getting to this branch: if they've changed, we need to throw away our
           work and figure things out again. */
        if (item->pi != nullptr) {
          GRPC_POLLING_TRACE(
              "add_poll_object: Raced creating new polling island. pi_new: %p "
              "(fd: %d, %s: %p)",
              (void*)pi_new, FD_FROM_PO(item)->fd, poll_obj_string(bag_type),
              (void*)bag);
          /* No need to lock 'pi_new' here since this is a new polling island
             and no one has a reference to it yet */
          polling_island_remove_all_fds_locked(pi_new, true, &error);

          /* Ref and unref so that the polling island gets deleted during unref
           */
          PI_ADD_REF(pi_new, "dance_of_destruction");
          PI_UNREF(pi_new, "dance_of_destruction");
          goto retry;
        }
      } else {
        pi_new = polling_island_create(nullptr, &error);
      }

      GRPC_POLLING_TRACE(
          "add_poll_object: Created new polling island. pi_new: %p (%s: %p, "
          "%s: %p)",
          (void*)pi_new, poll_obj_string(item_type), (void*)item,
          poll_obj_string(bag_type), (void*)bag);
    } else {
      GRPC_POLLING_TRACE(
          "add_poll_object: Same polling island. pi: %p (%s, %s)",
          (void*)pi_new, poll_obj_string(item_type), poll_obj_string(bag_type));
    }
  } else if (item->pi == nullptr) {
    /* GPR_ASSERT(bag->pi != NULL) */
    /* Make pi_new point to latest pi*/
    pi_new = polling_island_lock(bag->pi);

    if (item_type == POLL_OBJ_FD) {
      grpc_fd* fd = FD_FROM_PO(item);
      polling_island_add_fds_locked(pi_new, &fd, 1, true, &error);
    }

    gpr_mu_unlock(&pi_new->mu);
    GRPC_POLLING_TRACE(
        "add_poll_obj: item->pi was NULL. pi_new: %p (item(%s): %p, "
        "bag(%s): %p)",
        (void*)pi_new, poll_obj_string(item_type), (void*)item,
        poll_obj_string(bag_type), (void*)bag);
  } else if (bag->pi == nullptr) {
    /* GPR_ASSERT(item->pi != NULL) */
    /* Make pi_new to point to latest pi */
    pi_new = polling_island_lock(item->pi);
    gpr_mu_unlock(&pi_new->mu);
    GRPC_POLLING_TRACE(
        "add_poll_obj: bag->pi was NULL. pi_new: %p (item(%s): %p, "
        "bag(%s): %p)",
        (void*)pi_new, poll_obj_string(item_type), (void*)item,
        poll_obj_string(bag_type), (void*)bag);
  } else {
    pi_new = polling_island_merge(item->pi, bag->pi, &error);
    GRPC_POLLING_TRACE(
        "add_poll_obj: polling islands merged. pi_new: %p (item(%s): %p, "
        "bag(%s): %p)",
        (void*)pi_new, poll_obj_string(item_type), (void*)item,
        poll_obj_string(bag_type), (void*)bag);
  }

  /* At this point, pi_new is the polling island that both item->pi and bag->pi
     MUST be pointing to */

  if (item->pi != pi_new) {
    PI_ADD_REF(pi_new, poll_obj_string(item_type));
    if (item->pi != nullptr) {
      PI_UNREF(item->pi, poll_obj_string(item_type));
    }
    item->pi = pi_new;
  }

  if (bag->pi != pi_new) {
    PI_ADD_REF(pi_new, poll_obj_string(bag_type));
    if (bag->pi != nullptr) {
      PI_UNREF(bag->pi, poll_obj_string(bag_type));
    }
    bag->pi = pi_new;
  }

  gpr_mu_unlock(&item->mu);
  gpr_mu_unlock(&bag->mu);

  GRPC_LOG_IF_ERROR("add_poll_object", error);
}

static void pollset_add_fd(grpc_pollset* pollset, grpc_fd* fd) {
  add_poll_object(&pollset->po, POLL_OBJ_POLLSET, &fd->po, POLL_OBJ_FD);
}

/*******************************************************************************
 * Pollset-set Definitions
 */

static grpc_pollset_set* pollset_set_create(void) {
  grpc_pollset_set* pss =
      static_cast<grpc_pollset_set*>(gpr_malloc(sizeof(*pss)));
  gpr_mu_init(&pss->po.mu);
  pss->po.pi = nullptr;
#ifndef NDEBUG
  pss->po.obj_type = POLL_OBJ_POLLSET_SET;
#endif
  return pss;
}

static void pollset_set_destroy(grpc_pollset_set* pss) {
  gpr_mu_destroy(&pss->po.mu);

  if (pss->po.pi != nullptr) {
    PI_UNREF(pss->po.pi, "pss_destroy");
  }

  gpr_free(pss);
}

static void pollset_set_add_fd(grpc_pollset_set* pss, grpc_fd* fd) {
  add_poll_object(&pss->po, POLL_OBJ_POLLSET_SET, &fd->po, POLL_OBJ_FD);
}

static void pollset_set_del_fd(grpc_pollset_set* pss, grpc_fd* fd) {
  /* Nothing to do */
}

static void pollset_set_add_pollset(grpc_pollset_set* pss, grpc_pollset* ps) {
  add_poll_object(&pss->po, POLL_OBJ_POLLSET_SET, &ps->po, POLL_OBJ_POLLSET);
}

static void pollset_set_del_pollset(grpc_pollset_set* pss, grpc_pollset* ps) {
  /* Nothing to do */
}

static void pollset_set_add_pollset_set(grpc_pollset_set* bag,
                                        grpc_pollset_set* item) {
  add_poll_object(&bag->po, POLL_OBJ_POLLSET_SET, &item->po,
                  POLL_OBJ_POLLSET_SET);
}

static void pollset_set_del_pollset_set(grpc_pollset_set* bag,
                                        grpc_pollset_set* item) {
  /* Nothing to do */
}

/* Test helper functions
 * */
void* grpc_fd_get_polling_island(grpc_fd* fd) {
  polling_island* pi;

  gpr_mu_lock(&fd->po.mu);
  pi = fd->po.pi;
  gpr_mu_unlock(&fd->po.mu);

  return pi;
}

void* grpc_pollset_get_polling_island(grpc_pollset* ps) {
  polling_island* pi;

  gpr_mu_lock(&ps->po.mu);
  pi = ps->po.pi;
  gpr_mu_unlock(&ps->po.mu);

  return pi;
}

bool grpc_are_polling_islands_equal(void* p, void* q) {
  polling_island* p1 = static_cast<polling_island*>(p);
  polling_island* p2 = static_cast<polling_island*>(q);

  /* Note: polling_island_lock_pair() may change p1 and p2 to point to the
     latest polling islands in their respective linked lists */
  polling_island_lock_pair(&p1, &p2);
  polling_island_unlock_pair(p1, p2);

  return p1 == p2;
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
    sizeof(grpc_pollset),

    fd_create,
    fd_wrapped_fd,
    fd_orphan,
    fd_shutdown,
    fd_notify_on_read,
    fd_notify_on_write,
    fd_is_shutdown,
    fd_get_read_notifier_pollset,

    pollset_init,
    pollset_shutdown,
    pollset_destroy,
    pollset_work,
    pollset_kick,
    pollset_add_fd,

    pollset_set_create,
    pollset_set_destroy,
    pollset_set_add_pollset,
    pollset_set_del_pollset,
    pollset_set_add_pollset_set,
    pollset_set_del_pollset_set,
    pollset_set_add_fd,
    pollset_set_del_fd,

    shutdown_engine,
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

const grpc_event_engine_vtable* grpc_init_epollsig_linux(
    bool explicit_request) {
  /* If use of signals is disabled, we cannot use epoll engine*/
  if (is_grpc_wakeup_signal_initialized && grpc_wakeup_signal < 0) {
    gpr_log(GPR_ERROR, "Skipping epollsig because use of signals is disabled.");
    return nullptr;
  }

  if (!grpc_has_wakeup_fd()) {
    gpr_log(GPR_ERROR, "Skipping epollsig because of no wakeup fd.");
    return nullptr;
  }

  if (!is_epoll_available()) {
    gpr_log(GPR_ERROR, "Skipping epollsig because epoll is unavailable.");
    return nullptr;
  }

  if (!is_grpc_wakeup_signal_initialized) {
    if (explicit_request) {
      grpc_use_signal(SIGRTMIN + 6);
    } else {
      gpr_log(GPR_ERROR,
              "Skipping epollsig because uninitialized wakeup signal.");
      return nullptr;
    }
  }

  fd_global_init();

  if (!GRPC_LOG_IF_ERROR("pollset_global_init", pollset_global_init())) {
    return nullptr;
  }

  if (!GRPC_LOG_IF_ERROR("polling_island_global_init",
                         polling_island_global_init())) {
    return nullptr;
  }

  return &vtable;
}

#else /* defined(GRPC_LINUX_EPOLL_CREATE1) */
#if defined(GRPC_POSIX_SOCKET_EV_EPOLLSIG)
#include "src/core/lib/iomgr/ev_epollsig_linux.h"
/* If GRPC_LINUX_EPOLL_CREATE1 is not defined, it means
   epoll_create1 is not available. Return NULL */
const grpc_event_engine_vtable* grpc_init_epollsig_linux(
    bool explicit_request) {
  return nullptr;
}
#endif /* defined(GRPC_POSIX_SOCKET) */

void grpc_use_signal(int signum) {}
#endif /* !defined(GRPC_LINUX_EPOLL_CREATE1) */
