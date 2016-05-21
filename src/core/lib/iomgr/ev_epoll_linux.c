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

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SOCKET

#include "src/core/lib/iomgr/ev_epoll_posix.h"

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

struct polling_island;

/*******************************************************************************
 * FD declarations
 */

struct grpc_fd {
  int fd;
  /* refst format:
     bit0:   1=active/0=orphaned
     bit1-n: refcount
     meaning that mostly we ref by two to avoid altering the orphaned bit,
     and just unref by 1 when we're ready to flag the object as orphaned */
  gpr_atm refst;

  gpr_mu mu;
  int shutdown;
  int closed;
  int released;

  grpc_closure *read_closure;
  grpc_closure *write_closure;

  /* Mutex protecting the 'polling_island' field */
  gpr_mu pi_mu;

  /* The polling island to which this fd belongs to. An fd belongs to exactly
     one polling island */
  struct polling_island *polling_island;

  struct grpc_fd *freelist_next;

  grpc_closure *on_done_closure;

  grpc_iomgr_object iomgr_object;
};

/* Return 1 if this fd is orphaned, 0 otherwise */
static bool fd_is_orphaned(grpc_fd *fd);

/* Reference counting for fds */
/*#define GRPC_FD_REF_COUNT_DEBUG*/
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
 * Polling Island
 */
typedef struct polling_island {
  gpr_mu mu;
  int ref_cnt;

  /* Pointer to the polling_island this merged into. If this is not NULL, all
     the remaining fields in this pollset (i.e all fields except mu and ref_cnt)
     are considered invalid and must be ignored */
  struct polling_island *merged_to;

  /* The fd of the underlying epoll set */
  int epoll_fd;

  /* The file descriptors in the epoll set */
  size_t fd_cnt;
  size_t fd_capacity;
  grpc_fd **fds;

  /* Polling islands that are no longer needed are kept in a freelist so that
     they can be reused. This field points to the next polling island in the
     free list. Note that this is only used if the polling island is in the
     free list */
  struct polling_island *next_free;
} polling_island;

/* Polling island freelist */
static gpr_mu g_pi_freelist_mu;
static polling_island *g_pi_freelist = NULL;

/* TODO: sreek - Should we hold a lock on fd or add a ref to the fd ?
 * TODO: sreek - Should this add a ref to the grpc_fd ? */
/* The caller is expected to hold pi->mu lock before calling this function */
static void polling_island_add_fds_locked(polling_island *pi, grpc_fd **fds,
                                          size_t fd_count) {
  int err;
  size_t i;
  struct epoll_event ev;

  for (i = 0; i < fd_count; i++) {
    ev.events = (uint32_t)(EPOLLIN | EPOLLOUT | EPOLLET);
    ev.data.ptr = fds[i];
    err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_ADD, fds[i]->fd, &ev);

    if (err < 0 && errno != EEXIST) {
      gpr_log(GPR_ERROR, "epoll_ctl add for fd: %d failed with error: %s",
              fds[i]->fd, strerror(errno));
      /* TODO: sreek - Not sure if it is a good idea to continue here. We need a
       * better way to bubble up this error instead of doing an abort() */
      continue;
    }

    if (pi->fd_cnt == pi->fd_capacity) {
      pi->fd_capacity = GPR_MAX(pi->fd_capacity + 8, pi->fd_cnt * 3 / 2);
      pi->fds = gpr_realloc(pi->fds, sizeof(grpc_fd *) * pi->fd_capacity);
    }

    pi->fds[pi->fd_cnt++] = fds[i];
  }
}

/* TODO: sreek - Should we hold a lock on fd or add a ref to the fd ?
 * TODO: sreek - Might have to unref the fds (assuming whether we add a ref to
 * the fd when adding it to the epollset) */
/* The caller is expected to hold pi->mu lock before calling this function */
static void polling_island_clear_fds_locked(polling_island *pi) {
  int err;
  size_t i;

  for (i = 0; i < pi->fd_cnt; i++) {
    err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_DEL, pi->fds[i]->fd, NULL);

    if (err < 0 && errno != ENOENT) {
      gpr_log(GPR_ERROR,
              "epoll_ctl delete for fds[i]: %d failed with error: %s", i,
              pi->fds[i]->fd, strerror(errno));
      /* TODO: sreek - Not sure if it is a good idea to continue here. We need a
       * better way to bubble up this error instead of doing an abort() */
      continue;
    }
  }

  pi->fd_cnt = 0;
}

/* TODO: sreek - Should we hold a lock on fd or add a ref to the fd ?
 * TODO: sreek - Might have to unref the fd (assuming whether we add a ref to
 * the fd when adding it to the epollset) */
