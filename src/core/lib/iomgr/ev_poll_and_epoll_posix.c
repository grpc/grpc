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

/* This file will be removed shortly: it's here to keep refactoring
 * steps simple and auditable.
 * It's the combination of the old files:
 *  - fd_posix.{h,c}
 *  - pollset_posix.{h,c}
 *  - pullset_multipoller_with_{poll,epoll}.{h,c}
 * The new version will be split into:
 *  - ev_poll_posix.{h,c}
 *  - ev_epoll_posix.{h,c}
 */

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SOCKET

#include "src/core/lib/iomgr/ev_poll_and_epoll_posix.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/tls.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/block_annotate.h"

/*******************************************************************************
 * FD declarations
 */

typedef struct grpc_fd_watcher {
  struct grpc_fd_watcher *next;
  struct grpc_fd_watcher *prev;
  grpc_pollset *pollset;
  grpc_pollset_worker *worker;
  grpc_fd *fd;
} grpc_fd_watcher;

struct grpc_fd {
  int fd;
  /* refst format:
     bit0:   1=active/0=orphaned
     bit1-n: refcount
     meaning that mostly we ref by two to avoid altering the orphaned bit,
     and just unref by 1 when we're ready to flag the object as orphaned */
  gpr_atm refst;

  gpr_mu mu;
  bool shutdown;
  bool closed;
  bool released;

  /* The watcher list.

     The following watcher related fields are protected by watcher_mu.

     An fd_watcher is an ephemeral object created when an fd wants to
     begin polling, and destroyed after the poll.

     It denotes the fd's interest in whether to read poll or write poll
     or both or neither on this fd.

     If a watcher is asked to poll for reads or writes, the read_watcher
     or write_watcher fields are set respectively. A watcher may be asked
     to poll for both, in which case both fields will be set.

     read_watcher and write_watcher may be NULL if no watcher has been
     asked to poll for reads or writes.

     If an fd_watcher is not asked to poll for reads or writes, it's added
     to a linked list of inactive watchers, rooted at inactive_watcher_root.
     If at a later time there becomes need of a poller to poll, one of
     the inactive pollers may be kicked out of their poll loops to take
     that responsibility. */
  grpc_fd_watcher inactive_watcher_root;
  grpc_fd_watcher *read_watcher;
  grpc_fd_watcher *write_watcher;

  grpc_closure *read_closure;
  grpc_closure *write_closure;

  struct grpc_fd *freelist_next;

  grpc_closure *on_done_closure;

  grpc_iomgr_object iomgr_object;
};

/* Begin polling on an fd.
   Registers that the given pollset is interested in this fd - so that if read
   or writability interest changes, the pollset can be kicked to pick up that
   new interest.
   Return value is:
     (fd_needs_read? read_mask : 0) | (fd_needs_write? write_mask : 0)
   i.e. a combination of read_mask and write_mask determined by the fd's current
   interest in said events.
   Polling strategies that do not need to alter their behavior depending on the
   fd's current interest (such as epoll) do not need to call this function.
   MUST NOT be called with a pollset lock taken */
static uint32_t fd_begin_poll(grpc_fd *fd, grpc_pollset *pollset,
                              grpc_pollset_worker *worker, uint32_t read_mask,
                              uint32_t write_mask, grpc_fd_watcher *rec);
/* Complete polling previously started with fd_begin_poll
   MUST NOT be called with a pollset lock taken
   if got_read or got_write are 1, also does the become_{readable,writable} as
   appropriate. */
static void fd_end_poll(grpc_exec_ctx *exec_ctx, grpc_fd_watcher *rec,
                        int got_read, int got_write);

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
 * pollset declarations
 */

typedef struct grpc_pollset_vtable grpc_pollset_vtable;

typedef struct grpc_cached_wakeup_fd {
  grpc_wakeup_fd fd;
  struct grpc_cached_wakeup_fd *next;
} grpc_cached_wakeup_fd;

struct grpc_pollset_worker {
  grpc_cached_wakeup_fd *wakeup_fd;
  bool reevaluate_polling_on_wakeup;
  bool kicked_specifically;
  struct grpc_pollset_worker *next;
  struct grpc_pollset_worker *prev;
};

struct grpc_pollset {
  /* pollsets under posix can mutate representation as fds are added and
     removed.
     For example, we may choose a poll() based implementation on linux for
     few fds, and an epoll() based implementation for many fds */
  const grpc_pollset_vtable *vtable;
  gpr_mu mu;
  grpc_pollset_worker root_worker;
  int in_flight_cbs;
  bool shutting_down;
  bool called_shutdown;
  bool kicked_without_pollers;
  grpc_closure *shutdown_done;
  grpc_closure_list idle_jobs;
  union {
    int fd;
    void *ptr;
  } data;
  /* Local cache of eventfds for workers */
  grpc_cached_wakeup_fd *local_wakeup_cache;
};

struct grpc_pollset_vtable {
  void (*add_fd)(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                 struct grpc_fd *fd, int and_unlock_pollset);
  grpc_error *(*maybe_work_and_unlock)(grpc_exec_ctx *exec_ctx,
                                       grpc_pollset *pollset,
                                       grpc_pollset_worker *worker,
                                       gpr_timespec deadline, gpr_timespec now);
  void (*finish_shutdown)(grpc_pollset *pollset);
  void (*destroy)(grpc_pollset *pollset);
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
static grpc_error *pollset_kick_ext(grpc_pollset *p,
                                    grpc_pollset_worker *specific_worker,
                                    uint32_t flags) GRPC_MUST_USE_RESULT;

/* turn a pollset into a multipoller: platform specific */
typedef void (*platform_become_multipoller_type)(grpc_exec_ctx *exec_ctx,
                                                 grpc_pollset *pollset,
                                                 struct grpc_fd **fds,
                                                 size_t fd_count);
static platform_become_multipoller_type platform_become_multipoller;

/* Return 1 if the pollset has active threads in pollset_work (pollset must
 * be locked) */
static int pollset_has_workers(grpc_pollset *pollset);

static void remove_fd_from_all_epoll_sets(int fd);

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
  }

  gpr_mu_lock(&r->mu);
  gpr_atm_rel_store(&r->refst, 1);
  r->shutdown = false;
  r->read_closure = CLOSURE_NOT_READY;
  r->write_closure = CLOSURE_NOT_READY;
  r->fd = fd;
  r->inactive_watcher_root.next = r->inactive_watcher_root.prev =
      &r->inactive_watcher_root;
  r->freelist_next = NULL;
  r->read_watcher = r->write_watcher = NULL;
  r->on_done_closure = NULL;
  r->closed = false;
  r->released = false;
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

