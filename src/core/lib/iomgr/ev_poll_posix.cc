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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/ev_poll_posix.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/thd.h>
#include <grpc/support/tls.h>
#include <grpc/support/useful.h>

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/murmur_hash.h"
#include "src/core/lib/iomgr/block_annotate.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/wakeup_fd_cv.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/lib/profiling/timers.h"

#define GRPC_POLLSET_KICK_BROADCAST ((grpc_pollset_worker*)1)

/*******************************************************************************
 * FD declarations
 */
typedef struct grpc_fd_watcher {
  struct grpc_fd_watcher* next;
  struct grpc_fd_watcher* prev;
  grpc_pollset* pollset;
  grpc_pollset_worker* worker;
  grpc_fd* fd;
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
  int shutdown;
  int closed;
  int released;
  gpr_atm pollhup;
  grpc_error* shutdown_error;

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
  grpc_fd_watcher* read_watcher;
  grpc_fd_watcher* write_watcher;

  grpc_closure* read_closure;
  grpc_closure* write_closure;

  grpc_closure* on_done_closure;

  grpc_iomgr_object iomgr_object;

  /* The pollset that last noticed and notified that the fd is readable */
  grpc_pollset* read_notifier_pollset;
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
static uint32_t fd_begin_poll(grpc_fd* fd, grpc_pollset* pollset,
                              grpc_pollset_worker* worker, uint32_t read_mask,
                              uint32_t write_mask, grpc_fd_watcher* rec);
/* Complete polling previously started with fd_begin_poll
   MUST NOT be called with a pollset lock taken
   if got_read or got_write are 1, also does the become_{readable,writable} as
   appropriate. */
static void fd_end_poll(grpc_fd_watcher* rec, int got_read, int got_write,
                        grpc_pollset* read_notifier_pollset);

/* Return 1 if this fd is orphaned, 0 otherwise */
static bool fd_is_orphaned(grpc_fd* fd);

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

#define CLOSURE_NOT_READY ((grpc_closure*)0)
#define CLOSURE_READY ((grpc_closure*)1)

/*******************************************************************************
 * pollset declarations
 */

typedef struct grpc_cached_wakeup_fd {
  grpc_wakeup_fd fd;
  struct grpc_cached_wakeup_fd* next;
} grpc_cached_wakeup_fd;

struct grpc_pollset_worker {
  grpc_cached_wakeup_fd* wakeup_fd;
  int reevaluate_polling_on_wakeup;
  int kicked_specifically;
  struct grpc_pollset_worker* next;
  struct grpc_pollset_worker* prev;
};

struct grpc_pollset {
  gpr_mu mu;
  grpc_pollset_worker root_worker;
  int shutting_down;
  int called_shutdown;
  int kicked_without_pollers;
  grpc_closure* shutdown_done;
  grpc_closure_list idle_jobs;
  int pollset_set_count;
  /* all polled fds */
  size_t fd_count;
  size_t fd_capacity;
  grpc_fd** fds;
  /* Local cache of eventfds for workers */
  grpc_cached_wakeup_fd* local_wakeup_cache;
};

/* Add an fd to a pollset */
static void pollset_add_fd(grpc_pollset* pollset, struct grpc_fd* fd);

static void pollset_set_add_fd(grpc_pollset_set* pollset_set, grpc_fd* fd);

/* Convert a timespec to milliseconds:
   - very small or negative poll times are clamped to zero to do a
     non-blocking poll (which becomes spin polling)
   - other small values are rounded up to one millisecond
   - longer than a millisecond polls are rounded up to the next nearest
     millisecond to avoid spinning
   - infinite timeouts are converted to -1 */
static int poll_deadline_to_millis_timeout(grpc_millis deadline);

/* Allow kick to wakeup the currently polling worker */
#define GRPC_POLLSET_CAN_KICK_SELF 1
/* Force the wakee to repoll when awoken */
#define GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP 2
/* As per pollset_kick, with an extended set of flags (defined above)
   -- mostly for fd_posix's use. */
static grpc_error* pollset_kick_ext(grpc_pollset* p,
                                    grpc_pollset_worker* specific_worker,
                                    uint32_t flags) GRPC_MUST_USE_RESULT;

/* Return 1 if the pollset has active threads in pollset_work (pollset must
 * be locked) */
static bool pollset_has_workers(grpc_pollset* pollset);

/*******************************************************************************
 * pollset_set definitions
 */

struct grpc_pollset_set {
  gpr_mu mu;

  size_t pollset_count;
  size_t pollset_capacity;
  grpc_pollset** pollsets;

  size_t pollset_set_count;
  size_t pollset_set_capacity;
  struct grpc_pollset_set** pollset_sets;

  size_t fd_count;
  size_t fd_capacity;
  grpc_fd** fds;
};

/*******************************************************************************
 * condition variable polling definitions
 */

#define POLLCV_THREAD_GRACE_MS 1000
#define CV_POLL_PERIOD_MS 1000
#define CV_DEFAULT_TABLE_SIZE 16

typedef struct poll_result {
  gpr_refcount refcount;
  grpc_cv_node* watchers;
  int watchcount;
  struct pollfd* fds;
  nfds_t nfds;
  int retval;
  int err;
  int completed;
} poll_result;

typedef struct poll_args {
  gpr_cv trigger;
  int trigger_set;
  struct pollfd* fds;
  nfds_t nfds;
  poll_result* result;
  struct poll_args* next;
  struct poll_args* prev;
} poll_args;

// This is a 2-tiered cache, we mantain a hash table
// of active poll calls, so we can wait on the result
// of that call.  We also maintain a freelist of inactive
// poll threads.
typedef struct poll_hash_table {
  poll_args* free_pollers;
  poll_args** active_pollers;
  unsigned int size;
  unsigned int count;
} poll_hash_table;

poll_hash_table poll_cache;
grpc_cv_fd_table g_cvfds;

/*******************************************************************************
 * fd_posix.c
 */

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
    gpr_mu_destroy(&fd->mu);
    grpc_iomgr_unregister_object(&fd->iomgr_object);
    if (fd->shutdown) GRPC_ERROR_UNREF(fd->shutdown_error);
    gpr_free(fd);
  } else {
    GPR_ASSERT(old > n);
  }
}

static grpc_fd* fd_create(int fd, const char* name) {
  grpc_fd* r = (grpc_fd*)gpr_malloc(sizeof(*r));
  gpr_mu_init(&r->mu);
  gpr_atm_rel_store(&r->refst, 1);
  r->shutdown = 0;
  r->read_closure = CLOSURE_NOT_READY;
  r->write_closure = CLOSURE_NOT_READY;
  r->fd = fd;
  r->inactive_watcher_root.next = r->inactive_watcher_root.prev =
      &r->inactive_watcher_root;
  r->read_watcher = r->write_watcher = nullptr;
  r->on_done_closure = nullptr;
  r->closed = 0;
  r->released = 0;
  gpr_atm_no_barrier_store(&r->pollhup, 0);
  r->read_notifier_pollset = nullptr;

  char* name2;
  gpr_asprintf(&name2, "%s fd=%d", name, fd);
  grpc_iomgr_register_object(&r->iomgr_object, name2);
  gpr_free(name2);
  return r;
}