/* The caller is expected to hold pi->mu lock before calling this function */
static void polling_island_remove_fd_locked(polling_island *pi, grpc_fd *fd) {
  int err;
  size_t i;
  err = epoll_ctl(pi->epoll_fd, EPOLL_CTL_DEL, fd->fd, NULL);
  if (err < 0 && errno != ENOENT) {
    gpr_log(GPR_ERROR, "epoll_ctl delete for fd: %d failed with error; %s",
            fd->fd, strerror(errno));
  }

  for (i = 0; i < pi->fd_cnt; i++) {
    if (pi->fds[i] == fd) {
      pi->fds[i] = pi->fds[--pi->fd_cnt];
      break;
    }
  }
}

static polling_island *polling_island_create(grpc_fd *initial_fd,
                                             int initial_ref_cnt) {
  polling_island *pi = NULL;
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

    pi->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (pi->epoll_fd < 0) {
      gpr_log(GPR_ERROR, "epoll_create1() failed with error: %s",
              strerror(errno));
    }
    GPR_ASSERT(pi->epoll_fd >= 0);
  }

  pi->ref_cnt = initial_ref_cnt;
  pi->merged_to = NULL;
  pi->next_free = NULL;

  if (initial_fd != NULL) {
    /* polling_island_add_fds_locked() expects the caller to hold a pi->mu
     * lock. However, since this is a new polling island (and no one has a
     * reference to it yet), it is okay to not acquire pi->mu here */
    polling_island_add_fds_locked(pi, &initial_fd, 1);
  }

  return pi;
}

static void polling_island_delete(polling_island *pi) {
  GPR_ASSERT(pi->ref_cnt == 0);
  GPR_ASSERT(pi->fd_cnt == 0);

  pi->merged_to = NULL;

  gpr_mu_lock(&g_pi_freelist_mu);
  pi->next_free = g_pi_freelist;
  g_pi_freelist = pi;
  gpr_mu_unlock(&g_pi_freelist_mu);
}

void polling_island_unref_and_unlock(polling_island *pi, int unref_by) {
  pi->ref_cnt -= unref_by;
  int ref_cnt = pi->ref_cnt;
  GPR_ASSERT(ref_cnt >= 0);

  gpr_mu_unlock(&pi->mu);

  if (ref_cnt == 0) {
    polling_island_delete(pi);
  }
}

polling_island *polling_island_update_and_lock(polling_island *pi, int unref_by,
                                               int add_ref_by) {
  polling_island *next = NULL;
  gpr_mu_lock(&pi->mu);
  while (pi->merged_to != NULL) {
    next = pi->merged_to;
    polling_island_unref_and_unlock(pi, unref_by);
    pi = next;
    gpr_mu_lock(&pi->mu);
  }

  pi->ref_cnt += add_ref_by;
  return pi;
}

void polling_island_pair_update_and_lock(polling_island **p,
                                         polling_island **q) {
  polling_island *pi_1 = *p;
  polling_island *pi_2 = *q;
  polling_island *temp = NULL;
  bool pi_1_locked = false;
  bool pi_2_locked = false;
  int num_swaps = 0;

  while (pi_1 != pi_2 && !(pi_1_locked && pi_2_locked)) {
    // pi_1 is NOT equal to pi_2
    // pi_1 MAY be locked

    if (pi_1 > pi_2) {
      if (pi_1_locked) {
        gpr_mu_unlock(&pi_1->mu);
        pi_1_locked = false;
      }

      GPR_SWAP(polling_island *, pi_1, pi_2);
      num_swaps++;
    }

    // p1 < p2
    // p1 MAY BE locked
    // p2 is NOT locked

    if (!pi_1_locked) {
      gpr_mu_lock(&pi_1->mu);
      pi_1_locked = true;

      if (pi_1->merged_to != NULL) {
        temp = pi_1->merged_to;
        polling_island_unref_and_unlock(pi_1, 1);
        pi_1 = temp;
        pi_1_locked = false;

        continue;
      }
    }

    // p1 is LOCKED
    // p2 is UNLOCKED
    // p1 != p2

    gpr_mu_lock(&pi_2->mu);
    pi_2_locked = true;

    if (pi_2->merged_to != NULL) {
      temp = pi_2->merged_to;
      polling_island_unref_and_unlock(pi_2, 1);
      pi_2 = temp;
      pi_2_locked = false;
    }
  }

  // Either pi_1 == pi_2 OR we got both locks!
  if (pi_1 == pi_2) {
    GPR_ASSERT(pi_1_locked || (!pi_1_locked && !pi_2_locked));
    if (!pi_1_locked) {
      pi_1 = pi_2 = polling_island_update_and_lock(pi_1, 2, 0);
    }
  } else {
    GPR_ASSERT(pi_1_locked && pi_2_locked);
    if (num_swaps % 2 > 0) {
      GPR_SWAP(polling_island *, pi_1, pi_2);
    }
  }

  *p = pi_1;
  *q = pi_2;
}

