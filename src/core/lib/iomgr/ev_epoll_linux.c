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

#include <grpc/grpc_posix.h>
#include <grpc/support/port_platform.h>

#ifdef GPR_LINUX_EPOLL

#include "src/core/lib/iomgr/ev_epoll_linux.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
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

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/block_annotate.h"

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

/*******************************************************************************
 * Fd Declarations
 */
struct grpc_fd {
  int fd;
  /* refst format:
       bit 0    : 1=Active / 0=Orphaned
       bits 1-n : refcount
     Ref/Unref by two to avoid altering the orphaned bit */
  gpr_atm refst;

  gpr_mu mu;

  /* Indicates that the fd is shutdown and that any pending read/write closures
     should fail */
  bool shutdown;

  /* The fd is either closed or we relinquished control of it. In either cases,
     this indicates that the 'fd' on this structure is no longer valid */
  bool orphaned;

  /* TODO: sreek - Move this to a lockfree implementation */
  grpc_closure *read_closure;
  grpc_closure *write_closure;

  /* The polling island to which this fd belongs to and the mutex protecting the
     the field */
  gpr_mu pi_mu;
  struct polling_island *polling_island;

  struct grpc_fd *freelist_next;
  grpc_closure *on_done_closure;

  /* The pollset that last noticed that the fd is readable */
  grpc_pollset *read_notifier_pollset;

  grpc_iomgr_object iomgr_object;
};

/* Reference counting for fds */
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

#define CLOSURE_NOT_READY ((grpc_closure *)0)
#define CLOSURE_READY ((grpc_closure *)1)

/*******************************************************************************
 * Polling island Declarations
 */

// #define GRPC_PI_REF_COUNT_DEBUG
#ifdef GRPC_PI_REF_COUNT_DEBUG

#define PI_ADD_REF(p, r) pi_add_ref_dbg((p), 1, (r), __FILE__, __LINE__)
#define PI_UNREF(p, r) pi_unref_dbg((p), 1, (r), __FILE__, __LINE__)

#else /* defined(GRPC_PI_REF_COUNT_DEBUG) */

#define PI_ADD_REF(p, r) pi_add_ref((p), 1)
#define PI_UNREF(p, r) pi_unref((p), 1)

#endif /* !defined(GPRC_PI_REF_COUNT_DEBUG) */

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

  /* The fd of the underlying epoll set */
  int epoll_fd;

  /* The file descriptors in the epoll set */
  size_t fd_cnt;
  size_t fd_capacity;
  grpc_fd **fds;

  /* Polling islands that are no longer needed are kept in a freelist so that
     they can be reused. This field points to the next polling island in the
     free list */
  struct polling_island *next_free;
} polling_island;

/*******************************************************************************
 * Pollset Declarations
 */
struct grpc_pollset_worker {
  pthread_t pt_id; /* Thread id of this worker */
  struct grpc_pollset_worker *next;
  struct grpc_pollset_worker *prev;
};

struct grpc_pollset {
  gpr_mu mu;
  grpc_pollset_worker root_worker;
  bool kicked_without_pollers;

  bool shutting_down;          /* Is the pollset shutting down ? */
  bool finish_shutdown_called; /* Is the 'finish_shutdown_locked()' called ? */
  grpc_closure *shutdown_done; /* Called after after shutdown is complete */

  /* The polling island to which this pollset belongs to */
  struct polling_island *polling_island;
};

/*******************************************************************************
 * Pollset-set Declarations
 */
/* TODO: sreek - Change the pollset_set implementation such that a pollset_set
 * directly points to a polling_island (and adding an fd/pollset/pollset_set to
 * the current pollset_set would result in polling island merges. This would
 * remove the need to maintain fd_count here. This will also significantly
 * simplify the grpc_fd structure since we would no longer need to explicitly
 * maintain the orphaned state */
struct grpc_pollset_set {
  gpr_mu mu;

  size_t pollset_count;
  size_t pollset_capacity;
  grpc_pollset **pollsets;

  size_t pollset_set_count;
  size_t pollset_set_capacity;
  struct grpc_pollset_set **pollset_sets;

  size_t fd_count;
  size_t fd_capacity;
  grpc_fd **fds;
};

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

/* Polling island freelist */
static gpr_mu g_pi_freelist_mu;
static polling_island *g_pi_freelist = NULL;

static void polling_island_delete(); /* Forward declaration */

#ifdef GRPC_TSAN
/* Currently TSAN may incorrectly flag data races between epoll_ctl and
   epoll_wait for any grpc_fd structs that are added to the epoll set via
   epoll_ctl and are returned (within a very short window) via epoll_wait().

   To work-around this race, we establish a happens-before relation between
   the code just-before epoll_ctl() and the code after epoll_wait() by using
   this atomic */
gpr_atm g_epoll_sync;
#endif /* defined(GRPC_TSAN) */

#ifdef GRPC_PI_REF_COUNT_DEBUG
long pi_add_ref(polling_island *pi, int ref_cnt);
long pi_unref(polling_island *pi, int ref_cnt);

void pi_add_ref_dbg(polling_island *pi, int ref_cnt, char *reason, char *file,
                    int line) {
  long old_cnt = pi_add_ref(pi, ref_cnt);
  gpr_log(GPR_DEBUG, "Add ref pi: %p, old:%ld -> new:%ld (%s) - (%s, %d)",
          (void *)pi, old_cnt, (old_cnt + ref_cnt), reason, file, line);
}