static bool fd_is_orphaned(grpc_fd* fd) {
  return (gpr_atm_acq_load(&fd->refst) & 1) == 0;
}

/* Return the read-notifier pollset */
static grpc_pollset* fd_get_read_notifier_pollset(grpc_fd* fd) {
  grpc_pollset* notifier = nullptr;

  gpr_mu_lock(&fd->mu);
  notifier = fd->read_notifier_pollset;
  gpr_mu_unlock(&fd->mu);

  return notifier;
}

static grpc_error* pollset_kick_locked(grpc_fd_watcher* watcher) {
  gpr_mu_lock(&watcher->pollset->mu);
  GPR_ASSERT(watcher->worker);
  grpc_error* err = pollset_kick_ext(watcher->pollset, watcher->worker,
                                     GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP);
  gpr_mu_unlock(&watcher->pollset->mu);
  return err;
}

static void maybe_wake_one_watcher_locked(grpc_fd* fd) {
  if (fd->inactive_watcher_root.next != &fd->inactive_watcher_root) {
    pollset_kick_locked(fd->inactive_watcher_root.next);
  } else if (fd->read_watcher) {
    pollset_kick_locked(fd->read_watcher);
  } else if (fd->write_watcher) {
    pollset_kick_locked(fd->write_watcher);
  }
}

static void wake_all_watchers_locked(grpc_fd* fd) {
  grpc_fd_watcher* watcher;
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

static int has_watchers(grpc_fd* fd) {
  return fd->read_watcher != nullptr || fd->write_watcher != nullptr ||
         fd->inactive_watcher_root.next != &fd->inactive_watcher_root;
}

static void close_fd_locked(grpc_fd* fd) {
  fd->closed = 1;
  if (!fd->released) {
    close(fd->fd);
  }
  GRPC_CLOSURE_SCHED(fd->on_done_closure, GRPC_ERROR_NONE);
}

static int fd_wrapped_fd(grpc_fd* fd) {
  if (fd->released || fd->closed) {
    return -1;
  } else {
    return fd->fd;
  }
}

static void fd_orphan(grpc_fd* fd, grpc_closure* on_done, int* release_fd,
                      bool already_closed, const char* reason) {
  fd->on_done_closure = on_done;
  fd->released = release_fd != nullptr;
  if (release_fd != nullptr) {
    *release_fd = fd->fd;
    fd->released = true;
  } else if (already_closed) {
    fd->released = true;
  }
  gpr_mu_lock(&fd->mu);
  REF_BY(fd, 1, reason); /* remove active status, but keep referenced */
  if (!has_watchers(fd)) {
    close_fd_locked(fd);
  } else {
    wake_all_watchers_locked(fd);
  }
  gpr_mu_unlock(&fd->mu);
  UNREF_BY(fd, 2, reason); /* drop the reference */
}

/* increment refcount by two to avoid changing the orphan bit */
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

static grpc_error* fd_shutdown_error(grpc_fd* fd) {
  if (!fd->shutdown) {
    return GRPC_ERROR_NONE;
  } else {
    return GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "FD shutdown", &fd->shutdown_error, 1);
  }
}

static void notify_on_locked(grpc_fd* fd, grpc_closure** st,
                             grpc_closure* closure) {
  if (fd->shutdown || gpr_atm_no_barrier_load(&fd->pollhup)) {
    GRPC_CLOSURE_SCHED(closure,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("FD shutdown"));
  } else if (*st == CLOSURE_NOT_READY) {
    /* not ready ==> switch to a waiting state by setting the closure */
    *st = closure;
  } else if (*st == CLOSURE_READY) {
    /* already ready ==> queue the closure to run immediately */
    *st = CLOSURE_NOT_READY;
    GRPC_CLOSURE_SCHED(closure, fd_shutdown_error(fd));
    maybe_wake_one_watcher_locked(fd);
  } else {
    /* upcallptr was set to a different closure.  This is an error! */
    gpr_log(GPR_ERROR,
            "User called a notify_on function with a previous callback still "
            "pending");
    abort();
  }
}

/* returns 1 if state becomes not ready */
static int set_ready_locked(grpc_fd* fd, grpc_closure** st) {
  if (*st == CLOSURE_READY) {
    /* duplicate ready ==> ignore */
    return 0;
  } else if (*st == CLOSURE_NOT_READY) {
    /* not ready, and not waiting ==> flag ready */
    *st = CLOSURE_READY;
    return 0;
  } else {
    /* waiting ==> queue closure */
    GRPC_CLOSURE_SCHED(*st, fd_shutdown_error(fd));
    *st = CLOSURE_NOT_READY;
    return 1;
  }
}

static void set_read_notifier_pollset_locked(
    grpc_fd* fd, grpc_pollset* read_notifier_pollset) {
  fd->read_notifier_pollset = read_notifier_pollset;
}

static void fd_shutdown(grpc_fd* fd, grpc_error* why) {
  gpr_mu_lock(&fd->mu);
  /* only shutdown once */
  if (!fd->shutdown) {
    fd->shutdown = 1;
    fd->shutdown_error = why;
    /* signal read/write closed to OS so that future operations fail */
    shutdown(fd->fd, SHUT_RDWR);
    set_ready_locked(fd, &fd->read_closure);
    set_ready_locked(fd, &fd->write_closure);
  } else {
    GRPC_ERROR_UNREF(why);
  }
  gpr_mu_unlock(&fd->mu);
}

static bool fd_is_shutdown(grpc_fd* fd) {
  gpr_mu_lock(&fd->mu);
  bool r = fd->shutdown;
  gpr_mu_unlock(&fd->mu);
  return r;
}

static void fd_notify_on_read(grpc_fd* fd, grpc_closure* closure) {
  gpr_mu_lock(&fd->mu);
  notify_on_locked(fd, &fd->read_closure, closure);
  gpr_mu_unlock(&fd->mu);
}

static void fd_notify_on_write(grpc_fd* fd, grpc_closure* closure) {
  gpr_mu_lock(&fd->mu);
  notify_on_locked(fd, &fd->write_closure, closure);
  gpr_mu_unlock(&fd->mu);
}