polling_island *polling_island_merge(polling_island *p, polling_island *q) {
  polling_island *merged = NULL;

  polling_island_pair_update_and_lock(&p, &q);

  /* TODO: sreek: Think about this scenario some more. Is it possible ?. what
   * does it mean, when would this happen  */
  if (p == q) {
    merged = p;
  }

  // Move all the fds from polling_island p to polling_island q
  polling_island_add_fds_locked(q, p->fds, p->fd_cnt);
  polling_island_clear_fds_locked(p);

  q->ref_cnt += p->ref_cnt;

  gpr_mu_unlock(&p->mu);
  gpr_mu_unlock(&q->mu);

  return merged;
}

static void polling_island_global_init() {
  gpr_mu_init(&g_pi_freelist_mu);
  g_pi_freelist = NULL;
}

/*******************************************************************************
 * pollset declarations
 */

typedef struct grpc_cached_wakeup_fd {
  grpc_wakeup_fd fd;
  struct grpc_cached_wakeup_fd *next;
} grpc_cached_wakeup_fd;

struct grpc_pollset_worker {
  grpc_cached_wakeup_fd *wakeup_fd;
  int reevaluate_polling_on_wakeup;
  int kicked_specifically;
  pthread_t pt_id;
  struct grpc_pollset_worker *next;
  struct grpc_pollset_worker *prev;
};

struct grpc_pollset {
  gpr_mu mu;
  grpc_pollset_worker root_worker;
  int shutting_down;
  int called_shutdown;
  int kicked_without_pollers;
  grpc_closure *shutdown_done;

  int epoll_fd;

  /* Mutex protecting the 'polling_island' field */
  gpr_mu pi_mu;

  /* The polling island to which this fd belongs to. An fd belongs to exactly
     one polling island */
  struct polling_island *polling_island;

  /* Local cache of eventfds for workers */
  grpc_cached_wakeup_fd *local_wakeup_cache;
};

/* Add an fd to a pollset */
static void pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           struct grpc_fd *fd);

static void pollset_set_add_fd(grpc_exec_ctx *exec_ctx,
                               grpc_pollset_set *pollset_set, grpc_fd *fd);

/* Convert a timespec to milliseconds:
   - very small or negative poll times are clamped to zero to do a
     non-blocking poll (which becomes spin polling)
   - other small values are rounded up to one millisecond
   - longer than a millisecond polls are rounded up to the next nearest
     millisecond to avoid spinning
   - infinite timeouts are converted to -1 */
static int poll_deadline_to_millis_timeout(gpr_timespec deadline,
                                           gpr_timespec now);

/* Allow kick to wakeup the currently polling worker */
#define GRPC_POLLSET_CAN_KICK_SELF 1
/* Force the wakee to repoll when awoken */
#define GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP 2
/* As per pollset_kick, with an extended set of flags (defined above)
   -- mostly for fd_posix's use. */
static void pollset_kick_ext(grpc_pollset *p,
                             grpc_pollset_worker *specific_worker,
                             uint32_t flags);

/* turn a pollset into a multipoller: platform specific */
typedef void (*platform_become_multipoller_type)(grpc_exec_ctx *exec_ctx,
                                                 grpc_pollset *pollset,
                                                 struct grpc_fd **fds,
                                                 size_t fd_count);

/* Return 1 if the pollset has active threads in pollset_work (pollset must
 * be locked) */
static int pollset_has_workers(grpc_pollset *pollset);

/*******************************************************************************
 * pollset_set definitions
 */

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
 * fd_posix.c
 */

/* We need to keep a freelist not because of any concerns of malloc
 * performance
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
/* TODO(klempner): We could use some form of polling generation count to know
 * when these are safe to free. */
/* TODO(klempner): Consider disabling freelisting if we don't have multiple
 * threads in poll on the same fd */
/* TODO(klempner): Batch these allocations to reduce fragmentation */
static grpc_fd *fd_freelist = NULL;
static gpr_mu fd_freelist_mu;

static void freelist_fd(grpc_fd *fd) {
  gpr_mu_lock(&fd_freelist_mu);
  fd->freelist_next = fd_freelist;
  fd_freelist = fd;
  grpc_iomgr_unregister_object(&fd->iomgr_object);
  gpr_mu_unlock(&fd_freelist_mu);
}