void pi_unref_dbg(polling_island *pi, int ref_cnt, char *reason, char *file,
                  int line) {
  long old_cnt = pi_unref(pi, ref_cnt);
  gpr_log(GPR_DEBUG, "Unref pi: %p, old:%ld -> new:%ld (%s) - (%s, %d)",
          (void *)pi, old_cnt, (old_cnt - ref_cnt), reason, file, line);
}
#endif

long pi_add_ref(polling_island *pi, int ref_cnt) {
  return gpr_atm_full_fetch_add(&pi->ref_count, ref_cnt);
}

long pi_unref(polling_island *pi, int ref_cnt) {
  long old_cnt = gpr_atm_full_fetch_add(&pi->ref_count, -ref_cnt);

  /* If ref count went to zero, delete the polling island. Note that this need
     not be done under a lock. Once the ref count goes to zero, we are
     guaranteed that no one else holds a reference to the polling island (and
     that there is no racing pi_add_ref() call either.

     Also, if we are deleting the polling island and the merged_to field is
     non-empty, we should remove a ref to the merged_to polling island
   */
  if (old_cnt == ref_cnt) {
    polling_island *next = (polling_island *)gpr_atm_acq_load(&pi->merged_to);
    polling_island_delete(pi);
    if (next != NULL) {
      PI_UNREF(next, "pi_delete"); /* Recursive call */
    }
  } else {
    GPR_ASSERT(old_cnt > ref_cnt);
  }

  return old_cnt;
}

/* The caller is expected to hold pi->mu lock before calling this function */
static void polling_island_add_fds_locked(polling_island *pi, grpc_fd **fds,
                                          size_t fd_count, bool add_fd_refs) {
  int err;
  size_t i;
  struct epoll_event ev;

#ifdef GRPC_TSAN
  /* See the definition of g_epoll_sync for more context */
  gpr_atm_rel_store(&g_epoll_sync, 0);
#endif /* defined(GRPC_TSAN) */

  for (i = 0; i < fd_count; i++) {
    ev.events = (uint32_t)(EPOLLIN | EPOLLOUT | EPOLLET);
    ev.data.ptr = fds[i];
    err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_ADD, fds[i]->fd, &ev);

    if (err < 0) {
      if (errno != EEXIST) {
        /* TODO: sreek - We need a better way to bubble up this error instead of
           just logging a message */
        gpr_log(GPR_ERROR, "epoll_ctl add for fd: %d failed with error: %s",
                fds[i]->fd, strerror(errno));
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
                                                grpc_wakeup_fd *wakeup_fd) {
  struct epoll_event ev;
  int err;

  ev.events = (uint32_t)(EPOLLIN | EPOLLET);
  ev.data.ptr = wakeup_fd;
  err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_ADD,
                  GRPC_WAKEUP_FD_GET_READ_FD(wakeup_fd), &ev);
  if (err < 0) {
    gpr_log(GPR_ERROR,
            "Failed to add grpc_wake_up_fd (%d) to the epoll set (epoll_fd: %d)"
            ". Error: %s",
            GRPC_WAKEUP_FD_GET_READ_FD(&grpc_global_wakeup_fd), pi->epoll_fd,
            strerror(errno));
  }
}

/* The caller is expected to hold pi->mu lock before calling this function */
static void polling_island_remove_all_fds_locked(polling_island *pi,
                                                 bool remove_fd_refs) {
  int err;
  size_t i;

  for (i = 0; i < pi->fd_cnt; i++) {
    err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_DEL, pi->fds[i]->fd, NULL);
    if (err < 0 && errno != ENOENT) {
      /* TODO: sreek - We need a better way to bubble up this error instead of
      * just logging a message */
      gpr_log(GPR_ERROR,
              "epoll_ctl deleting fds[%zu]: %d failed with error: %s", i,
              pi->fds[i]->fd, strerror(errno));
    }

    if (remove_fd_refs) {
      GRPC_FD_UNREF(pi->fds[i], "polling_island");
    }
  }

  pi->fd_cnt = 0;
}