static grpc_error *pollset_kick_locked(grpc_fd_watcher *watcher) {
  gpr_mu_lock(&watcher->pollset->mu);
  GPR_ASSERT(watcher->worker);
  grpc_error *err = pollset_kick_ext(watcher->pollset, watcher->worker,
                                     GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP);
  gpr_mu_unlock(&watcher->pollset->mu);
  return err;
}

static void maybe_wake_one_watcher_locked(grpc_fd *fd) {
  if (fd->inactive_watcher_root.next != &fd->inactive_watcher_root) {
    pollset_kick_locked(fd->inactive_watcher_root.next);
  } else if (fd->read_watcher) {
    pollset_kick_locked(fd->read_watcher);
  } else if (fd->write_watcher) {
    pollset_kick_locked(fd->write_watcher);
  }
}

static void wake_all_watchers_locked(grpc_fd *fd) {
  grpc_fd_watcher *watcher;
  for (watcher = fd->inactive_watcher_root.next;
       watcher != &fd->inactive_watcher_root; watcher = watcher->next) {
    pollset_kick_locked(watcher);
  }
  if (fd->read_watcher) {
    pollset_kick_locked(fd->read_watcher);
  }
  if (fd->write_watcher && fd->write_watcher != fd->read_watcher) {
    pollset_kick_locked(fd->write_watcher);
  }
}

static int has_watchers(grpc_fd *fd) {
  return fd->read_watcher != NULL || fd->write_watcher != NULL ||
         fd->inactive_watcher_root.next != &fd->inactive_watcher_root;
}

static void close_fd_locked(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  fd->closed = true;
  if (!fd->released) {
    close(fd->fd);
  } else {
    remove_fd_from_all_epoll_sets(fd->fd);
  }
  grpc_exec_ctx_push(exec_ctx, fd->on_done_closure, GRPC_ERROR_NONE, NULL);
}

static int fd_wrapped_fd(grpc_fd *fd) {
  if (fd->released || fd->closed) {
    return -1;
  } else {
    return fd->fd;
  }
}

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
  if (!has_watchers(fd)) {
    close_fd_locked(exec_ctx, fd);
  } else {
    wake_all_watchers_locked(fd);
  }
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

static grpc_error *fd_shutdown_error(bool shutdown) {
  if (!shutdown) {
    return GRPC_ERROR_NONE;
  } else {
    return GRPC_ERROR_CREATE("FD shutdown");
  }
}

static void notify_on_locked(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                             grpc_closure **st, grpc_closure *closure) {
  if (*st == CLOSURE_NOT_READY) {
    /* not ready ==> switch to a waiting state by setting the closure */
    *st = closure;
  } else if (*st == CLOSURE_READY) {
    /* already ready ==> queue the closure to run immediately */
    *st = CLOSURE_NOT_READY;
    grpc_exec_ctx_push(exec_ctx, closure, fd_shutdown_error(fd->shutdown),
                       NULL);
    maybe_wake_one_watcher_locked(fd);
  } else {
    /* upcallptr was set to a different closure.  This is an error! */
    gpr_log(GPR_ERROR,
            "User called a notify_on function with a previous callback still "
            "pending");
    abort();
  }
}

/* returns true if state becomes not ready */
static bool set_ready_locked(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                             grpc_closure **st) {
  if (*st == CLOSURE_READY) {
    /* duplicate ready ==> ignore */
    return false;
  } else if (*st == CLOSURE_NOT_READY) {
    /* not ready, and not waiting ==> flag ready */
    *st = CLOSURE_READY;
    return false;
  } else {
    /* waiting ==> queue closure */
    grpc_exec_ctx_push(exec_ctx, *st, fd_shutdown_error(fd->shutdown), NULL);
    *st = CLOSURE_NOT_READY;
    return true;
  }
}