static grpc_fd *alloc_fd(int fd) {
  grpc_fd *r = NULL;

  gpr_mu_lock(&fd_freelist_mu);
  if (fd_freelist != NULL) {
    r = fd_freelist;
    fd_freelist = fd_freelist->freelist_next;
  }
  gpr_mu_unlock(&fd_freelist_mu);

  if (r == NULL) {
    r = gpr_malloc(sizeof(grpc_fd));
    gpr_mu_init(&r->mu);
    gpr_mu_init(&r->pi_mu);
  }

  /* TODO: sreek - check with ctiller on why we need to acquire a lock here */
  gpr_mu_lock(&r->mu);
  gpr_atm_rel_store(&r->refst, 1);
  r->shutdown = 0;
  r->read_closure = CLOSURE_NOT_READY;
  r->write_closure = CLOSURE_NOT_READY;
  r->fd = fd;
  r->polling_island = NULL;
  r->freelist_next = NULL;
  r->on_done_closure = NULL;
  r->closed = 0;
  r->released = 0;
  gpr_mu_unlock(&r->mu);
  return r;
}

static void destroy(grpc_fd *fd) {
  gpr_mu_destroy(&fd->mu);
  gpr_free(fd);
}

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
    freelist_fd(fd);
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
    destroy(fd);
  }
  gpr_mu_destroy(&fd_freelist_mu);
}

static grpc_fd *fd_create(int fd, const char *name) {
  grpc_fd *r = alloc_fd(fd);

  char *name2;
  gpr_asprintf(&name2, "%s fd=%d", name, fd);
  grpc_iomgr_register_object(&r->iomgr_object, name2);
  gpr_free(name2);
#ifdef GRPC_FD_REF_COUNT_DEBUG
  gpr_log(GPR_DEBUG, "FD %d %p create %s", fd, r, name);
#endif
  return r;
}

static bool fd_is_orphaned(grpc_fd *fd) {
  return (gpr_atm_acq_load(&fd->refst) & 1) == 0;
}

static void close_fd_locked(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  fd->closed = 1;
  if (!fd->released) {
    close(fd->fd);
  } else {
    /* TODO: sreek - Check for deadlocks */

    gpr_mu_lock(&fd->pi_mu);
    fd->polling_island =
        polling_island_update_and_lock(fd->polling_island, 1, 0);

    polling_island_remove_fd_locked(fd->polling_island, fd);
    polling_island_unref_and_unlock(fd->polling_island, 1);

    fd->polling_island = NULL;
    gpr_mu_unlock(&fd->pi_mu);
  }

  grpc_exec_ctx_enqueue(exec_ctx, fd->on_done_closure, true, NULL);
}

static int fd_wrapped_fd(grpc_fd *fd) {
  if (fd->released || fd->closed) {
    return -1;
  } else {
    return fd->fd;
  }
}

/* TODO: sreek - do something here with the pollset island link */
static void fd_orphan(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                      grpc_closure *on_done, int *release_fd,
                      const char *reason) {
  fd->on_done_closure = on_done;
  fd->released = release_fd != NULL;
  if (!fd->released) {
    shutdown(fd->fd, SHUT_RDWR);
  } else {
    *release_fd = fd->fd;
  }
  gpr_mu_lock(&fd->mu);
  REF_BY(fd, 1, reason); /* remove active status, but keep referenced */
  close_fd_locked(exec_ctx, fd);
  gpr_mu_unlock(&fd->mu);
  UNREF_BY(fd, 2, reason); /* drop the reference */
}

/* increment refcount by two to avoid changing the orphan bit */
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

static void notify_on_locked(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                             grpc_closure **st, grpc_closure *closure) {
  if (*st == CLOSURE_NOT_READY) {
    /* not ready ==> switch to a waiting state by setting the closure */
    *st = closure;
  } else if (*st == CLOSURE_READY) {
    /* already ready ==> queue the closure to run immediately */
    *st = CLOSURE_NOT_READY;
    grpc_exec_ctx_enqueue(exec_ctx, closure, !fd->shutdown, NULL);
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
    grpc_exec_ctx_enqueue(exec_ctx, *st, !fd->shutdown, NULL);
    *st = CLOSURE_NOT_READY;
    return 1;
  }
}