/* The caller is expected to hold pi->mu lock before calling this function */
static void polling_island_remove_fd_locked(polling_island *pi, grpc_fd *fd,
                                            bool is_fd_closed) {
  int err;
  size_t i;

  /* If fd is already closed, then it would have been automatically been removed
     from the epoll set */
  if (!is_fd_closed) {
    err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_DEL, fd->fd, NULL);
    if (err < 0 && errno != ENOENT) {
      gpr_log(GPR_ERROR, "epoll_ctl deleting fd: %d failed with error; %s",
              fd->fd, strerror(errno));
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

static polling_island *polling_island_create(grpc_fd *initial_fd) {
  polling_island *pi = NULL;

  /* Try to get one from the polling island freelist */
  gpr_mu_lock(&g_pi_freelist_mu);
  if (g_pi_freelist != NULL) {
    pi = g_pi_freelist;
    g_pi_freelist = g_pi_freelist->next_free;
    pi->next_free = NULL;
  }
  gpr_mu_unlock(&g_pi_freelist_mu);

  /* Create new polling island if we could not get one from the free list */
  if (pi == NULL) {
    pi = gpr_malloc(sizeof(*pi));
    gpr_mu_init(&pi->mu);
    pi->fd_cnt = 0;
    pi->fd_capacity = 0;
    pi->fds = NULL;
  }

  gpr_atm_rel_store(&pi->ref_count, 0);
  gpr_atm_rel_store(&pi->merged_to, NULL);

  pi->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

  if (pi->epoll_fd < 0) {
    gpr_log(GPR_ERROR, "epoll_create1() failed with error: %s",
            strerror(errno));
  }
  GPR_ASSERT(pi->epoll_fd >= 0);

  polling_island_add_wakeup_fd_locked(pi, &grpc_global_wakeup_fd);

  pi->next_free = NULL;

  if (initial_fd != NULL) {
    /* Lock the polling island here just in case we got this structure from the
       freelist and the polling island lock was not released yet (by the code
       that adds the polling island to the freelist) */
    gpr_mu_lock(&pi->mu);
    polling_island_add_fds_locked(pi, &initial_fd, 1, true);
    gpr_mu_unlock(&pi->mu);
  }

  return pi;
}

static void polling_island_delete(polling_island *pi) {
  GPR_ASSERT(pi->fd_cnt == 0);

  gpr_atm_rel_store(&pi->merged_to, NULL);

  close(pi->epoll_fd);
  pi->epoll_fd = -1;

  gpr_mu_lock(&g_pi_freelist_mu);
  pi->next_free = g_pi_freelist;
  g_pi_freelist = pi;
  gpr_mu_unlock(&g_pi_freelist_mu);
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

/* Gets the lock on the *latest* polling islands pointed by *p and *q.
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
     // Release locks
     // **IMPORTANT**: Make sure you check p1 == p2 AFTER the function
     // polling_island_lock_pair() was called and if so, release the lock only
     // once. Note: Even if p1 != p2 beforec calling polling_island_lock_pair(),
     // they might be after the function returns:
     if (p1 == p2) {
       gpr_mu_unlock(&p1->mu)
     } else {
       gpr_mu_unlock(&p1->mu);
       gpr_mu_unlock(&p2->mu);
     }

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

static polling_island *polling_island_merge(polling_island *p,
                                            polling_island *q) {
  /* Get locks on both the polling islands */
  polling_island_lock_pair(&p, &q);

  if (p == q) {
    /* Nothing needs to be done here */
    gpr_mu_unlock(&p->mu);
    return p;
  }

  /* Make sure that p points to the polling island with fewer fds than q */
  if (p->fd_cnt > q->fd_cnt) {
    GPR_SWAP(polling_island *, p, q);
  }

  /* "Merge" p with q i.e move all the fds from p (The one with fewer fds) to q
      Note that the refcounts on the fds being moved will not change here. This
      is why the last parameter in the following two functions is 'false') */
  polling_island_add_fds_locked(q, p->fds, p->fd_cnt, false);
  polling_island_remove_all_fds_locked(p, false);

  /* Wakeup all the pollers (if any) on p so that they can pickup this change */
  polling_island_add_wakeup_fd_locked(p, &polling_island_wakeup_fd);

  /* Add the 'merged_to' link from p --> q */
  gpr_atm_rel_store(&p->merged_to, q);
  PI_ADD_REF(q, "pi_merge"); /* To account for the new incoming ref from p */

  gpr_mu_unlock(&p->mu);
  gpr_mu_unlock(&q->mu);

  /* Return the merged polling island */
  return q;
}

static grpc_error *polling_island_global_init() {
  grpc_error *error = GRPC_ERROR_NONE;

  gpr_mu_init(&g_pi_freelist_mu);
  g_pi_freelist = NULL;

  error = grpc_wakeup_fd_init(&polling_island_wakeup_fd);
  if (error == GRPC_ERROR_NONE) {
    error = grpc_wakeup_fd_wakeup(&polling_island_wakeup_fd);
  }

  return error;
}

static void polling_island_global_shutdown() {
  polling_island *next;
  gpr_mu_lock(&g_pi_freelist_mu);
  gpr_mu_unlock(&g_pi_freelist_mu);
  while (g_pi_freelist != NULL) {
    next = g_pi_freelist->next_free;
    gpr_mu_destroy(&g_pi_freelist->mu);
    gpr_free(g_pi_freelist->fds);
    gpr_free(g_pi_freelist);
    g_pi_freelist = next;
  }
  gpr_mu_destroy(&g_pi_freelist_mu);

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

/* TODO: sreek: Right now, this wakes up all pollers. In future we should make
 * sure to wake up one polling thread (which can wake up other threads if
 * needed) */
grpc_wakeup_fd grpc_global_wakeup_fd;

static grpc_fd *fd_freelist = NULL;
static gpr_mu fd_freelist_mu;

#ifdef GRPC_FD_REF_COUNT_DEBUG
#define REF_BY(fd, n, reason) ref_by(fd, n, reason, __FILE__, __LINE__)
#define UNREF_BY(fd, n, reason) unref_by(fd, n, reason, __FILE__, __LINE__)
static void ref_by(grpc_fd *fd, int n, const char *reason, const char *file,
                   int line) {
  gpr_log(GPR_DEBUG, "FD %d %p   ref %d %d -> %d [%s; %s:%d]", fd->fd, fd, n,
          gpr_atm_no_barrier_load(&fd->refst),
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
  gpr_log(GPR_DEBUG, "FD %d %p unref %d %d -> %d [%s; %s:%d]", fd->fd, fd, n,
          gpr_atm_no_barrier_load(&fd->refst),
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
    gpr_mu_init(&new_fd->pi_mu);
  }

  /* Note: It is not really needed to get the new_fd->mu lock here. If this is a
     newly created fd (or an fd we got from the freelist), no one else would be
     holding a lock to it anyway. */
  gpr_mu_lock(&new_fd->mu);

  gpr_atm_rel_store(&new_fd->refst, 1);
  new_fd->fd = fd;
  new_fd->shutdown = false;
  new_fd->orphaned = false;
  new_fd->read_closure = CLOSURE_NOT_READY;
  new_fd->write_closure = CLOSURE_NOT_READY;
  new_fd->polling_island = NULL;
  new_fd->freelist_next = NULL;
  new_fd->on_done_closure = NULL;
  new_fd->read_notifier_pollset = NULL;

  gpr_mu_unlock(&new_fd->mu);

  char *fd_name;
  gpr_asprintf(&fd_name, "%s fd=%d", name, fd);
  grpc_iomgr_register_object(&new_fd->iomgr_object, fd_name);
  gpr_free(fd_name);
#ifdef GRPC_FD_REF_COUNT_DEBUG
  gpr_log(GPR_DEBUG, "FD %d %p create %s", fd, r, fd_name);
#endif
  return new_fd;
}

static bool fd_is_orphaned(grpc_fd *fd) {
  return (gpr_atm_acq_load(&fd->refst) & 1) == 0;
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

  /* Remove the fd from the polling island:
     - Get a lock on the latest polling island (i.e the last island in the
       linked list pointed by fd->polling_island). This is the island that
       would actually contain the fd
     - Remove the fd from the latest polling island
     - Unlock the latest polling island
     - Set fd->polling_island to NULL (but remove the ref on the polling island
       before doing this.) */
  gpr_mu_lock(&fd->pi_mu);
  if (fd->polling_island != NULL) {
    polling_island *pi_latest = polling_island_lock(fd->polling_island);
    polling_island_remove_fd_locked(pi_latest, fd, is_fd_closed);
    gpr_mu_unlock(&pi_latest->mu);

    PI_UNREF(fd->polling_island, "fd_orphan");
    fd->polling_island = NULL;
  }
  gpr_mu_unlock(&fd->pi_mu);

  grpc_exec_ctx_sched(exec_ctx, fd->on_done_closure, GRPC_ERROR_NONE, NULL);

  gpr_mu_unlock(&fd->mu);
  UNREF_BY(fd, 2, reason); /* Drop the reference */
}

static grpc_error *fd_shutdown_error(bool shutdown) {
  if (!shutdown) {
    return GRPC_ERROR_NONE;
  } else {
    return GRPC_ERROR_CREATE("FD shutdown");
  }
}

static void notify_on_locked(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                             grpc_closure **st, grpc_closure *closure) {
  if (fd->shutdown) {
    grpc_exec_ctx_sched(exec_ctx, closure, GRPC_ERROR_CREATE("FD shutdown"),
                        NULL);
  } else if (*st == CLOSURE_NOT_READY) {
    /* not ready ==> switch to a waiting state by setting the closure */
    *st = closure;
  } else if (*st == CLOSURE_READY) {
    /* already ready ==> queue the closure to run immediately */
    *st = CLOSURE_NOT_READY;
    grpc_exec_ctx_sched(exec_ctx, closure, fd_shutdown_error(fd->shutdown),
                        NULL);
  } else {
    /* upcallptr was set to a different closure.  This is an error! */
    gpr_log(GPR_ERROR,
            "User called a notify_on function with a previous callback still "
            "pending");
    abort();
  }
}

/* returns 1 if state becomes not ready */
static int set_ready_locked(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                            grpc_closure **st) {
  if (*st == CLOSURE_READY) {
    /* duplicate ready ==> ignore */
    return 0;
  } else if (*st == CLOSURE_NOT_READY) {
    /* not ready, and not waiting ==> flag ready */
    *st = CLOSURE_READY;
    return 0;
  } else {
    /* waiting ==> queue closure */
    grpc_exec_ctx_sched(exec_ctx, *st, fd_shutdown_error(fd->shutdown), NULL);
    *st = CLOSURE_NOT_READY;
    return 1;
  }
}

static grpc_pollset *fd_get_read_notifier_pollset(grpc_exec_ctx *exec_ctx,
                                                  grpc_fd *fd) {
  grpc_pollset *notifier = NULL;

  gpr_mu_lock(&fd->mu);
  notifier = fd->read_notifier_pollset;
  gpr_mu_unlock(&fd->mu);

  return notifier;
}

static bool fd_is_shutdown(grpc_fd *fd) {
  gpr_mu_lock(&fd->mu);
  const bool r = fd->shutdown;
  gpr_mu_unlock(&fd->mu);
  return r;
}

/* Might be called multiple times */
static void fd_shutdown(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  gpr_mu_lock(&fd->mu);
  /* Do the actual shutdown only once */
  if (!fd->shutdown) {
    fd->shutdown = true;

    shutdown(fd->fd, SHUT_RDWR);
    /* Flush any pending read and write closures. Since fd->shutdown is 'true'
       at this point, the closures would be called with 'success = false' */
    set_ready_locked(exec_ctx, fd, &fd->read_closure);
    set_ready_locked(exec_ctx, fd, &fd->write_closure);
  }
  gpr_mu_unlock(&fd->mu);
}

static void fd_notify_on_read(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                              grpc_closure *closure) {
  gpr_mu_lock(&fd->mu);
  notify_on_locked(exec_ctx, fd, &fd->read_closure, closure);
  gpr_mu_unlock(&fd->mu);
}

static void fd_notify_on_write(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                               grpc_closure *closure) {
  gpr_mu_lock(&fd->mu);
  notify_on_locked(exec_ctx, fd, &fd->write_closure, closure);
  gpr_mu_unlock(&fd->mu);
}

/*******************************************************************************
 * Pollset Definitions
 */
GPR_TLS_DECL(g_current_thread_pollset);
GPR_TLS_DECL(g_current_thread_worker);

static void sig_handler(int sig_num) {
#ifdef GRPC_EPOLL_DEBUG
  gpr_log(GPR_INFO, "Received signal %d", sig_num);
#endif
}

static void poller_kick_init() { signal(grpc_wakeup_signal, sig_handler); }

/* Global state management */
static grpc_error *pollset_global_init(void) {
  gpr_tls_init(&g_current_thread_pollset);
  gpr_tls_init(&g_current_thread_worker);
  poller_kick_init();
  return grpc_wakeup_fd_init(&grpc_global_wakeup_fd);
}

static void pollset_global_shutdown(void) {
  grpc_wakeup_fd_destroy(&grpc_global_wakeup_fd);
  gpr_tls_destroy(&g_current_thread_pollset);
  gpr_tls_destroy(&g_current_thread_worker);
}

static grpc_error *pollset_worker_kick(grpc_pollset_worker *worker) {
  grpc_error *err = GRPC_ERROR_NONE;
  int err_num = pthread_kill(worker->pt_id, grpc_wakeup_signal);
  if (err_num != 0) {
    err = GRPC_OS_ERROR(err_num, "pthread_kill");
  }
  return err;
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

static void kick_append_error(grpc_error **composite, grpc_error *error) {
  if (error == GRPC_ERROR_NONE) return;
  if (*composite == GRPC_ERROR_NONE) {
    *composite = GRPC_ERROR_CREATE("Kick Failure");
  }
  *composite = grpc_error_add_child(*composite, error);
}

/* p->mu must be held before calling this function */
static grpc_error *pollset_kick(grpc_pollset *p,
                                grpc_pollset_worker *specific_worker) {
  GPR_TIMER_BEGIN("pollset_kick", 0);
  grpc_error *error = GRPC_ERROR_NONE;

  grpc_pollset_worker *worker = specific_worker;
  if (worker != NULL) {
    if (worker == GRPC_POLLSET_KICK_BROADCAST) {
      if (pollset_has_workers(p)) {
        GPR_TIMER_BEGIN("pollset_kick.broadcast", 0);
        for (worker = p->root_worker.next; worker != &p->root_worker;
             worker = worker->next) {
          if (gpr_tls_get(&g_current_thread_worker) != (intptr_t)worker) {
            kick_append_error(&error, pollset_worker_kick(worker));
          }
        }
      } else {
        p->kicked_without_pollers = true;
      }
      GPR_TIMER_END("pollset_kick.broadcast", 0);
    } else {
      GPR_TIMER_MARK("kicked_specifically", 0);
      if (gpr_tls_get(&g_current_thread_worker) != (intptr_t)worker) {
        kick_append_error(&error, pollset_worker_kick(worker));
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
      kick_append_error(&error, pollset_worker_kick(worker));
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
  return grpc_wakeup_fd_wakeup(&grpc_global_wakeup_fd);
}

static void pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  gpr_mu_init(&pollset->mu);
  *mu = &pollset->mu;

  pollset->root_worker.next = pollset->root_worker.prev = &pollset->root_worker;
  pollset->kicked_without_pollers = false;

  pollset->shutting_down = false;
  pollset->finish_shutdown_called = false;
  pollset->shutdown_done = NULL;

  pollset->polling_island = NULL;
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
  return gpr_time_to_millis(gpr_time_add(
      timeout, gpr_time_from_nanos(GPR_NS_PER_MS - 1, GPR_TIMESPAN)));
}

static void fd_become_readable(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                               grpc_pollset *notifier) {
  /* Need the fd->mu since we might be racing with fd_notify_on_read */
  gpr_mu_lock(&fd->mu);
  set_ready_locked(exec_ctx, fd, &fd->read_closure);
  fd->read_notifier_pollset = notifier;
  gpr_mu_unlock(&fd->mu);
}

static void fd_become_writable(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  /* Need the fd->mu since we might be racing with fd_notify_on_write */
  gpr_mu_lock(&fd->mu);
  set_ready_locked(exec_ctx, fd, &fd->write_closure);
  gpr_mu_unlock(&fd->mu);
}

static void pollset_release_polling_island(grpc_pollset *ps, char *reason) {
  if (ps->polling_island != NULL) {
    PI_UNREF(ps->polling_island, reason);
  }
  ps->polling_island = NULL;
}

static void finish_shutdown_locked(grpc_exec_ctx *exec_ctx,
                                   grpc_pollset *pollset) {
  /* The pollset cannot have any workers if we are at this stage */
  GPR_ASSERT(!pollset_has_workers(pollset));

  pollset->finish_shutdown_called = true;

  /* Release the ref and set pollset->polling_island to NULL */
  pollset_release_polling_island(pollset, "ps_shutdown");
  grpc_exec_ctx_sched(exec_ctx, pollset->shutdown_done, GRPC_ERROR_NONE, NULL);
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

static void pollset_reset(grpc_pollset *pollset) {
  GPR_ASSERT(pollset->shutting_down);
  GPR_ASSERT(!pollset_has_workers(pollset));
  pollset->shutting_down = false;
  pollset->finish_shutdown_called = false;
  pollset->kicked_without_pollers = false;
  pollset->shutdown_done = NULL;
  pollset_release_polling_island(pollset, "ps_reset");
}

static void work_combine_error(grpc_error **composite, grpc_error *error) {
  if (error == GRPC_ERROR_NONE) return;
  if (*composite == GRPC_ERROR_NONE) {
    *composite = GRPC_ERROR_CREATE("pollset_work");
  }
  *composite = grpc_error_add_child(*composite, error);
}

#define GRPC_EPOLL_MAX_EVENTS 1000
static grpc_error *pollset_work_and_unlock(grpc_exec_ctx *exec_ctx,
                                           grpc_pollset *pollset,
                                           int timeout_ms, sigset_t *sig_mask) {
  struct epoll_event ep_ev[GRPC_EPOLL_MAX_EVENTS];
  int epoll_fd = -1;
  int ep_rv;
  polling_island *pi = NULL;
  grpc_error *error = GRPC_ERROR_NONE;
  GPR_TIMER_BEGIN("pollset_work_and_unlock", 0);

  /* We need to get the epoll_fd to wait on. The epoll_fd is in inside the
     latest polling island pointed by pollset->polling_island.

     Since epoll_fd is immutable, we can read it without obtaining the polling
     island lock. There is however a possibility that the polling island (from
     which we got the epoll_fd) got merged with another island while we are
     in this function. This is still okay because in such a case, we will wakeup
     right-away from epoll_wait() and pick up the latest polling_island the next
     this function (i.e pollset_work_and_unlock()) is called.
     */

  if (pollset->polling_island == NULL) {
    pollset->polling_island = polling_island_create(NULL);
    PI_ADD_REF(pollset->polling_island, "ps");
  }

  pi = polling_island_maybe_get_latest(pollset->polling_island);
  epoll_fd = pi->epoll_fd;

  /* Update the pollset->polling_island since the island being pointed by
     pollset->polling_island maybe older than the one pointed by pi) */
  if (pollset->polling_island != pi) {
    /* Always do PI_ADD_REF before PI_UNREF because PI_UNREF may cause the
       polling island to be deleted */
    PI_ADD_REF(pi, "ps");
    PI_UNREF(pollset->polling_island, "ps");
    pollset->polling_island = pi;
  }

  /* Add an extra ref so that the island does not get destroyed (which means
     the epoll_fd won't be closed) while we are are doing an epoll_wait() on the
     epoll_fd */
  PI_ADD_REF(pi, "ps_work");
  gpr_mu_unlock(&pollset->mu);

  do {
    ep_rv = epoll_pwait(epoll_fd, ep_ev, GRPC_EPOLL_MAX_EVENTS, timeout_ms,
                        sig_mask);
    if (ep_rv < 0) {
      if (errno != EINTR) {
        gpr_log(GPR_ERROR, "epoll_pwait() failed: %s", strerror(errno));
        work_combine_error(&error, GRPC_OS_ERROR(errno, "epoll_pwait"));
      } else {
        /* We were interrupted. Save an interation by doing a zero timeout
           epoll_wait to see if there are any other events of interest */
        ep_rv = epoll_wait(epoll_fd, ep_ev, GRPC_EPOLL_MAX_EVENTS, 0);
      }
    }

#ifdef GRPC_TSAN
    /* See the definition of g_poll_sync for more details */
    gpr_atm_acq_load(&g_epoll_sync);
#endif /* defined(GRPC_TSAN) */

    for (int i = 0; i < ep_rv; ++i) {
      void *data_ptr = ep_ev[i].data.ptr;
      if (data_ptr == &grpc_global_wakeup_fd) {
        work_combine_error(
            &error, grpc_wakeup_fd_consume_wakeup(&grpc_global_wakeup_fd));
      } else if (data_ptr == &polling_island_wakeup_fd) {
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
  } while (ep_rv == GRPC_EPOLL_MAX_EVENTS);

  GPR_ASSERT(pi != NULL);

  /* Before leaving, release the extra ref we added to the polling island. It
     is important to use "pi" here (i.e our old copy of pollset->polling_island
     that we got before releasing the polling island lock). This is because
     pollset->polling_island pointer might get udpated in other parts of the
     code when there is an island merge while we are doing epoll_wait() above */
  PI_UNREF(pi, "ps_work");

  GPR_TIMER_END("pollset_work_and_unlock", 0);
  return error;
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
  int timeout_ms = poll_deadline_to_millis_timeout(deadline, now);

  sigset_t new_mask;
  sigset_t orig_mask;

  grpc_pollset_worker worker;
  worker.next = worker.prev = NULL;
  worker.pt_id = pthread_self();

  *worker_hdl = &worker;
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
    sigemptyset(&new_mask);
    sigaddset(&new_mask, grpc_wakeup_signal);
    pthread_sigmask(SIG_BLOCK, &new_mask, &orig_mask);
    sigdelset(&orig_mask, grpc_wakeup_signal);

    push_front_worker(pollset, &worker);

    error = pollset_work_and_unlock(exec_ctx, pollset, timeout_ms, &orig_mask);
    grpc_exec_ctx_flush(exec_ctx);

    gpr_mu_lock(&pollset->mu);
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

  *worker_hdl = NULL;
  gpr_tls_set(&g_current_thread_pollset, (intptr_t)0);
  gpr_tls_set(&g_current_thread_worker, (intptr_t)0);
  GPR_TIMER_END("pollset_work", 0);
  GRPC_LOG_IF_ERROR("pollset_work", GRPC_ERROR_REF(error));
  return error;
}

static void pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_fd *fd) {
  gpr_mu_lock(&pollset->mu);
  gpr_mu_lock(&fd->pi_mu);

  polling_island *pi_new = NULL;

  /* 1) If fd->polling_island and pollset->polling_island are both non-NULL and
   *    equal, do nothing.
   * 2) If fd->polling_island and pollset->polling_island are both NULL, create
   *    a new polling island (with a refcount of 2) and make the polling_island
   *    fields in both fd and pollset to point to the new island
   * 3) If one of fd->polling_island or pollset->polling_island is NULL, update
   *    the NULL polling_island field to point to the non-NULL polling_island
   *    field (ensure that the refcount on the polling island is incremented by
   *    1 to account for the newly added reference)
   * 4) Finally, if fd->polling_island and pollset->polling_island are non-NULL
   *    and different, merge both the polling islands and update the
   *    polling_island fields in both fd and pollset to point to the merged
   *    polling island.
   */
  if (fd->polling_island == pollset->polling_island) {
    pi_new = fd->polling_island;
    if (pi_new == NULL) {
      pi_new = polling_island_create(fd);
    }
  } else if (fd->polling_island == NULL) {
    pi_new = polling_island_lock(pollset->polling_island);
    polling_island_add_fds_locked(pi_new, &fd, 1, true);
    gpr_mu_unlock(&pi_new->mu);
  } else if (pollset->polling_island == NULL) {
    pi_new = polling_island_lock(fd->polling_island);
    gpr_mu_unlock(&pi_new->mu);
  } else {
    pi_new = polling_island_merge(fd->polling_island, pollset->polling_island);
  }

  if (fd->polling_island != pi_new) {
    PI_ADD_REF(pi_new, "fd");
    if (fd->polling_island != NULL) {
      PI_UNREF(fd->polling_island, "fd");
    }
    fd->polling_island = pi_new;
  }

  if (pollset->polling_island != pi_new) {
    PI_ADD_REF(pi_new, "ps");
    if (pollset->polling_island != NULL) {
      PI_UNREF(pollset->polling_island, "ps");
    }
    pollset->polling_island = pi_new;
  }

  gpr_mu_unlock(&fd->pi_mu);
  gpr_mu_unlock(&pollset->mu);
}

/*******************************************************************************
 * Pollset-set Definitions
 */

static grpc_pollset_set *pollset_set_create(void) {
  grpc_pollset_set *pollset_set = gpr_malloc(sizeof(*pollset_set));
  memset(pollset_set, 0, sizeof(*pollset_set));
  gpr_mu_init(&pollset_set->mu);
  return pollset_set;
}

static void pollset_set_destroy(grpc_pollset_set *pollset_set) {
  size_t i;
  gpr_mu_destroy(&pollset_set->mu);
  for (i = 0; i < pollset_set->fd_count; i++) {
    GRPC_FD_UNREF(pollset_set->fds[i], "pollset_set");
  }
  gpr_free(pollset_set->pollsets);
  gpr_free(pollset_set->pollset_sets);
  gpr_free(pollset_set->fds);
  gpr_free(pollset_set);
}

static void pollset_set_add_fd(grpc_exec_ctx *exec_ctx,
                               grpc_pollset_set *pollset_set, grpc_fd *fd) {
  size_t i;
  gpr_mu_lock(&pollset_set->mu);
  if (pollset_set->fd_count == pollset_set->fd_capacity) {
    pollset_set->fd_capacity = GPR_MAX(8, 2 * pollset_set->fd_capacity);
    pollset_set->fds = gpr_realloc(
        pollset_set->fds, pollset_set->fd_capacity * sizeof(*pollset_set->fds));
  }
  GRPC_FD_REF(fd, "pollset_set");
  pollset_set->fds[pollset_set->fd_count++] = fd;
  for (i = 0; i < pollset_set->pollset_count; i++) {
    pollset_add_fd(exec_ctx, pollset_set->pollsets[i], fd);
  }
  for (i = 0; i < pollset_set->pollset_set_count; i++) {
    pollset_set_add_fd(exec_ctx, pollset_set->pollset_sets[i], fd);
  }
  gpr_mu_unlock(&pollset_set->mu);
}

static void pollset_set_del_fd(grpc_exec_ctx *exec_ctx,
                               grpc_pollset_set *pollset_set, grpc_fd *fd) {
  size_t i;
  gpr_mu_lock(&pollset_set->mu);
  for (i = 0; i < pollset_set->fd_count; i++) {
    if (pollset_set->fds[i] == fd) {
      pollset_set->fd_count--;
      GPR_SWAP(grpc_fd *, pollset_set->fds[i],
               pollset_set->fds[pollset_set->fd_count]);
      GRPC_FD_UNREF(fd, "pollset_set");
      break;
    }
  }
  for (i = 0; i < pollset_set->pollset_set_count; i++) {
    pollset_set_del_fd(exec_ctx, pollset_set->pollset_sets[i], fd);
  }
  gpr_mu_unlock(&pollset_set->mu);
}

static void pollset_set_add_pollset(grpc_exec_ctx *exec_ctx,
                                    grpc_pollset_set *pollset_set,
                                    grpc_pollset *pollset) {
  size_t i, j;
  gpr_mu_lock(&pollset_set->mu);
  if (pollset_set->pollset_count == pollset_set->pollset_capacity) {
    pollset_set->pollset_capacity =
        GPR_MAX(8, 2 * pollset_set->pollset_capacity);
    pollset_set->pollsets =
        gpr_realloc(pollset_set->pollsets, pollset_set->pollset_capacity *
                                               sizeof(*pollset_set->pollsets));
  }
  pollset_set->pollsets[pollset_set->pollset_count++] = pollset;
  for (i = 0, j = 0; i < pollset_set->fd_count; i++) {
    if (fd_is_orphaned(pollset_set->fds[i])) {
      GRPC_FD_UNREF(pollset_set->fds[i], "pollset_set");
    } else {
      pollset_add_fd(exec_ctx, pollset, pollset_set->fds[i]);
      pollset_set->fds[j++] = pollset_set->fds[i];
    }
  }
  pollset_set->fd_count = j;
  gpr_mu_unlock(&pollset_set->mu);
}

static void pollset_set_del_pollset(grpc_exec_ctx *exec_ctx,
                                    grpc_pollset_set *pollset_set,
                                    grpc_pollset *pollset) {
  size_t i;
  gpr_mu_lock(&pollset_set->mu);
  for (i = 0; i < pollset_set->pollset_count; i++) {
    if (pollset_set->pollsets[i] == pollset) {
      pollset_set->pollset_count--;
      GPR_SWAP(grpc_pollset *, pollset_set->pollsets[i],
               pollset_set->pollsets[pollset_set->pollset_count]);
      break;
    }
  }
  gpr_mu_unlock(&pollset_set->mu);
}

static void pollset_set_add_pollset_set(grpc_exec_ctx *exec_ctx,
                                        grpc_pollset_set *bag,
                                        grpc_pollset_set *item) {
  size_t i, j;
  gpr_mu_lock(&bag->mu);
  if (bag->pollset_set_count == bag->pollset_set_capacity) {
    bag->pollset_set_capacity = GPR_MAX(8, 2 * bag->pollset_set_capacity);
    bag->pollset_sets =
        gpr_realloc(bag->pollset_sets,
                    bag->pollset_set_capacity * sizeof(*bag->pollset_sets));
  }
  bag->pollset_sets[bag->pollset_set_count++] = item;
  for (i = 0, j = 0; i < bag->fd_count; i++) {
    if (fd_is_orphaned(bag->fds[i])) {
      GRPC_FD_UNREF(bag->fds[i], "pollset_set");
    } else {
      pollset_set_add_fd(exec_ctx, item, bag->fds[i]);
      bag->fds[j++] = bag->fds[i];
    }
  }
  bag->fd_count = j;
  gpr_mu_unlock(&bag->mu);
}

static void pollset_set_del_pollset_set(grpc_exec_ctx *exec_ctx,
                                        grpc_pollset_set *bag,
                                        grpc_pollset_set *item) {
  size_t i;
  gpr_mu_lock(&bag->mu);
  for (i = 0; i < bag->pollset_set_count; i++) {
    if (bag->pollset_sets[i] == item) {
      bag->pollset_set_count--;
      GPR_SWAP(grpc_pollset_set *, bag->pollset_sets[i],
               bag->pollset_sets[bag->pollset_set_count]);
      break;
    }
  }
  gpr_mu_unlock(&bag->mu);
}

/* Test helper functions
 * */
void *grpc_fd_get_polling_island(grpc_fd *fd) {
  polling_island *pi;

  gpr_mu_lock(&fd->pi_mu);
  pi = fd->polling_island;
  gpr_mu_unlock(&fd->pi_mu);

  return pi;
}

void *grpc_pollset_get_polling_island(grpc_pollset *ps) {
  polling_island *pi;

  gpr_mu_lock(&ps->mu);
  pi = ps->polling_island;
  gpr_mu_unlock(&ps->mu);

  return pi;
}

bool grpc_are_polling_islands_equal(void *p, void *q) {
  polling_island *p1 = p;
  polling_island *p2 = q;

  polling_island_lock_pair(&p1, &p2);
  if (p1 == p2) {
    gpr_mu_unlock(&p1->mu);
  } else {
    gpr_mu_unlock(&p1->mu);
    gpr_mu_unlock(&p2->mu);
  }

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
    .pollset_size = sizeof(grpc_pollset),

    .fd_create = fd_create,
    .fd_wrapped_fd = fd_wrapped_fd,
    .fd_orphan = fd_orphan,
    .fd_shutdown = fd_shutdown,
    .fd_is_shutdown = fd_is_shutdown,
    .fd_notify_on_read = fd_notify_on_read,
    .fd_notify_on_write = fd_notify_on_write,
    .fd_get_read_notifier_pollset = fd_get_read_notifier_pollset,

    .pollset_init = pollset_init,
    .pollset_shutdown = pollset_shutdown,
    .pollset_reset = pollset_reset,
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

const grpc_event_engine_vtable *grpc_init_epoll_linux(void) {
  /* If use of signals is disabled, we cannot use epoll engine*/
  if (is_grpc_wakeup_signal_initialized && grpc_wakeup_signal < 0) {
    return NULL;
  }

  if (!is_epoll_available()) {
    return NULL;
  }

  if (!is_grpc_wakeup_signal_initialized) {
    grpc_use_signal(SIGRTMIN + 2);
  }

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

#else /* defined(GPR_LINUX_EPOLL) */
#if defined(GPR_POSIX_SOCKET)
#include "src/core/lib/iomgr/ev_posix.h"
/* If GPR_LINUX_EPOLL is not defined, it means epoll is not available. Return
 * NULL */
const grpc_event_engine_vtable *grpc_init_epoll_linux(void) { return NULL; }
#endif /* defined(GPR_POSIX_SOCKET) */

void grpc_use_signal(int signum) {}
#endif /* !defined(GPR_LINUX_EPOLL) */