static uint32_t fd_begin_poll(grpc_fd* fd, grpc_pollset* pollset,
                              grpc_pollset_worker* worker, uint32_t read_mask,
                              uint32_t write_mask, grpc_fd_watcher* watcher) {
  uint32_t mask = 0;
  grpc_closure* cur;
  int requested;
  /* keep track of pollers that have requested our events, in case they change
   */
  GRPC_FD_REF(fd, "poll");

  gpr_mu_lock(&fd->mu);

  /* if we are shutdown, then don't add to the watcher set */
  if (fd->shutdown) {
    watcher->fd = nullptr;
    watcher->pollset = nullptr;
    watcher->worker = nullptr;
    gpr_mu_unlock(&fd->mu);
    GRPC_FD_UNREF(fd, "poll");
    return 0;
  }

  /* if there is nobody polling for read, but we need to, then start doing so */
  cur = fd->read_closure;
  requested = cur != CLOSURE_READY;
  if (read_mask && fd->read_watcher == nullptr && requested) {
    fd->read_watcher = watcher;
    mask |= read_mask;
  }
  /* if there is nobody polling for write, but we need to, then start doing so
   */
  cur = fd->write_closure;
  requested = cur != CLOSURE_READY;
  if (write_mask && fd->write_watcher == nullptr && requested) {
    fd->write_watcher = watcher;
    mask |= write_mask;
  }
  /* if not polling, remember this watcher in case we need someone to later */
  if (mask == 0 && worker != nullptr) {
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

static void fd_end_poll(grpc_fd_watcher* watcher, int got_read, int got_write,
                        grpc_pollset* read_notifier_pollset) {
  int was_polling = 0;
  int kick = 0;
  grpc_fd* fd = watcher->fd;

  if (fd == nullptr) {
    return;
  }

  gpr_mu_lock(&fd->mu);

  if (watcher == fd->read_watcher) {
    /* remove read watcher, kick if we still need a read */
    was_polling = 1;
    if (!got_read) {
      kick = 1;
    }
    fd->read_watcher = nullptr;
  }
  if (watcher == fd->write_watcher) {
    /* remove write watcher, kick if we still need a write */
    was_polling = 1;
    if (!got_write) {
      kick = 1;
    }
    fd->write_watcher = nullptr;
  }
  if (!was_polling && watcher->worker != nullptr) {
    /* remove from inactive list */
    watcher->next->prev = watcher->prev;
    watcher->prev->next = watcher->next;
  }
  if (got_read) {
    if (set_ready_locked(fd, &fd->read_closure)) {
      kick = 1;
    }
    if (read_notifier_pollset != nullptr) {
      set_read_notifier_pollset_locked(fd, read_notifier_pollset);
    }
  }
  if (got_write) {
    if (set_ready_locked(fd, &fd->write_closure)) {
      kick = 1;
    }
  }
  if (kick) {
    maybe_wake_one_watcher_locked(fd);
  }
  if (fd_is_orphaned(fd) && !has_watchers(fd) && !fd->closed) {
    close_fd_locked(fd);
  }
  gpr_mu_unlock(&fd->mu);

  GRPC_FD_UNREF(fd, "poll");
}

/*******************************************************************************
 * pollset_posix.c
 */

GPR_TLS_DECL(g_current_thread_poller);
GPR_TLS_DECL(g_current_thread_worker);

static void remove_worker(grpc_pollset* p, grpc_pollset_worker* worker) {
  worker->prev->next = worker->next;
  worker->next->prev = worker->prev;
}

static bool pollset_has_workers(grpc_pollset* p) {
  return p->root_worker.next != &p->root_worker;
}

static bool pollset_in_pollset_sets(grpc_pollset* p) {
  return p->pollset_set_count;
}

static bool pollset_has_observers(grpc_pollset* p) {
  return pollset_has_workers(p) || pollset_in_pollset_sets(p);
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

static void kick_append_error(grpc_error** composite, grpc_error* error) {
  if (error == GRPC_ERROR_NONE) return;
  if (*composite == GRPC_ERROR_NONE) {
    *composite = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Kick Failure");
  }
  *composite = grpc_error_add_child(*composite, error);
}

static grpc_error* pollset_kick_ext(grpc_pollset* p,
                                    grpc_pollset_worker* specific_worker,
                                    uint32_t flags) {
  GPR_TIMER_BEGIN("pollset_kick_ext", 0);
  grpc_error* error = GRPC_ERROR_NONE;
  GRPC_STATS_INC_POLLSET_KICK();

  /* pollset->mu already held */
  if (specific_worker != nullptr) {
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
    if (specific_worker != nullptr) {
      if (gpr_tls_get(&g_current_thread_worker) == (intptr_t)specific_worker) {
        GPR_TIMER_MARK("kick_anonymous_not_self", 0);
        push_back_worker(p, specific_worker);
        specific_worker = pop_front_worker(p);
        if ((flags & GRPC_POLLSET_CAN_KICK_SELF) == 0 &&
            gpr_tls_get(&g_current_thread_worker) ==
                (intptr_t)specific_worker) {
          push_back_worker(p, specific_worker);
          specific_worker = nullptr;
        }
      }
      if (specific_worker != nullptr) {
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
  GRPC_LOG_IF_ERROR("pollset_kick_ext", GRPC_ERROR_REF(error));
  return error;
}

static grpc_error* pollset_kick(grpc_pollset* p,
                                grpc_pollset_worker* specific_worker) {
  return pollset_kick_ext(p, specific_worker, 0);
}

/* global state management */

static grpc_error* pollset_global_init(void) {
  gpr_tls_init(&g_current_thread_poller);
  gpr_tls_init(&g_current_thread_worker);
  return GRPC_ERROR_NONE;
}

static void pollset_global_shutdown(void) {
  gpr_tls_destroy(&g_current_thread_poller);
  gpr_tls_destroy(&g_current_thread_worker);
}

/* main interface */

static void pollset_init(grpc_pollset* pollset, gpr_mu** mu) {
  gpr_mu_init(&pollset->mu);
  *mu = &pollset->mu;
  pollset->root_worker.next = pollset->root_worker.prev = &pollset->root_worker;
  pollset->shutting_down = 0;
  pollset->called_shutdown = 0;
  pollset->kicked_without_pollers = 0;
  pollset->idle_jobs.head = pollset->idle_jobs.tail = nullptr;
  pollset->local_wakeup_cache = nullptr;
  pollset->kicked_without_pollers = 0;
  pollset->fd_count = 0;
  pollset->fd_capacity = 0;
  pollset->fds = nullptr;
  pollset->pollset_set_count = 0;
}

static void pollset_destroy(grpc_pollset* pollset) {
  GPR_ASSERT(!pollset_has_workers(pollset));
  GPR_ASSERT(pollset->idle_jobs.head == pollset->idle_jobs.tail);
  while (pollset->local_wakeup_cache) {
    grpc_cached_wakeup_fd* next = pollset->local_wakeup_cache->next;
    grpc_wakeup_fd_destroy(&pollset->local_wakeup_cache->fd);
    gpr_free(pollset->local_wakeup_cache);
    pollset->local_wakeup_cache = next;
  }
  gpr_free(pollset->fds);
  gpr_mu_destroy(&pollset->mu);
}

static void pollset_add_fd(grpc_pollset* pollset, grpc_fd* fd) {
  gpr_mu_lock(&pollset->mu);
  size_t i;
  /* TODO(ctiller): this is O(num_fds^2); maybe switch to a hash set here */
  for (i = 0; i < pollset->fd_count; i++) {
    if (pollset->fds[i] == fd) goto exit;
  }
  if (pollset->fd_count == pollset->fd_capacity) {
    pollset->fd_capacity =
        GPR_MAX(pollset->fd_capacity + 8, pollset->fd_count * 3 / 2);
    pollset->fds = (grpc_fd**)gpr_realloc(
        pollset->fds, sizeof(grpc_fd*) * pollset->fd_capacity);
  }
  pollset->fds[pollset->fd_count++] = fd;
  GRPC_FD_REF(fd, "multipoller");
  pollset_kick(pollset, nullptr);
exit:
  gpr_mu_unlock(&pollset->mu);
}

static void finish_shutdown(grpc_pollset* pollset) {
  GPR_ASSERT(grpc_closure_list_empty(pollset->idle_jobs));
  size_t i;
  for (i = 0; i < pollset->fd_count; i++) {
    GRPC_FD_UNREF(pollset->fds[i], "multipoller");
  }
  pollset->fd_count = 0;
  GRPC_CLOSURE_SCHED(pollset->shutdown_done, GRPC_ERROR_NONE);
}

static void work_combine_error(grpc_error** composite, grpc_error* error) {
  if (error == GRPC_ERROR_NONE) return;
  if (*composite == GRPC_ERROR_NONE) {
    *composite = GRPC_ERROR_CREATE_FROM_STATIC_STRING("pollset_work");
  }
  *composite = grpc_error_add_child(*composite, error);
}

static grpc_error* pollset_work(grpc_pollset* pollset,
                                grpc_pollset_worker** worker_hdl,
                                grpc_millis deadline) {
  grpc_pollset_worker worker;
  if (worker_hdl) *worker_hdl = &worker;
  grpc_error* error = GRPC_ERROR_NONE;

  /* Avoid malloc for small number of elements. */
  enum { inline_elements = 96 };
  struct pollfd pollfd_space[inline_elements];
  struct grpc_fd_watcher watcher_space[inline_elements];

  /* pollset->mu already held */
  int added_worker = 0;
  int locked = 1;
  int queued_work = 0;
  int keep_polling = 0;
  GPR_TIMER_BEGIN("pollset_work", 0);
  /* this must happen before we (potentially) drop pollset->mu */
  worker.next = worker.prev = nullptr;
  worker.reevaluate_polling_on_wakeup = 0;
  if (pollset->local_wakeup_cache != nullptr) {
    worker.wakeup_fd = pollset->local_wakeup_cache;
    pollset->local_wakeup_cache = worker.wakeup_fd->next;
  } else {
    worker.wakeup_fd =
        (grpc_cached_wakeup_fd*)gpr_malloc(sizeof(*worker.wakeup_fd));
    error = grpc_wakeup_fd_init(&worker.wakeup_fd->fd);
    if (error != GRPC_ERROR_NONE) {
      GRPC_LOG_IF_ERROR("pollset_work", GRPC_ERROR_REF(error));
      return error;
    }
  }
  worker.kicked_specifically = 0;
  /* If there's work waiting for the pollset to be idle, and the
     pollset is idle, then do that work */
  if (!pollset_has_workers(pollset) &&
      !grpc_closure_list_empty(pollset->idle_jobs)) {
    GPR_TIMER_MARK("pollset_work.idle_jobs", 0);
    GRPC_CLOSURE_LIST_SCHED(&pollset->idle_jobs);
    goto done;
  }
  /* If we're shutting down then we don't execute any extended work */
  if (pollset->shutting_down) {
    GPR_TIMER_MARK("pollset_work.shutting_down", 0);
    goto done;
  }
  /* Start polling, and keep doing so while we're being asked to
     re-evaluate our pollers (this allows poll() based pollers to
     ensure they don't miss wakeups) */
  keep_polling = 1;
  gpr_tls_set(&g_current_thread_poller, (intptr_t)pollset);
  while (keep_polling) {
    keep_polling = 0;
    if (!pollset->kicked_without_pollers) {
      if (!added_worker) {
        push_front_worker(pollset, &worker);
        added_worker = 1;
        gpr_tls_set(&g_current_thread_worker, (intptr_t)&worker);
      }
      GPR_TIMER_BEGIN("maybe_work_and_unlock", 0);
#define POLLOUT_CHECK (POLLOUT | POLLHUP | POLLERR)
#define POLLIN_CHECK (POLLIN | POLLHUP | POLLERR)

      int timeout;
      int r;
      size_t i, fd_count;
      nfds_t pfd_count;
      grpc_fd_watcher* watchers;
      struct pollfd* pfds;

      timeout = poll_deadline_to_millis_timeout(deadline);

      if (pollset->fd_count + 2 <= inline_elements) {
        pfds = pollfd_space;
        watchers = watcher_space;
      } else {
        /* Allocate one buffer to hold both pfds and watchers arrays */
        const size_t pfd_size = sizeof(*pfds) * (pollset->fd_count + 2);
        const size_t watch_size = sizeof(*watchers) * (pollset->fd_count + 2);
        void* buf = gpr_malloc(pfd_size + watch_size);
        pfds = (struct pollfd*)buf;
        watchers = (grpc_fd_watcher*)(void*)((char*)buf + pfd_size);
      }

      fd_count = 0;
      pfd_count = 1;
      pfds[0].fd = GRPC_WAKEUP_FD_GET_READ_FD(&worker.wakeup_fd->fd);
      pfds[0].events = POLLIN;
      pfds[0].revents = 0;
      for (i = 0; i < pollset->fd_count; i++) {
        if (fd_is_orphaned(pollset->fds[i]) ||
            gpr_atm_no_barrier_load(&pollset->fds[i]->pollhup) == 1) {
          GRPC_FD_UNREF(pollset->fds[i], "multipoller");
        } else {
          pollset->fds[fd_count++] = pollset->fds[i];
          watchers[pfd_count].fd = pollset->fds[i];
          GRPC_FD_REF(watchers[pfd_count].fd, "multipoller_start");
          pfds[pfd_count].fd = pollset->fds[i]->fd;
          pfds[pfd_count].revents = 0;
          pfd_count++;
        }
      }
      pollset->fd_count = fd_count;
      gpr_mu_unlock(&pollset->mu);

      for (i = 1; i < pfd_count; i++) {
        grpc_fd* fd = watchers[i].fd;
        pfds[i].events = (short)fd_begin_poll(fd, pollset, &worker, POLLIN,
                                              POLLOUT, &watchers[i]);
        GRPC_FD_UNREF(fd, "multipoller_start");
      }

      /* TODO(vpai): Consider first doing a 0 timeout poll here to avoid
         even going into the blocking annotation if possible */
      GRPC_SCHEDULING_START_BLOCKING_REGION;
      GRPC_STATS_INC_SYSCALL_POLL();
      r = grpc_poll_function(pfds, pfd_count, timeout);
      GRPC_SCHEDULING_END_BLOCKING_REGION;

      if (grpc_polling_trace.enabled()) {
        gpr_log(GPR_DEBUG, "%p poll=%d", pollset, r);
      }

      if (r < 0) {
        if (errno != EINTR) {
          work_combine_error(&error, GRPC_OS_ERROR(errno, "poll"));
        }

        for (i = 1; i < pfd_count; i++) {
          if (watchers[i].fd == nullptr) {
            fd_end_poll(&watchers[i], 0, 0, nullptr);
          } else {
            // Wake up all the file descriptors, if we have an invalid one
            // we can identify it on the next pollset_work()
            fd_end_poll(&watchers[i], 1, 1, pollset);
          }
        }
      } else if (r == 0) {
        for (i = 1; i < pfd_count; i++) {
          fd_end_poll(&watchers[i], 0, 0, nullptr);
        }
      } else {
        if (pfds[0].revents & POLLIN_CHECK) {
          if (grpc_polling_trace.enabled()) {
            gpr_log(GPR_DEBUG, "%p: got_wakeup", pollset);
          }
          work_combine_error(
              &error, grpc_wakeup_fd_consume_wakeup(&worker.wakeup_fd->fd));
        }
        for (i = 1; i < pfd_count; i++) {
          if (watchers[i].fd == nullptr) {
            fd_end_poll(&watchers[i], 0, 0, nullptr);
          } else {
            if (grpc_polling_trace.enabled()) {
              gpr_log(GPR_DEBUG, "%p got_event: %d r:%d w:%d [%d]", pollset,
                      pfds[i].fd, (pfds[i].revents & POLLIN_CHECK) != 0,
                      (pfds[i].revents & POLLOUT_CHECK) != 0, pfds[i].revents);
            }
            /* This is a mitigation to prevent poll() from spinning on a
             ** POLLHUP https://github.com/grpc/grpc/pull/13665
             */
            if (pfds[i].revents & POLLHUP) {
              gpr_atm_no_barrier_store(&watchers[i].fd->pollhup, 1);
            }
            fd_end_poll(&watchers[i], pfds[i].revents & POLLIN_CHECK,
                        pfds[i].revents & POLLOUT_CHECK, pollset);
          }
        }
      }

      if (pfds != pollfd_space) {
        /* pfds and watchers are in the same memory block pointed to by pfds */
        gpr_free(pfds);
      }

      GPR_TIMER_END("maybe_work_and_unlock", 0);
      locked = 0;
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
      queued_work |= grpc_core::ExecCtx::Get()->Flush();
      gpr_mu_lock(&pollset->mu);
      locked = 1;
    }
    /* If we're forced to re-evaluate polling (via pollset_kick with
       GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP) then we land here and force
       a loop */
    if (worker.reevaluate_polling_on_wakeup && error == GRPC_ERROR_NONE) {
      worker.reevaluate_polling_on_wakeup = 0;
      pollset->kicked_without_pollers = 0;
      if (queued_work || worker.kicked_specifically) {
        /* If there's queued work on the list, then set the deadline to be
           immediate so we get back out of the polling loop quickly */
        deadline = 0;
      }
      keep_polling = 1;
    }
  }
  gpr_tls_set(&g_current_thread_poller, 0);
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
      pollset_kick(pollset, nullptr);
    } else if (!pollset->called_shutdown && !pollset_has_observers(pollset)) {
      pollset->called_shutdown = 1;
      gpr_mu_unlock(&pollset->mu);
      finish_shutdown(pollset);
      grpc_core::ExecCtx::Get()->Flush();
      /* Continuing to access pollset here is safe -- it is the caller's
       * responsibility to not destroy when it has outstanding calls to
       * pollset_work.
       * TODO(dklempner): Can we refactor the shutdown logic to avoid this? */
      gpr_mu_lock(&pollset->mu);
    } else if (!grpc_closure_list_empty(pollset->idle_jobs)) {
      GRPC_CLOSURE_LIST_SCHED(&pollset->idle_jobs);
      gpr_mu_unlock(&pollset->mu);
      grpc_core::ExecCtx::Get()->Flush();
      gpr_mu_lock(&pollset->mu);
    }
  }
  if (worker_hdl) *worker_hdl = nullptr;
  GPR_TIMER_END("pollset_work", 0);
  GRPC_LOG_IF_ERROR("pollset_work", GRPC_ERROR_REF(error));
  return error;
}

static void pollset_shutdown(grpc_pollset* pollset, grpc_closure* closure) {
  GPR_ASSERT(!pollset->shutting_down);
  pollset->shutting_down = 1;
  pollset->shutdown_done = closure;
  pollset_kick(pollset, GRPC_POLLSET_KICK_BROADCAST);
  if (!pollset_has_workers(pollset)) {
    GRPC_CLOSURE_LIST_SCHED(&pollset->idle_jobs);
  }
  if (!pollset->called_shutdown && !pollset_has_observers(pollset)) {
    pollset->called_shutdown = 1;
    finish_shutdown(pollset);
  }
}

static int poll_deadline_to_millis_timeout(grpc_millis deadline) {
  if (deadline == GRPC_MILLIS_INF_FUTURE) return -1;
  if (deadline == 0) return 0;
  grpc_millis n = deadline - grpc_core::ExecCtx::Get()->Now();
  if (n < 0) return 0;
  if (n > INT_MAX) return -1;
  return (int)n;
}

/*******************************************************************************
 * pollset_set_posix.c
 */

static grpc_pollset_set* pollset_set_create(void) {
  grpc_pollset_set* pollset_set =
      (grpc_pollset_set*)gpr_zalloc(sizeof(*pollset_set));
  gpr_mu_init(&pollset_set->mu);
  return pollset_set;
}

static void pollset_set_destroy(grpc_pollset_set* pollset_set) {
  size_t i;
  gpr_mu_destroy(&pollset_set->mu);
  for (i = 0; i < pollset_set->fd_count; i++) {
    GRPC_FD_UNREF(pollset_set->fds[i], "pollset_set");
  }
  for (i = 0; i < pollset_set->pollset_count; i++) {
    grpc_pollset* pollset = pollset_set->pollsets[i];
    gpr_mu_lock(&pollset->mu);
    pollset->pollset_set_count--;
    /* check shutdown */
    if (pollset->shutting_down && !pollset->called_shutdown &&
        !pollset_has_observers(pollset)) {
      pollset->called_shutdown = 1;
      gpr_mu_unlock(&pollset->mu);
      finish_shutdown(pollset);
    } else {
      gpr_mu_unlock(&pollset->mu);
    }
  }
  gpr_free(pollset_set->pollsets);
  gpr_free(pollset_set->pollset_sets);
  gpr_free(pollset_set->fds);
  gpr_free(pollset_set);
}

static void pollset_set_add_pollset(grpc_pollset_set* pollset_set,
                                    grpc_pollset* pollset) {
  size_t i, j;
  gpr_mu_lock(&pollset->mu);
  pollset->pollset_set_count++;
  gpr_mu_unlock(&pollset->mu);
  gpr_mu_lock(&pollset_set->mu);
  if (pollset_set->pollset_count == pollset_set->pollset_capacity) {
    pollset_set->pollset_capacity =
        GPR_MAX(8, 2 * pollset_set->pollset_capacity);
    pollset_set->pollsets = (grpc_pollset**)gpr_realloc(
        pollset_set->pollsets,
        pollset_set->pollset_capacity * sizeof(*pollset_set->pollsets));
  }
  pollset_set->pollsets[pollset_set->pollset_count++] = pollset;
  for (i = 0, j = 0; i < pollset_set->fd_count; i++) {
    if (fd_is_orphaned(pollset_set->fds[i])) {
      GRPC_FD_UNREF(pollset_set->fds[i], "pollset_set");
    } else {
      pollset_add_fd(pollset, pollset_set->fds[i]);
      pollset_set->fds[j++] = pollset_set->fds[i];
    }
  }
  pollset_set->fd_count = j;
  gpr_mu_unlock(&pollset_set->mu);
}

static void pollset_set_del_pollset(grpc_pollset_set* pollset_set,
                                    grpc_pollset* pollset) {
  size_t i;
  gpr_mu_lock(&pollset_set->mu);
  for (i = 0; i < pollset_set->pollset_count; i++) {
    if (pollset_set->pollsets[i] == pollset) {
      pollset_set->pollset_count--;
      GPR_SWAP(grpc_pollset*, pollset_set->pollsets[i],
               pollset_set->pollsets[pollset_set->pollset_count]);
      break;
    }
  }
  gpr_mu_unlock(&pollset_set->mu);
  gpr_mu_lock(&pollset->mu);
  pollset->pollset_set_count--;
  /* check shutdown */
  if (pollset->shutting_down && !pollset->called_shutdown &&
      !pollset_has_observers(pollset)) {
    pollset->called_shutdown = 1;
    gpr_mu_unlock(&pollset->mu);
    finish_shutdown(pollset);
  } else {
    gpr_mu_unlock(&pollset->mu);
  }
}

static void pollset_set_add_pollset_set(grpc_pollset_set* bag,
                                        grpc_pollset_set* item) {
  size_t i, j;
  gpr_mu_lock(&bag->mu);
  if (bag->pollset_set_count == bag->pollset_set_capacity) {
    bag->pollset_set_capacity = GPR_MAX(8, 2 * bag->pollset_set_capacity);
    bag->pollset_sets = (grpc_pollset_set**)gpr_realloc(
        bag->pollset_sets,
        bag->pollset_set_capacity * sizeof(*bag->pollset_sets));
  }
  bag->pollset_sets[bag->pollset_set_count++] = item;
  for (i = 0, j = 0; i < bag->fd_count; i++) {
    if (fd_is_orphaned(bag->fds[i])) {
      GRPC_FD_UNREF(bag->fds[i], "pollset_set");
    } else {
      pollset_set_add_fd(item, bag->fds[i]);
      bag->fds[j++] = bag->fds[i];
    }
  }
  bag->fd_count = j;
  gpr_mu_unlock(&bag->mu);
}

static void pollset_set_del_pollset_set(grpc_pollset_set* bag,
                                        grpc_pollset_set* item) {
  size_t i;
  gpr_mu_lock(&bag->mu);
  for (i = 0; i < bag->pollset_set_count; i++) {
    if (bag->pollset_sets[i] == item) {
      bag->pollset_set_count--;
      GPR_SWAP(grpc_pollset_set*, bag->pollset_sets[i],
               bag->pollset_sets[bag->pollset_set_count]);
      break;
    }
  }
  gpr_mu_unlock(&bag->mu);
}

static void pollset_set_add_fd(grpc_pollset_set* pollset_set, grpc_fd* fd) {
  size_t i;
  gpr_mu_lock(&pollset_set->mu);
  if (pollset_set->fd_count == pollset_set->fd_capacity) {
    pollset_set->fd_capacity = GPR_MAX(8, 2 * pollset_set->fd_capacity);
    pollset_set->fds = (grpc_fd**)gpr_realloc(
        pollset_set->fds, pollset_set->fd_capacity * sizeof(*pollset_set->fds));
  }
  GRPC_FD_REF(fd, "pollset_set");
  pollset_set->fds[pollset_set->fd_count++] = fd;
  for (i = 0; i < pollset_set->pollset_count; i++) {
    pollset_add_fd(pollset_set->pollsets[i], fd);
  }
  for (i = 0; i < pollset_set->pollset_set_count; i++) {
    pollset_set_add_fd(pollset_set->pollset_sets[i], fd);
  }
  gpr_mu_unlock(&pollset_set->mu);
}

static void pollset_set_del_fd(grpc_pollset_set* pollset_set, grpc_fd* fd) {
  size_t i;
  gpr_mu_lock(&pollset_set->mu);
  for (i = 0; i < pollset_set->fd_count; i++) {
    if (pollset_set->fds[i] == fd) {
      pollset_set->fd_count--;
      GPR_SWAP(grpc_fd*, pollset_set->fds[i],
               pollset_set->fds[pollset_set->fd_count]);
      GRPC_FD_UNREF(fd, "pollset_set");
      break;
    }
  }
  for (i = 0; i < pollset_set->pollset_set_count; i++) {
    pollset_set_del_fd(pollset_set->pollset_sets[i], fd);
  }
  gpr_mu_unlock(&pollset_set->mu);
}

/*******************************************************************************
 * Condition Variable polling extensions
 */

static void run_poll(void* args);
static void cache_poller_locked(poll_args* args);

static void cache_insert_locked(poll_args* args) {
  uint32_t key = gpr_murmur_hash3(args->fds, args->nfds * sizeof(struct pollfd),
                                  0xDEADBEEF);
  key = key % poll_cache.size;
  if (poll_cache.active_pollers[key]) {
    poll_cache.active_pollers[key]->prev = args;
  }
  args->next = poll_cache.active_pollers[key];
  args->prev = nullptr;
  poll_cache.active_pollers[key] = args;
  poll_cache.count++;
}

static void init_result(poll_args* pargs) {
  pargs->result = (poll_result*)gpr_malloc(sizeof(poll_result));
  gpr_ref_init(&pargs->result->refcount, 1);
  pargs->result->watchers = nullptr;
  pargs->result->watchcount = 0;
  pargs->result->fds =
      (struct pollfd*)gpr_malloc(sizeof(struct pollfd) * pargs->nfds);
  memcpy(pargs->result->fds, pargs->fds, sizeof(struct pollfd) * pargs->nfds);
  pargs->result->nfds = pargs->nfds;
  pargs->result->retval = 0;
  pargs->result->err = 0;
  pargs->result->completed = 0;
}

// Creates a poll_args object for a given arguments to poll().
// This object may return a poll_args in the cache.
static poll_args* get_poller_locked(struct pollfd* fds, nfds_t count) {
  uint32_t key =
      gpr_murmur_hash3(fds, count * sizeof(struct pollfd), 0xDEADBEEF);
  key = key % poll_cache.size;
  poll_args* curr = poll_cache.active_pollers[key];
  while (curr) {
    if (curr->nfds == count &&
        memcmp(curr->fds, fds, count * sizeof(struct pollfd)) == 0) {
      gpr_free(fds);
      return curr;
    }
    curr = curr->next;
  }

  if (poll_cache.free_pollers) {
    poll_args* pargs = poll_cache.free_pollers;
    poll_cache.free_pollers = pargs->next;
    if (poll_cache.free_pollers) {
      poll_cache.free_pollers->prev = nullptr;
    }
    pargs->fds = fds;
    pargs->nfds = count;
    pargs->next = nullptr;
    pargs->prev = nullptr;
    init_result(pargs);
    cache_poller_locked(pargs);
    return pargs;
  }

  poll_args* pargs = (poll_args*)gpr_malloc(sizeof(struct poll_args));
  gpr_cv_init(&pargs->trigger);
  pargs->fds = fds;
  pargs->nfds = count;
  pargs->next = nullptr;
  pargs->prev = nullptr;
  pargs->trigger_set = 0;
  init_result(pargs);
  cache_poller_locked(pargs);
  gpr_thd_id t_id;
  gpr_thd_options opt = gpr_thd_options_default();
  gpr_ref(&g_cvfds.pollcount);
  gpr_thd_options_set_detached(&opt);
  GPR_ASSERT(gpr_thd_new(&t_id, "grpc_poller", &run_poll, pargs, &opt));
  return pargs;
}

static void cache_delete_locked(poll_args* args) {
  if (!args->prev) {
    uint32_t key = gpr_murmur_hash3(
        args->fds, args->nfds * sizeof(struct pollfd), 0xDEADBEEF);
    key = key % poll_cache.size;
    GPR_ASSERT(poll_cache.active_pollers[key] == args);
    poll_cache.active_pollers[key] = args->next;
  } else {
    args->prev->next = args->next;
  }

  if (args->next) {
    args->next->prev = args->prev;
  }

  poll_cache.count--;
  if (poll_cache.free_pollers) {
    poll_cache.free_pollers->prev = args;
  }
  args->prev = nullptr;
  args->next = poll_cache.free_pollers;
  gpr_free(args->fds);
  poll_cache.free_pollers = args;
}

static void cache_poller_locked(poll_args* args) {
  if (poll_cache.count + 1 > poll_cache.size / 2) {
    poll_args** old_active_pollers = poll_cache.active_pollers;
    poll_cache.size = poll_cache.size * 2;
    poll_cache.count = 0;
    poll_cache.active_pollers =
        (poll_args**)gpr_malloc(sizeof(void*) * poll_cache.size);
    for (unsigned int i = 0; i < poll_cache.size; i++) {
      poll_cache.active_pollers[i] = nullptr;
    }
    for (unsigned int i = 0; i < poll_cache.size / 2; i++) {
      poll_args* curr = old_active_pollers[i];
      poll_args* next = nullptr;
      while (curr) {
        next = curr->next;
        cache_insert_locked(curr);
        curr = next;
      }
    }
    gpr_free(old_active_pollers);
  }

  cache_insert_locked(args);
}

static void cache_destroy_locked(poll_args* args) {
  if (args->next) {
    args->next->prev = args->prev;
  }

  if (args->prev) {
    args->prev->next = args->next;
  } else {
    poll_cache.free_pollers = args->next;
  }

  gpr_free(args);
}

static void decref_poll_result(poll_result* res) {
  if (gpr_unref(&res->refcount)) {
    GPR_ASSERT(!res->watchers);
    gpr_free(res->fds);
    gpr_free(res);
  }
}

void remove_cvn(grpc_cv_node** head, grpc_cv_node* target) {
  if (target->next) {
    target->next->prev = target->prev;
  }

  if (target->prev) {
    target->prev->next = target->next;
  } else {
    *head = target->next;
  }
}

gpr_timespec thread_grace;

// Poll in a background thread
static void run_poll(void* args) {
  poll_args* pargs = (poll_args*)args;
  while (1) {
    poll_result* result = pargs->result;
    int retval = g_cvfds.poll(result->fds, result->nfds, CV_POLL_PERIOD_MS);
    gpr_mu_lock(&g_cvfds.mu);
    if (retval != 0) {
      result->completed = 1;
      result->retval = retval;
      result->err = errno;
      grpc_cv_node* watcher = result->watchers;
      while (watcher) {
        gpr_cv_signal(watcher->cv);
        watcher = watcher->next;
      }
    }
    if (result->watchcount == 0 || result->completed) {
      cache_delete_locked(pargs);
      decref_poll_result(result);
      // Leave this polling thread alive for a grace period to do another poll()
      // op
      gpr_timespec deadline = gpr_now(GPR_CLOCK_MONOTONIC);
      deadline = gpr_time_add(deadline, thread_grace);
      pargs->trigger_set = 0;
      gpr_cv_wait(&pargs->trigger, &g_cvfds.mu, deadline);
      if (!pargs->trigger_set) {
        cache_destroy_locked(pargs);
        break;
      }
    }
    gpr_mu_unlock(&g_cvfds.mu);
  }

  // We still have the lock here
  if (gpr_unref(&g_cvfds.pollcount)) {
    gpr_cv_signal(&g_cvfds.shutdown_cv);
  }
  gpr_mu_unlock(&g_cvfds.mu);
}

// This function overrides poll() to handle condition variable wakeup fds
static int cvfd_poll(struct pollfd* fds, nfds_t nfds, int timeout) {
  unsigned int i;
  int res, idx;
  grpc_cv_node* pollcv;
  int skip_poll = 0;
  nfds_t nsockfds = 0;
  poll_result* result = nullptr;
  gpr_mu_lock(&g_cvfds.mu);
  pollcv = (grpc_cv_node*)gpr_malloc(sizeof(grpc_cv_node));
  pollcv->next = nullptr;
  gpr_cv pollcv_cv;
  gpr_cv_init(&pollcv_cv);
  pollcv->cv = &pollcv_cv;
  grpc_cv_node* fd_cvs = (grpc_cv_node*)gpr_malloc(nfds * sizeof(grpc_cv_node));

  for (i = 0; i < nfds; i++) {
    fds[i].revents = 0;
    if (fds[i].fd < 0 && (fds[i].events & POLLIN)) {
      idx = GRPC_FD_TO_IDX(fds[i].fd);
      fd_cvs[i].cv = &pollcv_cv;
      fd_cvs[i].prev = nullptr;
      fd_cvs[i].next = g_cvfds.cvfds[idx].cvs;
      if (g_cvfds.cvfds[idx].cvs) {
        g_cvfds.cvfds[idx].cvs->prev = &(fd_cvs[i]);
      }
      g_cvfds.cvfds[idx].cvs = &(fd_cvs[i]);
      // Don't bother polling if a wakeup fd is ready
      if (g_cvfds.cvfds[idx].is_set) {
        skip_poll = 1;
      }
    } else if (fds[i].fd >= 0) {
      nsockfds++;
    }
  }

  gpr_timespec deadline = gpr_now(GPR_CLOCK_MONOTONIC);
  if (timeout < 0) {
    deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  } else {
    deadline =
        gpr_time_add(deadline, gpr_time_from_millis(timeout, GPR_TIMESPAN));
  }

  res = 0;
  if (!skip_poll && nsockfds > 0) {
    struct pollfd* pollfds =
        (struct pollfd*)gpr_malloc(sizeof(struct pollfd) * nsockfds);
    idx = 0;
    for (i = 0; i < nfds; i++) {
      if (fds[i].fd >= 0) {
        pollfds[idx].fd = fds[i].fd;
        pollfds[idx].events = fds[i].events;
        pollfds[idx].revents = 0;
        idx++;
      }
    }
    poll_args* pargs = get_poller_locked(pollfds, nsockfds);
    result = pargs->result;
    pollcv->next = result->watchers;
    pollcv->prev = nullptr;
    if (result->watchers) {
      result->watchers->prev = pollcv;
    }
    result->watchers = pollcv;
    result->watchcount++;
    gpr_ref(&result->refcount);

    pargs->trigger_set = 1;
    gpr_cv_signal(&pargs->trigger);
    gpr_cv_wait(&pollcv_cv, &g_cvfds.mu, deadline);
    res = result->retval;
    errno = result->err;
    result->watchcount--;
    remove_cvn(&result->watchers, pollcv);
  } else if (!skip_poll) {
    gpr_cv_wait(&pollcv_cv, &g_cvfds.mu, deadline);
  }

  idx = 0;
  for (i = 0; i < nfds; i++) {
    if (fds[i].fd < 0 && (fds[i].events & POLLIN)) {
      remove_cvn(&g_cvfds.cvfds[GRPC_FD_TO_IDX(fds[i].fd)].cvs, &(fd_cvs[i]));
      if (g_cvfds.cvfds[GRPC_FD_TO_IDX(fds[i].fd)].is_set) {
        fds[i].revents = POLLIN;
        if (res >= 0) res++;
      }
    } else if (!skip_poll && fds[i].fd >= 0 && result->completed) {
      fds[i].revents = result->fds[idx].revents;
      idx++;
    }
  }

  gpr_free(fd_cvs);
  gpr_free(pollcv);
  if (result) {
    decref_poll_result(result);
  }

  gpr_mu_unlock(&g_cvfds.mu);

  return res;
}

static void global_cv_fd_table_init() {
  gpr_mu_init(&g_cvfds.mu);
  gpr_mu_lock(&g_cvfds.mu);
  gpr_cv_init(&g_cvfds.shutdown_cv);
  gpr_ref_init(&g_cvfds.pollcount, 1);
  g_cvfds.size = CV_DEFAULT_TABLE_SIZE;
  g_cvfds.cvfds =
      (grpc_fd_node*)gpr_malloc(sizeof(grpc_fd_node) * CV_DEFAULT_TABLE_SIZE);
  g_cvfds.free_fds = nullptr;
  thread_grace = gpr_time_from_millis(POLLCV_THREAD_GRACE_MS, GPR_TIMESPAN);
  for (int i = 0; i < CV_DEFAULT_TABLE_SIZE; i++) {
    g_cvfds.cvfds[i].is_set = 0;
    g_cvfds.cvfds[i].cvs = nullptr;
    g_cvfds.cvfds[i].next_free = g_cvfds.free_fds;
    g_cvfds.free_fds = &g_cvfds.cvfds[i];
  }
  // Override the poll function with one that supports cvfds
  g_cvfds.poll = grpc_poll_function;
  grpc_poll_function = &cvfd_poll;

  // Initialize the cache
  poll_cache.size = 32;
  poll_cache.count = 0;
  poll_cache.free_pollers = nullptr;
  poll_cache.active_pollers = (poll_args**)gpr_malloc(sizeof(void*) * 32);
  for (unsigned int i = 0; i < poll_cache.size; i++) {
    poll_cache.active_pollers[i] = nullptr;
  }

  gpr_mu_unlock(&g_cvfds.mu);
}

static void global_cv_fd_table_shutdown() {
  gpr_mu_lock(&g_cvfds.mu);
  // Attempt to wait for all abandoned poll() threads to terminate
  // Not doing so will result in reported memory leaks
  if (!gpr_unref(&g_cvfds.pollcount)) {
    int res = gpr_cv_wait(&g_cvfds.shutdown_cv, &g_cvfds.mu,
                          gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                       gpr_time_from_seconds(3, GPR_TIMESPAN)));
    GPR_ASSERT(res == 0);
  }
  gpr_cv_destroy(&g_cvfds.shutdown_cv);
  grpc_poll_function = g_cvfds.poll;
  gpr_free(g_cvfds.cvfds);

  gpr_free(poll_cache.active_pollers);

  gpr_mu_unlock(&g_cvfds.mu);
  gpr_mu_destroy(&g_cvfds.mu);
}

/*******************************************************************************
 * event engine binding
 */

static void shutdown_engine(void) {
  pollset_global_shutdown();
  if (grpc_cv_wakeup_fds_enabled()) {
    global_cv_fd_table_shutdown();
  }
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

const grpc_event_engine_vtable* grpc_init_poll_posix(bool explicit_request) {
  if (!grpc_has_wakeup_fd()) {
    gpr_log(GPR_ERROR, "Skipping poll because of no wakeup fd.");
    return nullptr;
  }
  if (!GRPC_LOG_IF_ERROR("pollset_global_init", pollset_global_init())) {
    return nullptr;
  }
  return &vtable;
}

const grpc_event_engine_vtable* grpc_init_poll_cv_posix(bool explicit_request) {
  global_cv_fd_table_init();
  grpc_enable_cv_wakeup_fds(1);
  if (!GRPC_LOG_IF_ERROR("pollset_global_init", pollset_global_init())) {
    global_cv_fd_table_shutdown();
    grpc_enable_cv_wakeup_fds(0);
    return nullptr;
  }
  return &vtable;
}

#endif