/* Do something here with the pollset island link (?) */
static void fd_shutdown(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  gpr_mu_lock(&fd->mu);
  GPR_ASSERT(!fd->shutdown);
  fd->shutdown = 1;
  set_ready_locked(exec_ctx, fd, &fd->read_closure);
  set_ready_locked(exec_ctx, fd, &fd->write_closure);
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
 * pollset_posix.c
 */

GPR_TLS_DECL(g_current_thread_poller);
GPR_TLS_DECL(g_current_thread_worker);

/** The alarm system needs to be able to wakeup 'some poller' sometimes
 *  (specifically when a new alarm needs to be triggered earlier than the next
 *  alarm 'epoch').
 *  This wakeup_fd gives us something to alert on when such a case occurs. */
grpc_wakeup_fd grpc_global_wakeup_fd;

static void remove_worker(grpc_pollset *p, grpc_pollset_worker *worker) {
  worker->prev->next = worker->next;
  worker->next->prev = worker->prev;
}

static int pollset_has_workers(grpc_pollset *p) {
  return p->root_worker.next != &p->root_worker;
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

static void pollset_kick_ext(grpc_pollset *p,
                             grpc_pollset_worker *specific_worker,
                             uint32_t flags) {
  GPR_TIMER_BEGIN("pollset_kick_ext", 0);

  /* pollset->mu already held */
  if (specific_worker != NULL) {
    if (specific_worker == GRPC_POLLSET_KICK_BROADCAST) {
      GPR_TIMER_BEGIN("pollset_kick_ext.broadcast", 0);
      GPR_ASSERT((flags & GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) == 0);
      for (specific_worker = p->root_worker.next;
           specific_worker != &p->root_worker;
           specific_worker = specific_worker->next) {
        grpc_wakeup_fd_wakeup(&specific_worker->wakeup_fd->fd);
      }
      p->kicked_without_pollers = 1;
      GPR_TIMER_END("pollset_kick_ext.broadcast", 0);
    } else if (gpr_tls_get(&g_current_thread_worker) !=
               (intptr_t)specific_worker) {
      GPR_TIMER_MARK("different_thread_worker", 0);
      if ((flags & GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) != 0) {
        specific_worker->reevaluate_polling_on_wakeup = 1;
      }
      specific_worker->kicked_specifically = 1;
      grpc_wakeup_fd_wakeup(&specific_worker->wakeup_fd->fd);
      /* TODO (sreek): Refactor this into a separate file*/
      pthread_kill(specific_worker->pt_id, SIGUSR1);
    } else if ((flags & GRPC_POLLSET_CAN_KICK_SELF) != 0) {
      GPR_TIMER_MARK("kick_yoself", 0);
      if ((flags & GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) != 0) {
        specific_worker->reevaluate_polling_on_wakeup = 1;
      }
      specific_worker->kicked_specifically = 1;
      grpc_wakeup_fd_wakeup(&specific_worker->wakeup_fd->fd);
    }
  } else if (gpr_tls_get(&g_current_thread_poller) != (intptr_t)p) {
    GPR_ASSERT((flags & GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) == 0);
    GPR_TIMER_MARK("kick_anonymous", 0);
    specific_worker = pop_front_worker(p);
    if (specific_worker != NULL) {
      if (gpr_tls_get(&g_current_thread_worker) == (intptr_t)specific_worker) {
        GPR_TIMER_MARK("kick_anonymous_not_self", 0);
        push_back_worker(p, specific_worker);
        specific_worker = pop_front_worker(p);
        if ((flags & GRPC_POLLSET_CAN_KICK_SELF) == 0 &&
            gpr_tls_get(&g_current_thread_worker) ==
                (intptr_t)specific_worker) {
          push_back_worker(p, specific_worker);
          specific_worker = NULL;
        }
      }
      if (specific_worker != NULL) {
        GPR_TIMER_MARK("finally_kick", 0);
        push_back_worker(p, specific_worker);
        grpc_wakeup_fd_wakeup(&specific_worker->wakeup_fd->fd);
      }
    } else {
      GPR_TIMER_MARK("kicked_no_pollers", 0);
      p->kicked_without_pollers = 1;
    }
  }

  GPR_TIMER_END("pollset_kick_ext", 0);
}

static void pollset_kick(grpc_pollset *p,
                         grpc_pollset_worker *specific_worker) {
  pollset_kick_ext(p, specific_worker, 0);
}

/* global state management */

static void sig_handler(int sig_num) {
  gpr_log(GPR_INFO, "Received signal %d", sig_num);
}

static void pollset_global_init(void) {
  gpr_tls_init(&g_current_thread_poller);
  gpr_tls_init(&g_current_thread_worker);
  grpc_wakeup_fd_init(&grpc_global_wakeup_fd);
  signal(SIGUSR1, sig_handler);
}

static void pollset_global_shutdown(void) {
  grpc_wakeup_fd_destroy(&grpc_global_wakeup_fd);
  gpr_tls_destroy(&g_current_thread_poller);
  gpr_tls_destroy(&g_current_thread_worker);
}

static void kick_poller(void) { grpc_wakeup_fd_wakeup(&grpc_global_wakeup_fd); }

/* TODO: sreek. Try to Remove this forward declaration*/
static void multipoll_with_epoll_pollset_create_efd(grpc_pollset *pollset);

/* main interface */

static void pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  gpr_mu_init(&pollset->mu);
  *mu = &pollset->mu;
  pollset->root_worker.next = pollset->root_worker.prev = &pollset->root_worker;
  gpr_mu_init(&pollset->pi_mu);
  pollset->polling_island = NULL;
  pollset->shutting_down = 0;
  pollset->called_shutdown = 0;
  pollset->kicked_without_pollers = 0;
  pollset->local_wakeup_cache = NULL;
  pollset->kicked_without_pollers = 0;

  multipoll_with_epoll_pollset_create_efd(pollset);
}

/* TODO(sreek): Maybe merge multipoll_*_destroy() with pollset_destroy()
 * function */
static void multipoll_with_epoll_pollset_destroy(grpc_pollset *pollset);

static void pollset_destroy(grpc_pollset *pollset) {
  GPR_ASSERT(!pollset_has_workers(pollset));

  multipoll_with_epoll_pollset_destroy(pollset);

  while (pollset->local_wakeup_cache) {
    grpc_cached_wakeup_fd *next = pollset->local_wakeup_cache->next;
    grpc_wakeup_fd_destroy(&pollset->local_wakeup_cache->fd);
    gpr_free(pollset->local_wakeup_cache);
    pollset->local_wakeup_cache = next;
  }
  gpr_mu_destroy(&pollset->pi_mu);
  gpr_mu_destroy(&pollset->mu);
}

/* TODO(sreek) - Do something with the pollset island link (??) */
static void pollset_reset(grpc_pollset *pollset) {
  GPR_ASSERT(pollset->shutting_down);
  GPR_ASSERT(!pollset_has_workers(pollset));
  pollset->shutting_down = 0;
  pollset->called_shutdown = 0;
  pollset->kicked_without_pollers = 0;
}

/* TODO (sreek): Remove multipoll_with_epoll_finish_shutdown() declaration */
static void multipoll_with_epoll_pollset_finish_shutdown(grpc_pollset *pollset);

static void finish_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset) {
  multipoll_with_epoll_pollset_finish_shutdown(pollset);
  grpc_exec_ctx_enqueue(exec_ctx, pollset->shutdown_done, true, NULL);
}