static void fd_shutdown(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  gpr_mu_lock(&fd->mu);
  GPR_ASSERT(!fd->shutdown);
  fd->shutdown = true;
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

static uint32_t fd_begin_poll(grpc_fd *fd, grpc_pollset *pollset,
                              grpc_pollset_worker *worker, uint32_t read_mask,
                              uint32_t write_mask, grpc_fd_watcher *watcher) {
  uint32_t mask = 0;
  grpc_closure *cur;
  int requested;
  /* keep track of pollers that have requested our events, in case they change
   */
  GRPC_FD_REF(fd, "poll");

  gpr_mu_lock(&fd->mu);

  /* if we are shutdown, then don't add to the watcher set */
  if (fd->shutdown) {
    watcher->fd = NULL;
    watcher->pollset = NULL;
    watcher->worker = NULL;
    gpr_mu_unlock(&fd->mu);
    GRPC_FD_UNREF(fd, "poll");
    return 0;
  }

  /* if there is nobody polling for read, but we need to, then start doing so */
  cur = fd->read_closure;
  requested = cur != CLOSURE_READY;
  if (read_mask && fd->read_watcher == NULL && requested) {
    fd->read_watcher = watcher;
    mask |= read_mask;
  }
  /* if there is nobody polling for write, but we need to, then start doing so
   */
  cur = fd->write_closure;
  requested = cur != CLOSURE_READY;
  if (write_mask && fd->write_watcher == NULL && requested) {
    fd->write_watcher = watcher;
    mask |= write_mask;
  }
  /* if not polling, remember this watcher in case we need someone to later */
  if (mask == 0 && worker != NULL) {
    watcher->next = &fd->inactive_watcher_root;
    watcher->prev = watcher->next->prev;
    watcher->next->prev = watcher->prev->next = watcher;
  }
  watcher->pollset = pollset;
  watcher->worker = worker;
  watcher->fd = fd;
  gpr_mu_unlock(&fd->mu);

  return mask;
}

static void fd_end_poll(grpc_exec_ctx *exec_ctx, grpc_fd_watcher *watcher,
                        int got_read, int got_write) {
  bool was_polling = false;
  bool kick = false;
  grpc_fd *fd = watcher->fd;

  if (fd == NULL) {
    return;
  }

  gpr_mu_lock(&fd->mu);

  if (watcher == fd->read_watcher) {
    /* remove read watcher, kick if we still need a read */
    was_polling = true;
    if (!got_read) {
      kick = true;
    }
    fd->read_watcher = NULL;
  }
  if (watcher == fd->write_watcher) {
    /* remove write watcher, kick if we still need a write */
    was_polling = true;
    if (!got_write) {
      kick = true;
    }
    fd->write_watcher = NULL;
  }
  if (!was_polling && watcher->worker != NULL) {
    /* remove from inactive list */
    watcher->next->prev = watcher->prev;
    watcher->prev->next = watcher->next;
  }
  if (got_read) {
    if (set_ready_locked(exec_ctx, fd, &fd->read_closure)) {
      kick = true;
    }
  }
  if (got_write) {
    if (set_ready_locked(exec_ctx, fd, &fd->write_closure)) {
      kick = true;
    }
  }
  if (kick) {
    maybe_wake_one_watcher_locked(fd);
  }
  if (fd_is_orphaned(fd) && !has_watchers(fd) && !fd->closed) {
    close_fd_locked(exec_ctx, fd);
  }
  gpr_mu_unlock(&fd->mu);

  GRPC_FD_UNREF(fd, "poll");
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

static void kick_append_error(grpc_error **composite, grpc_error *error) {
  if (error == GRPC_ERROR_NONE) return;
  if (*composite == GRPC_ERROR_NONE) {
    *composite = GRPC_ERROR_CREATE("Kick Failure");
  }
  *composite = grpc_error_add_child(*composite, error);
}

static grpc_error *pollset_kick_ext(grpc_pollset *p,
                                    grpc_pollset_worker *specific_worker,
                                    uint32_t flags) {
  GPR_TIMER_BEGIN("pollset_kick_ext", 0);
  grpc_error *error = GRPC_ERROR_NONE;

  /* pollset->mu already held */
  if (specific_worker != NULL) {
    if (specific_worker == GRPC_POLLSET_KICK_BROADCAST) {
      GPR_TIMER_BEGIN("pollset_kick_ext.broadcast", 0);
      GPR_ASSERT((flags & GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) == 0);
      for (specific_worker = p->root_worker.next;
           specific_worker != &p->root_worker;
           specific_worker = specific_worker->next) {
        kick_append_error(
            &error, grpc_wakeup_fd_wakeup(&specific_worker->wakeup_fd->fd));
      }
      p->kicked_without_pollers = true;
      GPR_TIMER_END("pollset_kick_ext.broadcast", 0);
    } else if (gpr_tls_get(&g_current_thread_worker) !=
               (intptr_t)specific_worker) {
      GPR_TIMER_MARK("different_thread_worker", 0);
      if ((flags & GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) != 0) {
        specific_worker->reevaluate_polling_on_wakeup = true;
      }
      specific_worker->kicked_specifically = true;
      kick_append_error(&error,
                        grpc_wakeup_fd_wakeup(&specific_worker->wakeup_fd->fd));
    } else if ((flags & GRPC_POLLSET_CAN_KICK_SELF) != 0) {
      GPR_TIMER_MARK("kick_yoself", 0);
      if ((flags & GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) != 0) {
        specific_worker->reevaluate_polling_on_wakeup = true;
      }
      specific_worker->kicked_specifically = true;
      kick_append_error(&error,
                        grpc_wakeup_fd_wakeup(&specific_worker->wakeup_fd->fd));
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
        kick_append_error(
            &error, grpc_wakeup_fd_wakeup(&specific_worker->wakeup_fd->fd));
      }
    } else {
      GPR_TIMER_MARK("kicked_no_pollers", 0);
      p->kicked_without_pollers = true;
    }
  }

  GPR_TIMER_END("pollset_kick_ext", 0);
  return error;
}

static grpc_error *pollset_kick(grpc_pollset *p,
                                grpc_pollset_worker *specific_worker) {
  return pollset_kick_ext(p, specific_worker, 0);
}

/* global state management */

static grpc_error *pollset_global_init(void) {
  gpr_tls_init(&g_current_thread_poller);
  gpr_tls_init(&g_current_thread_worker);
  return grpc_wakeup_fd_init(&grpc_global_wakeup_fd);
}

static void pollset_global_shutdown(void) {
  grpc_wakeup_fd_destroy(&grpc_global_wakeup_fd);
  gpr_tls_destroy(&g_current_thread_poller);
  gpr_tls_destroy(&g_current_thread_worker);
}

static grpc_error *kick_poller(void) {
  return grpc_wakeup_fd_wakeup(&grpc_global_wakeup_fd);
}

/* main interface */

static void become_basic_pollset(grpc_pollset *pollset, grpc_fd *fd_or_null);

static void pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  gpr_mu_init(&pollset->mu);
  *mu = &pollset->mu;
  pollset->root_worker.next = pollset->root_worker.prev = &pollset->root_worker;
  pollset->in_flight_cbs = 0;
  pollset->shutting_down = false;
  pollset->called_shutdown = false;
  pollset->kicked_without_pollers = false;
  pollset->idle_jobs.head = pollset->idle_jobs.tail = NULL;
  pollset->local_wakeup_cache = NULL;
  become_basic_pollset(pollset, NULL);
}