/* TODO(sreek): Remove multipoll_with_epoll_*_maybe_work_and_unlock
 * declaration
 */
static void multipoll_with_epoll_pollset_maybe_work_and_unlock(
    grpc_exec_ctx *exec_ctx, grpc_pollset *pollset, grpc_pollset_worker *worker,
    gpr_timespec deadline, gpr_timespec now);

static void pollset_work(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                         grpc_pollset_worker **worker_hdl, gpr_timespec now,
                         gpr_timespec deadline) {
  grpc_pollset_worker worker;
  *worker_hdl = &worker;

  /* pollset->mu already held */
  int added_worker = 0;
  int locked = 1;
  int queued_work = 0;
  int keep_polling = 0;
  GPR_TIMER_BEGIN("pollset_work", 0);
  /* this must happen before we (potentially) drop pollset->mu */
  worker.next = worker.prev = NULL;
  worker.reevaluate_polling_on_wakeup = 0;
  if (pollset->local_wakeup_cache != NULL) {
    worker.wakeup_fd = pollset->local_wakeup_cache;
    pollset->local_wakeup_cache = worker.wakeup_fd->next;
  } else {
    worker.wakeup_fd = gpr_malloc(sizeof(*worker.wakeup_fd));
    grpc_wakeup_fd_init(&worker.wakeup_fd->fd);
  }
  worker.kicked_specifically = 0;

  /* TODO(sreek): Abstract this thread id stuff out into a separate file */
  worker.pt_id = pthread_self();
  /* If we're shutting down then we don't execute any extended work */
  if (pollset->shutting_down) {
    GPR_TIMER_MARK("pollset_work.shutting_down", 0);
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
        push_front_worker(pollset, &worker);
        added_worker = 1;
        gpr_tls_set(&g_current_thread_worker, (intptr_t)&worker);
      }
      gpr_tls_set(&g_current_thread_poller, (intptr_t)pollset);
      GPR_TIMER_BEGIN("maybe_work_and_unlock", 0);

      multipoll_with_epoll_pollset_maybe_work_and_unlock(
          exec_ctx, pollset, &worker, deadline, now);

      GPR_TIMER_END("maybe_work_and_unlock", 0);
      locked = 0;
      gpr_tls_set(&g_current_thread_poller, 0);
    } else {
      GPR_TIMER_MARK("pollset_work.kicked_without_pollers", 0);
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
    /* If we're forced to re-evaluate polling (via pollset_kick with
       GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) then we land here and force
       a loop */
    if (worker.reevaluate_polling_on_wakeup) {
      worker.reevaluate_polling_on_wakeup = 0;
      pollset->kicked_without_pollers = 0;
      if (queued_work || worker.kicked_specifically) {
        /* If there's queued work on the list, then set the deadline to be
           immediate so we get back out of the polling loop quickly */
        deadline = gpr_inf_past(GPR_CLOCK_MONOTONIC);
      }
      keep_polling = 1;
    }
  }
  if (added_worker) {
    remove_worker(pollset, &worker);
    gpr_tls_set(&g_current_thread_worker, 0);
  }
  /* release wakeup fd to the local pool */
  worker.wakeup_fd->next = pollset->local_wakeup_cache;
  pollset->local_wakeup_cache = worker.wakeup_fd;
  /* check shutdown conditions */
  if (pollset->shutting_down) {
    if (pollset_has_workers(pollset)) {
      pollset_kick(pollset, NULL);
    } else if (!pollset->called_shutdown) {
      pollset->called_shutdown = 1;
      gpr_mu_unlock(&pollset->mu);
      finish_shutdown(exec_ctx, pollset);
      grpc_exec_ctx_flush(exec_ctx);
      /* Continuing to access pollset here is safe -- it is the caller's
       * responsibility to not destroy when it has outstanding calls to
       * pollset_work.
       * TODO(dklempner): Can we refactor the shutdown logic to avoid this? */
      gpr_mu_lock(&pollset->mu);
    }
  }
  *worker_hdl = NULL;
  GPR_TIMER_END("pollset_work", 0);
}

/* TODO: (sreek) Do something with the pollset island link */
static void pollset_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                             grpc_closure *closure) {
  GPR_ASSERT(!pollset->shutting_down);
  pollset->shutting_down = 1;
  pollset->shutdown_done = closure;
  pollset_kick(pollset, GRPC_POLLSET_KICK_BROADCAST);

  if (!pollset->called_shutdown && !pollset_has_workers(pollset)) {
    pollset->called_shutdown = 1;
    finish_shutdown(exec_ctx, pollset);
  }
}

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

/*******************************************************************************
 * pollset_multipoller_with_epoll_posix.c
 */

static void set_ready(grpc_exec_ctx *exec_ctx, grpc_fd *fd, grpc_closure **st) {
  /* only one set_ready can be active at once (but there may be a racing
     notify_on) */
  gpr_mu_lock(&fd->mu);
  set_ready_locked(exec_ctx, fd, st);
  gpr_mu_unlock(&fd->mu);
}

static void fd_become_readable(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  set_ready(exec_ctx, fd, &fd->read_closure);
}

static void fd_become_writable(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  set_ready(exec_ctx, fd, &fd->write_closure);
}

/* TODO: sreek - This function multipoll_with_epoll_pollset_add_fd() and
 * finally_add_fd() in ev_poll_and_epoll_posix.c */
static void pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_fd *fd) {
  /* TODO sreek - Check if we need to get a pollset->mu lock here */
  gpr_mu_lock(&pollset->pi_mu);
  gpr_mu_lock(&fd->pi_mu);

  polling_island *pi_new = NULL;

  if (fd->polling_island == pollset->polling_island) {
    pi_new = fd->polling_island;
    if (pi_new == NULL) {
      pi_new = polling_island_create(fd, 2);
    }
  } else if (fd->polling_island == NULL) {
    pi_new = polling_island_update_and_lock(pollset->polling_island, 1, 1);

  } else if (pollset->polling_island == NULL) {
    pi_new = polling_island_update_and_lock(fd->polling_island, 1, 1);
  } else {  // Non null and different
    pi_new = polling_island_merge(fd->polling_island, pollset->polling_island);
  }

  fd->polling_island = pollset->polling_island = pi_new;

  gpr_mu_unlock(&fd->pi_mu);
  gpr_mu_unlock(&pollset->pi_mu);
}

/* Creates an epoll fd and initializes the pollset */
/* TODO: This has to be called ONLY from pollset_init function. and hence it
 * does not acquire any lock */