static void pollset_destroy(grpc_pollset *pollset) {
  GPR_ASSERT(pollset->in_flight_cbs == 0);
  GPR_ASSERT(!pollset_has_workers(pollset));
  GPR_ASSERT(pollset->idle_jobs.head == pollset->idle_jobs.tail);
  pollset->vtable->destroy(pollset);
  while (pollset->local_wakeup_cache) {
    grpc_cached_wakeup_fd *next = pollset->local_wakeup_cache->next;
    grpc_wakeup_fd_destroy(&pollset->local_wakeup_cache->fd);
    gpr_free(pollset->local_wakeup_cache);
    pollset->local_wakeup_cache = next;
  }
  gpr_mu_destroy(&pollset->mu);
}

static void pollset_reset(grpc_pollset *pollset) {
  GPR_ASSERT(pollset->shutting_down);
  GPR_ASSERT(pollset->in_flight_cbs == 0);
  GPR_ASSERT(!pollset_has_workers(pollset));
  GPR_ASSERT(pollset->idle_jobs.head == pollset->idle_jobs.tail);
  pollset->vtable->destroy(pollset);
  pollset->shutting_down = false;
  pollset->called_shutdown = false;
  pollset->kicked_without_pollers = false;
  become_basic_pollset(pollset, NULL);
}

static void pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_fd *fd) {
  gpr_mu_lock(&pollset->mu);
  pollset->vtable->add_fd(exec_ctx, pollset, fd, 1);
/* the following (enabled only in debug) will reacquire and then release
   our lock - meaning that if the unlocking flag passed to add_fd above is
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
  grpc_exec_ctx_push(exec_ctx, pollset->shutdown_done, GRPC_ERROR_NONE, NULL);
}

static grpc_error *pollset_work(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                                grpc_pollset_worker **worker_hdl,
                                gpr_timespec now, gpr_timespec deadline) {
  grpc_pollset_worker worker;
  *worker_hdl = &worker;
  grpc_error *error = GRPC_ERROR_NONE;

  /* pollset->mu already held */
  int added_worker = 0;
  int locked = 1;
  int queued_work = 0;
  int keep_polling = 0;
  GPR_TIMER_BEGIN("pollset_work", 0);
  /* this must happen before we (potentially) drop pollset->mu */
  worker.next = worker.prev = NULL;
  worker.reevaluate_polling_on_wakeup = false;
  if (pollset->local_wakeup_cache != NULL) {
    worker.wakeup_fd = pollset->local_wakeup_cache;
    pollset->local_wakeup_cache = worker.wakeup_fd->next;
  } else {
    worker.wakeup_fd = gpr_malloc(sizeof(*worker.wakeup_fd));
    error = grpc_wakeup_fd_init(&worker.wakeup_fd->fd);
    if (error != GRPC_ERROR_NONE) {
      return error;
    }
  }
  worker.kicked_specifically = false;
  /* If there's work waiting for the pollset to be idle, and the
     pollset is idle, then do that work */
  if (!pollset_has_workers(pollset) &&
      !grpc_closure_list_empty(pollset->idle_jobs)) {
    GPR_TIMER_MARK("pollset_work.idle_jobs", 0);
    grpc_exec_ctx_enqueue_list(exec_ctx, &pollset->idle_jobs, NULL);
    goto done;
  }
  /* If we're shutting down then we don't execute any extended work */
  if (pollset->shutting_down) {
    GPR_TIMER_MARK("pollset_work.shutting_down", 0);
    goto done;
  }
  /* Give do_promote priority so we don't starve it out */
  if (pollset->in_flight_cbs) {
    GPR_TIMER_MARK("pollset_work.in_flight_cbs", 0);
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
        push_front_worker(pollset, &worker);
        added_worker = 1;
        gpr_tls_set(&g_current_thread_worker, (intptr_t)&worker);
      }
      gpr_tls_set(&g_current_thread_poller, (intptr_t)pollset);
      GPR_TIMER_BEGIN("maybe_work_and_unlock", 0);
      error = pollset->vtable->maybe_work_and_unlock(exec_ctx, pollset, &worker,
                                                     deadline, now);
      GPR_TIMER_END("maybe_work_and_unlock", 0);
      locked = 0;
      gpr_tls_set(&g_current_thread_poller, 0);
    } else {
      GPR_TIMER_MARK("pollset_work.kicked_without_pollers", 0);
      pollset->kicked_without_pollers = false;
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
    if (worker.reevaluate_polling_on_wakeup && error == GRPC_ERROR_NONE) {
      worker.reevaluate_polling_on_wakeup = false;
      pollset->kicked_without_pollers = false;
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
    } else if (!pollset->called_shutdown && pollset->in_flight_cbs == 0) {
      pollset->called_shutdown = true;
      gpr_mu_unlock(&pollset->mu);
      finish_shutdown(exec_ctx, pollset);
      grpc_exec_ctx_flush(exec_ctx);
      /* Continuing to access pollset here is safe -- it is the caller's
       * responsibility to not destroy when it has outstanding calls to
       * pollset_work.
       * TODO(dklempner): Can we refactor the shutdown logic to avoid this? */
      gpr_mu_lock(&pollset->mu);
    } else if (!grpc_closure_list_empty(pollset->idle_jobs)) {
      grpc_exec_ctx_enqueue_list(exec_ctx, &pollset->idle_jobs, NULL);
      gpr_mu_unlock(&pollset->mu);
      grpc_exec_ctx_flush(exec_ctx);
      gpr_mu_lock(&pollset->mu);
    }
  }
  *worker_hdl = NULL;
  GPR_TIMER_END("pollset_work", 0);
  return error;
}

static void pollset_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                             grpc_closure *closure) {
  GPR_ASSERT(!pollset->shutting_down);
  pollset->shutting_down = true;
  pollset->shutdown_done = closure;
  pollset_kick(pollset, GRPC_POLLSET_KICK_BROADCAST);
  if (!pollset_has_workers(pollset)) {
    grpc_exec_ctx_enqueue_list(exec_ctx, &pollset->idle_jobs, NULL);
  }
  if (!pollset->called_shutdown && pollset->in_flight_cbs == 0 &&
      !pollset_has_workers(pollset)) {
    pollset->called_shutdown = true;
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

static void basic_do_promote(grpc_exec_ctx *exec_ctx, void *args,
                             grpc_error *error) {
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
  GPR_ASSERT(!pollset_has_workers(pollset));

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
      pollset->called_shutdown = true;
      finish_shutdown(exec_ctx, pollset);
    }
  } else if (fd_is_orphaned(fd)) {
    /* Don't try to add it to anything, we'll drop our ref on it below */
  } else if (pollset->vtable != original_vtable) {
    pollset->vtable->add_fd(exec_ctx, pollset, fd, 0);
  } else if (fd != pollset->data.ptr) {
    grpc_fd *fds[2];
    fds[0] = pollset->data.ptr;
    fds[1] = fd;

    if (fds[0] && !fd_is_orphaned(fds[0])) {
      platform_become_multipoller(exec_ctx, pollset, fds, GPR_ARRAY_SIZE(fds));
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

  if (!pollset_has_workers(pollset)) {
    /* Fast path -- no in flight cbs */
    /* TODO(klempner): Comment this out and fix any test failures or establish
     * they are due to timing issues */
    grpc_fd *fds[2];
    fds[0] = pollset->data.ptr;
    fds[1] = fd;

    if (fds[0] == NULL) {
      pollset->data.ptr = fd;
      GRPC_FD_REF(fd, "basicpoll");
    } else if (!fd_is_orphaned(fds[0])) {
      platform_become_multipoller(exec_ctx, pollset, fds, GPR_ARRAY_SIZE(fds));
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

  grpc_closure_list_append(&pollset->idle_jobs, &up_args->promotion_closure,
                           GRPC_ERROR_NONE);
  pollset_kick(pollset, GRPC_POLLSET_KICK_BROADCAST);

exit:
  if (and_unlock_pollset) {
    gpr_mu_unlock(&pollset->mu);
  }
}

static void work_combine_error(grpc_error **composite, grpc_error *error) {
  if (error == GRPC_ERROR_NONE) return;
  if (*composite == GRPC_ERROR_NONE) {
    *composite = GRPC_ERROR_CREATE("pollset_work");
  }
  *composite = grpc_error_add_child(*composite, error);
}

static grpc_error *basic_pollset_maybe_work_and_unlock(
    grpc_exec_ctx *exec_ctx, grpc_pollset *pollset, grpc_pollset_worker *worker,
    gpr_timespec deadline, gpr_timespec now) {
  grpc_error *error = GRPC_ERROR_NONE;

#define POLLOUT_CHECK (POLLOUT | POLLHUP | POLLERR)
#define POLLIN_CHECK (POLLIN | POLLHUP | POLLERR)

  struct pollfd pfd[3];
  grpc_fd *fd;
  grpc_fd_watcher fd_watcher;
  int timeout;
  int r;
  nfds_t nfds;

  fd = pollset->data.ptr;
  if (fd && fd_is_orphaned(fd)) {
    GRPC_FD_UNREF(fd, "basicpoll");
    fd = pollset->data.ptr = NULL;
  }
  timeout = poll_deadline_to_millis_timeout(deadline, now);
  pfd[0].fd = GRPC_WAKEUP_FD_GET_READ_FD(&grpc_global_wakeup_fd);
  pfd[0].events = POLLIN;
  pfd[0].revents = 0;
  pfd[1].fd = GRPC_WAKEUP_FD_GET_READ_FD(&worker->wakeup_fd->fd);
  pfd[1].events = POLLIN;
  pfd[1].revents = 0;
  nfds = 2;
  if (fd) {
    pfd[2].fd = fd->fd;
    pfd[2].revents = 0;
    GRPC_FD_REF(fd, "basicpoll_begin");
    gpr_mu_unlock(&pollset->mu);
    pfd[2].events =
        (short)fd_begin_poll(fd, pollset, worker, POLLIN, POLLOUT, &fd_watcher);
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
    if (errno != EINTR) {
      work_combine_error(&error, GRPC_OS_ERROR(errno, "poll"));
    }
    if (fd) {
      fd_end_poll(exec_ctx, &fd_watcher, 0, 0);
    }
  } else if (r == 0) {
    if (fd) {
      fd_end_poll(exec_ctx, &fd_watcher, 0, 0);
    }
  } else {
    if (pfd[0].revents & POLLIN_CHECK) {
      work_combine_error(&error,
                         grpc_wakeup_fd_consume_wakeup(&grpc_global_wakeup_fd));
    }
    if (pfd[1].revents & POLLIN_CHECK) {
      work_combine_error(&error,
                         grpc_wakeup_fd_consume_wakeup(&worker->wakeup_fd->fd));
    }
    if (nfds > 2) {
      fd_end_poll(exec_ctx, &fd_watcher, pfd[2].revents & POLLIN_CHECK,
                  pfd[2].revents & POLLOUT_CHECK);
    } else if (fd) {
      fd_end_poll(exec_ctx, &fd_watcher, 0, 0);
    }
  }

  if (fd) {
    GRPC_FD_UNREF(fd, "basicpoll_begin");
  }

  return error;
}

static void basic_pollset_destroy(grpc_pollset *pollset) {
  if (pollset->data.ptr != NULL) {
    GRPC_FD_UNREF(pollset->data.ptr, "basicpoll");
    pollset->data.ptr = NULL;
  }
}

static const grpc_pollset_vtable basic_pollset = {
    basic_pollset_add_fd, basic_pollset_maybe_work_and_unlock,
    basic_pollset_destroy, basic_pollset_destroy};

static void become_basic_pollset(grpc_pollset *pollset, grpc_fd *fd_or_null) {
  pollset->vtable = &basic_pollset;
  pollset->data.ptr = fd_or_null;
  if (fd_or_null != NULL) {
    GRPC_FD_REF(fd_or_null, "basicpoll");
  }
}

/*******************************************************************************
 * pollset_multipoller_with_poll_posix.c
 */

#ifndef GPR_LINUX_MULTIPOLL_WITH_EPOLL

typedef struct {
  /* all polled fds */
  size_t fd_count;
  size_t fd_capacity;
  grpc_fd **fds;
  /* fds that have been removed from the pollset explicitly */
  size_t del_count;
  size_t del_capacity;
  grpc_fd **dels;
} poll_hdr;

static void multipoll_with_poll_pollset_add_fd(grpc_exec_ctx *exec_ctx,
                                               grpc_pollset *pollset,
                                               grpc_fd *fd,
                                               int and_unlock_pollset) {
  size_t i;
  poll_hdr *h = pollset->data.ptr;
  /* TODO(ctiller): this is O(num_fds^2); maybe switch to a hash set here */
  for (i = 0; i < h->fd_count; i++) {
    if (h->fds[i] == fd) goto exit;
  }
  if (h->fd_count == h->fd_capacity) {
    h->fd_capacity = GPR_MAX(h->fd_capacity + 8, h->fd_count * 3 / 2);
    h->fds = gpr_realloc(h->fds, sizeof(grpc_fd *) * h->fd_capacity);
  }
  h->fds[h->fd_count++] = fd;
  GRPC_FD_REF(fd, "multipoller");
exit:
  if (and_unlock_pollset) {
    gpr_mu_unlock(&pollset->mu);
  }
}

static grpc_error *multipoll_with_poll_pollset_maybe_work_and_unlock(
    grpc_exec_ctx *exec_ctx, grpc_pollset *pollset, grpc_pollset_worker *worker,
    gpr_timespec deadline, gpr_timespec now) {
  grpc_error *error = GRPC_ERROR_NONE;

#define POLLOUT_CHECK (POLLOUT | POLLHUP | POLLERR)
#define POLLIN_CHECK (POLLIN | POLLHUP | POLLERR)

  int timeout;
  int r;
  size_t i, j, fd_count;
  nfds_t pfd_count;
  poll_hdr *h;
  /* TODO(ctiller): inline some elements to avoid an allocation */
  grpc_fd_watcher *watchers;
  struct pollfd *pfds;

  h = pollset->data.ptr;
  timeout = poll_deadline_to_millis_timeout(deadline, now);
  /* TODO(ctiller): perform just one malloc here if we exceed the inline case */
  pfds = gpr_malloc(sizeof(*pfds) * (h->fd_count + 2));
  watchers = gpr_malloc(sizeof(*watchers) * (h->fd_count + 2));
  fd_count = 0;
  pfd_count = 2;
  pfds[0].fd = GRPC_WAKEUP_FD_GET_READ_FD(&grpc_global_wakeup_fd);
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;
  pfds[1].fd = GRPC_WAKEUP_FD_GET_READ_FD(&worker->wakeup_fd->fd);
  pfds[1].events = POLLIN;
  pfds[1].revents = 0;
  for (i = 0; i < h->fd_count; i++) {
    int remove = fd_is_orphaned(h->fds[i]);
    for (j = 0; !remove && j < h->del_count; j++) {
      if (h->fds[i] == h->dels[j]) remove = 1;
    }
    if (remove) {
      GRPC_FD_UNREF(h->fds[i], "multipoller");
    } else {
      h->fds[fd_count++] = h->fds[i];
      watchers[pfd_count].fd = h->fds[i];
      GRPC_FD_REF(watchers[pfd_count].fd, "multipoller_start");
      pfds[pfd_count].fd = h->fds[i]->fd;
      pfds[pfd_count].revents = 0;
      pfd_count++;
    }
  }
  for (j = 0; j < h->del_count; j++) {
    GRPC_FD_UNREF(h->dels[j], "multipoller_del");
  }
  h->del_count = 0;
  h->fd_count = fd_count;
  gpr_mu_unlock(&pollset->mu);

  for (i = 2; i < pfd_count; i++) {
    grpc_fd *fd = watchers[i].fd;
    pfds[i].events = (short)fd_begin_poll(fd, pollset, worker, POLLIN, POLLOUT,
                                          &watchers[i]);
    GRPC_FD_UNREF(fd, "multipoller_start");
  }

  /* TODO(vpai): Consider first doing a 0 timeout poll here to avoid
     even going into the blocking annotation if possible */
  GRPC_SCHEDULING_START_BLOCKING_REGION;
  r = grpc_poll_function(pfds, pfd_count, timeout);
  GRPC_SCHEDULING_END_BLOCKING_REGION;

  if (r < 0) {
    if (errno != EINTR) {
      work_combine_error(&error, GRPC_OS_ERROR(errno, "poll"));
    }
    for (i = 2; i < pfd_count; i++) {
      fd_end_poll(exec_ctx, &watchers[i], 0, 0);
    }
  } else if (r == 0) {
    for (i = 2; i < pfd_count; i++) {
      fd_end_poll(exec_ctx, &watchers[i], 0, 0);
    }
  } else {
    if (pfds[0].revents & POLLIN_CHECK) {
      work_combine_error(&error,
                         grpc_wakeup_fd_consume_wakeup(&grpc_global_wakeup_fd));
    }
    if (pfds[1].revents & POLLIN_CHECK) {
      work_combine_error(&error,
                         grpc_wakeup_fd_consume_wakeup(&worker->wakeup_fd->fd));
    }
    for (i = 2; i < pfd_count; i++) {
      if (watchers[i].fd == NULL) {
        fd_end_poll(exec_ctx, &watchers[i], 0, 0);
        continue;
      }
      fd_end_poll(exec_ctx, &watchers[i], pfds[i].revents & POLLIN_CHECK,
                  pfds[i].revents & POLLOUT_CHECK);
    }
  }

  gpr_free(pfds);
  gpr_free(watchers);
  return error;
}

static void multipoll_with_poll_pollset_finish_shutdown(grpc_pollset *pollset) {
  size_t i;
  poll_hdr *h = pollset->data.ptr;
  for (i = 0; i < h->fd_count; i++) {
    GRPC_FD_UNREF(h->fds[i], "multipoller");
  }
  for (i = 0; i < h->del_count; i++) {
    GRPC_FD_UNREF(h->dels[i], "multipoller_del");
  }
  h->fd_count = 0;
  h->del_count = 0;
}

static void multipoll_with_poll_pollset_destroy(grpc_pollset *pollset) {
  poll_hdr *h = pollset->data.ptr;
  multipoll_with_poll_pollset_finish_shutdown(pollset);
  gpr_free(h->fds);
  gpr_free(h->dels);
  gpr_free(h);
}

static const grpc_pollset_vtable multipoll_with_poll_pollset = {
    multipoll_with_poll_pollset_add_fd,
    multipoll_with_poll_pollset_maybe_work_and_unlock,
    multipoll_with_poll_pollset_finish_shutdown,
    multipoll_with_poll_pollset_destroy};

static void poll_become_multipoller(grpc_exec_ctx *exec_ctx,
                                    grpc_pollset *pollset, grpc_fd **fds,
                                    size_t nfds) {
  size_t i;
  poll_hdr *h = gpr_malloc(sizeof(poll_hdr));
  pollset->vtable = &multipoll_with_poll_pollset;
  pollset->data.ptr = h;
  h->fd_count = nfds;
  h->fd_capacity = nfds;
  h->fds = gpr_malloc(nfds * sizeof(grpc_fd *));
  h->del_count = 0;
  h->del_capacity = 0;
  h->dels = NULL;
  for (i = 0; i < nfds; i++) {
    h->fds[i] = fds[i];
    GRPC_FD_REF(fds[i], "multipoller");
  }
}

#endif /* !GPR_LINUX_MULTIPOLL_WITH_EPOLL */

/*******************************************************************************
 * pollset_multipoller_with_epoll_posix.c
 */

#ifdef GPR_LINUX_MULTIPOLL_WITH_EPOLL

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/block_annotate.h"

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

struct epoll_fd_list {
  int *epoll_fds;
  size_t count;
  size_t capacity;
};

static struct epoll_fd_list epoll_fd_global_list;
static gpr_once init_epoll_fd_list_mu = GPR_ONCE_INIT;
static gpr_mu epoll_fd_list_mu;

static void init_mu(void) { gpr_mu_init(&epoll_fd_list_mu); }

static void add_epoll_fd_to_global_list(int epoll_fd) {
  gpr_once_init(&init_epoll_fd_list_mu, init_mu);

  gpr_mu_lock(&epoll_fd_list_mu);
  if (epoll_fd_global_list.count == epoll_fd_global_list.capacity) {
    epoll_fd_global_list.capacity =
        GPR_MAX((size_t)8, epoll_fd_global_list.capacity * 2);
    epoll_fd_global_list.epoll_fds =
        gpr_realloc(epoll_fd_global_list.epoll_fds,
                    epoll_fd_global_list.capacity * sizeof(int));
  }
  epoll_fd_global_list.epoll_fds[epoll_fd_global_list.count++] = epoll_fd;
  gpr_mu_unlock(&epoll_fd_list_mu);
}

static void remove_epoll_fd_from_global_list(int epoll_fd) {
  gpr_mu_lock(&epoll_fd_list_mu);
  GPR_ASSERT(epoll_fd_global_list.count > 0);
  for (size_t i = 0; i < epoll_fd_global_list.count; i++) {
    if (epoll_fd == epoll_fd_global_list.epoll_fds[i]) {
      epoll_fd_global_list.epoll_fds[i] =
          epoll_fd_global_list.epoll_fds[--(epoll_fd_global_list.count)];
      break;
    }
  }
  gpr_mu_unlock(&epoll_fd_list_mu);
}

static void remove_fd_from_all_epoll_sets(int fd) {
  int err;
  gpr_once_init(&init_epoll_fd_list_mu, init_mu);
  gpr_mu_lock(&epoll_fd_list_mu);
  if (epoll_fd_global_list.count == 0) {
    gpr_mu_unlock(&epoll_fd_list_mu);
    return;
  }
  for (size_t i = 0; i < epoll_fd_global_list.count; i++) {
    err = epoll_ctl(epoll_fd_global_list.epoll_fds[i], EPOLL_CTL_DEL, fd, NULL);
    if (err < 0 && errno != ENOENT) {
      gpr_log(GPR_ERROR, "epoll_ctl del for %d failed: %s", fd,
              strerror(errno));
    }
  }
  gpr_mu_unlock(&epoll_fd_list_mu);
}

typedef struct {
  grpc_pollset *pollset;
  grpc_fd *fd;
  grpc_closure closure;
} delayed_add;

typedef struct { int epoll_fd; } epoll_hdr;

static void finally_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_fd *fd) {
  epoll_hdr *h = pollset->data.ptr;
  struct epoll_event ev;
  int err;
  grpc_fd_watcher watcher;

  /* We pretend to be polling whilst adding an fd to keep the fd from being
     closed during the add. This may result in a spurious wakeup being assigned
     to this pollset whilst adding, but that should be benign. */
  GPR_ASSERT(fd_begin_poll(fd, pollset, NULL, 0, 0, &watcher) == 0);
  if (watcher.fd != NULL) {
    ev.events = (uint32_t)(EPOLLIN | EPOLLOUT | EPOLLET);
    ev.data.ptr = fd;
    err = epoll_ctl(h->epoll_fd, EPOLL_CTL_ADD, fd->fd, &ev);
    if (err < 0) {
      /* FDs may be added to a pollset multiple times, so EEXIST is normal. */
      if (errno != EEXIST) {
        gpr_log(GPR_ERROR, "epoll_ctl add for %d failed: %s", fd->fd,
                strerror(errno));
      }
    }
  }
  fd_end_poll(exec_ctx, &watcher, 0, 0);
}

static void perform_delayed_add(grpc_exec_ctx *exec_ctx, void *arg,
                                grpc_error *error) {
  delayed_add *da = arg;

  if (!fd_is_orphaned(da->fd)) {
    finally_add_fd(exec_ctx, da->pollset, da->fd);
  }

  gpr_mu_lock(&da->pollset->mu);
  da->pollset->in_flight_cbs--;
  if (da->pollset->shutting_down) {
    /* We don't care about this pollset anymore. */
    if (da->pollset->in_flight_cbs == 0 && !da->pollset->called_shutdown) {
      da->pollset->called_shutdown = true;
      grpc_exec_ctx_push(exec_ctx, da->pollset->shutdown_done, GRPC_ERROR_NONE,
                         NULL);
    }
  }
  gpr_mu_unlock(&da->pollset->mu);

  GRPC_FD_UNREF(da->fd, "delayed_add");

  gpr_free(da);
}

static void multipoll_with_epoll_pollset_add_fd(grpc_exec_ctx *exec_ctx,
                                                grpc_pollset *pollset,
                                                grpc_fd *fd,
                                                int and_unlock_pollset) {
  if (and_unlock_pollset) {
    gpr_mu_unlock(&pollset->mu);
    finally_add_fd(exec_ctx, pollset, fd);
  } else {
    delayed_add *da = gpr_malloc(sizeof(*da));
    da->pollset = pollset;
    da->fd = fd;
    GRPC_FD_REF(fd, "delayed_add");
    grpc_closure_init(&da->closure, perform_delayed_add, da);
    pollset->in_flight_cbs++;
    grpc_exec_ctx_push(exec_ctx, &da->closure, GRPC_ERROR_NONE, NULL);
  }
}

/* TODO(klempner): We probably want to turn this down a bit */
#define GRPC_EPOLL_MAX_EVENTS 1000

static grpc_error *multipoll_with_epoll_pollset_maybe_work_and_unlock(
    grpc_exec_ctx *exec_ctx, grpc_pollset *pollset, grpc_pollset_worker *worker,
    gpr_timespec deadline, gpr_timespec now) {
  struct epoll_event ep_ev[GRPC_EPOLL_MAX_EVENTS];
  int ep_rv;
  int poll_rv;
  epoll_hdr *h = pollset->data.ptr;
  int timeout_ms;
  struct pollfd pfds[2];
  grpc_error *error = GRPC_ERROR_NONE;

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
  pfds[1].fd = h->epoll_fd;
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
      work_combine_error(&error, GRPC_OS_ERROR(errno, "poll"));
    }
  } else if (poll_rv == 0) {
    /* do nothing */
  } else {
    if (pfds[0].revents) {
      work_combine_error(&error,
                         grpc_wakeup_fd_consume_wakeup(&worker->wakeup_fd->fd));
    }
    if (pfds[1].revents) {
      do {
        /* The following epoll_wait never blocks; it has a timeout of 0 */
        ep_rv = epoll_wait(h->epoll_fd, ep_ev, GRPC_EPOLL_MAX_EVENTS, 0);
        if (ep_rv < 0) {
          if (errno != EINTR) {
            work_combine_error(&error, GRPC_OS_ERROR(errno, "epoll_wait"));
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
              work_combine_error(&error, grpc_wakeup_fd_consume_wakeup(
                                             &grpc_global_wakeup_fd));
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
  return error;
}

static void multipoll_with_epoll_pollset_finish_shutdown(
    grpc_pollset *pollset) {}

static void multipoll_with_epoll_pollset_destroy(grpc_pollset *pollset) {
  epoll_hdr *h = pollset->data.ptr;
  close(h->epoll_fd);
  remove_epoll_fd_from_global_list(h->epoll_fd);
  gpr_free(h);
}

static const grpc_pollset_vtable multipoll_with_epoll_pollset = {
    multipoll_with_epoll_pollset_add_fd,
    multipoll_with_epoll_pollset_maybe_work_and_unlock,
    multipoll_with_epoll_pollset_finish_shutdown,
    multipoll_with_epoll_pollset_destroy};

static void epoll_become_multipoller(grpc_exec_ctx *exec_ctx,
                                     grpc_pollset *pollset, grpc_fd **fds,
                                     size_t nfds) {
  size_t i;
  epoll_hdr *h = gpr_malloc(sizeof(epoll_hdr));
  struct epoll_event ev;
  int err;

  pollset->vtable = &multipoll_with_epoll_pollset;
  pollset->data.ptr = h;
  h->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (h->epoll_fd < 0) {
    /* TODO(klempner): Fall back to poll here, especially on ENOSYS */
    gpr_log(GPR_ERROR, "epoll_create1 failed: %s", strerror(errno));
    abort();
  }
  add_epoll_fd_to_global_list(h->epoll_fd);

  ev.events = (uint32_t)(EPOLLIN | EPOLLET);
  ev.data.ptr = NULL;
  err = epoll_ctl(h->epoll_fd, EPOLL_CTL_ADD,
                  GRPC_WAKEUP_FD_GET_READ_FD(&grpc_global_wakeup_fd), &ev);
  if (err < 0) {
    gpr_log(GPR_ERROR, "epoll_ctl add for %d failed: %s",
            GRPC_WAKEUP_FD_GET_READ_FD(&grpc_global_wakeup_fd),
            strerror(errno));
  }

  for (i = 0; i < nfds; i++) {
    multipoll_with_epoll_pollset_add_fd(exec_ctx, pollset, fds[i], 0);
  }
}

#else /* GPR_LINUX_MULTIPOLL_WITH_EPOLL */

static void remove_fd_from_all_epoll_sets(int fd) {}

#endif /* GPR_LINUX_MULTIPOLL_WITH_EPOLL */

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

const grpc_event_engine_vtable *grpc_init_poll_and_epoll_posix(void) {
  const char *msg;
  grpc_error *err = GRPC_ERROR_NONE;
#ifdef GPR_LINUX_MULTIPOLL_WITH_EPOLL
  platform_become_multipoller = epoll_become_multipoller;
#else
  platform_become_multipoller = poll_become_multipoller;
#endif
  fd_global_init();
  err = pollset_global_init();
  if (err != GRPC_ERROR_NONE) goto error;
  return &vtable;

error:
  msg = grpc_error_string(err);
  gpr_log(GPR_ERROR, "%s", msg);
  grpc_error_free_string(msg);
  return NULL;
}

#endif