static void multipoll_with_epoll_pollset_create_efd(grpc_pollset *pollset) {
  struct epoll_event ev;
  int err;

  pollset->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (pollset->epoll_fd < 0) {
    gpr_log(GPR_ERROR, "epoll_create1 failed: %s", strerror(errno));
    abort();
  }

  ev.events = (uint32_t)(EPOLLIN | EPOLLET);
  ev.data.ptr = NULL;

  err = epoll_ctl(pollset->epoll_fd, EPOLL_CTL_ADD,
                  GRPC_WAKEUP_FD_GET_READ_FD(&grpc_global_wakeup_fd), &ev);
  if (err < 0) {
    gpr_log(GPR_ERROR, "epoll_ctl add for %d failed: %s",
            GRPC_WAKEUP_FD_GET_READ_FD(&grpc_global_wakeup_fd),
            strerror(errno));
  }
}

/* TODO(klempner): We probably want to turn this down a bit */
#define GRPC_EPOLL_MAX_EVENTS 1000

static void multipoll_with_epoll_pollset_maybe_work_and_unlock(
    grpc_exec_ctx *exec_ctx, grpc_pollset *pollset, grpc_pollset_worker *worker,
    gpr_timespec deadline, gpr_timespec now) {
  struct epoll_event ep_ev[GRPC_EPOLL_MAX_EVENTS];
  int epoll_fd = pollset->epoll_fd;
  int ep_rv;
  int poll_rv;
  int timeout_ms;
  struct pollfd pfds[2];

  /* If you want to ignore epoll's ability to sanely handle parallel pollers,
   * for a more apples-to-apples performance comparison with poll, add a
   * if (pollset->counter != 0) { return 0; }
   * here.
   */

  gpr_mu_unlock(&pollset->mu);

  timeout_ms = poll_deadline_to_millis_timeout(deadline, now);

  pfds[0].fd = GRPC_WAKEUP_FD_GET_READ_FD(&worker->wakeup_fd->fd);
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;
  pfds[1].fd = epoll_fd;
  pfds[1].events = POLLIN;
  pfds[1].revents = 0;

  /* TODO(vpai): Consider first doing a 0 timeout poll here to avoid
     even going into the blocking annotation if possible */
  GPR_TIMER_BEGIN("poll", 0);
  GRPC_SCHEDULING_START_BLOCKING_REGION;
  poll_rv = grpc_poll_function(pfds, 2, timeout_ms);
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  GPR_TIMER_END("poll", 0);

  if (poll_rv < 0) {
    if (errno != EINTR) {
      gpr_log(GPR_ERROR, "poll() failed: %s", strerror(errno));
    }
  } else if (poll_rv == 0) {
    /* do nothing */
  } else {
    if (pfds[0].revents) {
      grpc_wakeup_fd_consume_wakeup(&worker->wakeup_fd->fd);
    }
    if (pfds[1].revents) {
      do {
        /* The following epoll_wait never blocks; it has a timeout of 0 */
        ep_rv = epoll_wait(epoll_fd, ep_ev, GRPC_EPOLL_MAX_EVENTS, 0);
        if (ep_rv < 0) {
          if (errno != EINTR) {
            gpr_log(GPR_ERROR, "epoll_wait() failed: %s", strerror(errno));
          }
        } else {
          int i;
          for (i = 0; i < ep_rv; ++i) {
            grpc_fd *fd = ep_ev[i].data.ptr;
            /* TODO(klempner): We might want to consider making err and pri
             * separate events */
            int cancel = ep_ev[i].events & (EPOLLERR | EPOLLHUP);
            int read_ev = ep_ev[i].events & (EPOLLIN | EPOLLPRI);
            int write_ev = ep_ev[i].events & EPOLLOUT;
            if (fd == NULL) {
              grpc_wakeup_fd_consume_wakeup(&grpc_global_wakeup_fd);
            } else {
              if (read_ev || cancel) {
                fd_become_readable(exec_ctx, fd);
              }
              if (write_ev || cancel) {
                fd_become_writable(exec_ctx, fd);
              }
            }
          }
        }
      } while (ep_rv == GRPC_EPOLL_MAX_EVENTS);
    }
  }
}

static void multipoll_with_epoll_pollset_finish_shutdown(
    grpc_pollset *pollset) {}

static void multipoll_with_epoll_pollset_destroy(grpc_pollset *pollset) {
  close(pollset->epoll_fd);
}

/*******************************************************************************
 * pollset_set_posix.c
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

/*******************************************************************************
 * event engine binding
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
    .fd_notify_on_read = fd_notify_on_read,
    .fd_notify_on_write = fd_notify_on_write,

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

const grpc_event_engine_vtable *grpc_init_epoll_linux(void) {
  fd_global_init();
  pollset_global_init();
  polling_island_global_init();
  return &vtable;
}

#endif
