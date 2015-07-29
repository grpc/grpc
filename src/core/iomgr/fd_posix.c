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

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SOCKET

#include "src/core/iomgr/fd_posix.h"

#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

enum descriptor_state {
  NOT_READY = 0,
  READY = 1
}; /* or a pointer to a closure to call */

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
    gpr_mu_init(&r->set_state_mu);
    gpr_mu_init(&r->watcher_mu);
  }

  gpr_atm_rel_store(&r->refst, 1);
  gpr_atm_rel_store(&r->readst, NOT_READY);
  gpr_atm_rel_store(&r->writest, NOT_READY);
  gpr_atm_rel_store(&r->shutdown, 0);
  r->fd = fd;
  r->inactive_watcher_root.next = r->inactive_watcher_root.prev =
      &r->inactive_watcher_root;
  r->freelist_next = NULL;
  r->read_watcher = r->write_watcher = NULL;
  r->on_done_closure = NULL;
  return r;
}

static void destroy(grpc_fd *fd) {
  gpr_mu_destroy(&fd->set_state_mu);
  gpr_mu_destroy(&fd->watcher_mu);
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

void grpc_fd_global_init(void) { gpr_mu_init(&fd_freelist_mu); }

void grpc_fd_global_shutdown(void) {
  while (fd_freelist != NULL) {
    grpc_fd *fd = fd_freelist;
    fd_freelist = fd_freelist->freelist_next;
    destroy(fd);
  }
  gpr_mu_destroy(&fd_freelist_mu);
}

grpc_fd *grpc_fd_create(int fd, const char *name) {
  grpc_fd *r = alloc_fd(fd);
  grpc_iomgr_register_object(&r->iomgr_object, name);
  return r;
}

int grpc_fd_is_orphaned(grpc_fd *fd) {
  return (gpr_atm_acq_load(&fd->refst) & 1) == 0;
}

static void pollset_kick_locked(grpc_pollset *pollset) {
  gpr_mu_lock(GRPC_POLLSET_MU(pollset));
  grpc_pollset_kick(pollset, NULL);
  gpr_mu_unlock(GRPC_POLLSET_MU(pollset));
}

static void maybe_wake_one_watcher_locked(grpc_fd *fd) {
  if (fd->inactive_watcher_root.next != &fd->inactive_watcher_root) {
    pollset_kick_locked(fd->inactive_watcher_root.next->pollset);
  } else if (fd->read_watcher) {
    pollset_kick_locked(fd->read_watcher->pollset);
  } else if (fd->write_watcher) {
    pollset_kick_locked(fd->write_watcher->pollset);
  }
}

static void maybe_wake_one_watcher(grpc_fd *fd) {
  gpr_mu_lock(&fd->watcher_mu);
  maybe_wake_one_watcher_locked(fd);
  gpr_mu_unlock(&fd->watcher_mu);
}

static void wake_all_watchers_locked(grpc_fd *fd) {
  grpc_fd_watcher *watcher;
  for (watcher = fd->inactive_watcher_root.next;
       watcher != &fd->inactive_watcher_root; watcher = watcher->next) {
    pollset_kick_locked(watcher->pollset);
  }
  if (fd->read_watcher) {
    pollset_kick_locked(fd->read_watcher->pollset);
  }
  if (fd->write_watcher && fd->write_watcher != fd->read_watcher) {
    pollset_kick_locked(fd->write_watcher->pollset);
  }
}

static int has_watchers(grpc_fd *fd) {
  return fd->read_watcher != NULL || fd->write_watcher != NULL ||
         fd->inactive_watcher_root.next != &fd->inactive_watcher_root;
}

void grpc_fd_orphan(grpc_fd *fd, grpc_iomgr_closure *on_done,
                    const char *reason) {
  fd->on_done_closure = on_done;
  shutdown(fd->fd, SHUT_RDWR);
  REF_BY(fd, 1, reason); /* remove active status, but keep referenced */
  gpr_mu_lock(&fd->watcher_mu);
  if (!has_watchers(fd)) {
    close(fd->fd);
    if (fd->on_done_closure) {
      grpc_iomgr_add_callback(fd->on_done_closure);
    }
  } else {
    wake_all_watchers_locked(fd);
  }
  gpr_mu_unlock(&fd->watcher_mu);
  UNREF_BY(fd, 2, reason); /* drop the reference */
}

/* increment refcount by two to avoid changing the orphan bit */
#ifdef GRPC_FD_REF_COUNT_DEBUG
void grpc_fd_ref(grpc_fd *fd, const char *reason, const char *file, int line) {
  ref_by(fd, 2, reason, file, line);
}

void grpc_fd_unref(grpc_fd *fd, const char *reason, const char *file,
                   int line) {
  unref_by(fd, 2, reason, file, line);
}
#else
void grpc_fd_ref(grpc_fd *fd) { ref_by(fd, 2); }

void grpc_fd_unref(grpc_fd *fd) { unref_by(fd, 2); }
#endif

static void process_callback(grpc_iomgr_closure *closure, int success,
                             int allow_synchronous_callback) {
  if (allow_synchronous_callback) {
    closure->cb(closure->cb_arg, success);
  } else {
    grpc_iomgr_add_delayed_callback(closure, success);
  }
}

static void process_callbacks(grpc_iomgr_closure *callbacks, size_t n,
                              int success, int allow_synchronous_callback) {
  size_t i;
  for (i = 0; i < n; i++) {
    process_callback(callbacks + i, success, allow_synchronous_callback);
  }
}

static void notify_on(grpc_fd *fd, gpr_atm *st, grpc_iomgr_closure *closure,
                      int allow_synchronous_callback) {
  switch (gpr_atm_acq_load(st)) {
    case NOT_READY:
      /* There is no race if the descriptor is already ready, so we skip
         the interlocked op in that case.  As long as the app doesn't
         try to set the same upcall twice (which it shouldn't) then
         oldval should never be anything other than READY or NOT_READY.  We
         don't
         check for user error on the fast path. */
      if (gpr_atm_rel_cas(st, NOT_READY, (gpr_intptr)closure)) {
        /* swap was successful -- the closure will run after the next
           set_ready call.  NOTE: we don't have an ABA problem here,
           since we should never have concurrent calls to the same
           notify_on function. */
        maybe_wake_one_watcher(fd);
        return;
      }
    /* swap was unsuccessful due to an intervening set_ready call.
       Fall through to the READY code below */
    case READY:
      GPR_ASSERT(gpr_atm_no_barrier_load(st) == READY);
      gpr_atm_rel_store(st, NOT_READY);
      process_callback(closure, !gpr_atm_acq_load(&fd->shutdown),
                       allow_synchronous_callback);
      return;
    default: /* WAITING */
      /* upcallptr was set to a different closure.  This is an error! */
      gpr_log(GPR_ERROR,
              "User called a notify_on function with a previous callback still "
              "pending");
      abort();
  }
  gpr_log(GPR_ERROR, "Corrupt memory in &st->state");
  abort();
}

static void set_ready_locked(gpr_atm *st, grpc_iomgr_closure **callbacks,
                             size_t *ncallbacks) {
  gpr_intptr state = gpr_atm_acq_load(st);

  switch (state) {
    case READY:
      /* duplicate ready, ignore */
      return;
    case NOT_READY:
      if (gpr_atm_rel_cas(st, NOT_READY, READY)) {
        /* swap was successful -- the closure will run after the next
           notify_on call. */
        return;
      }
      /* swap was unsuccessful due to an intervening set_ready call.
         Fall through to the WAITING code below */
      state = gpr_atm_acq_load(st);
    default: /* waiting */
      GPR_ASSERT(gpr_atm_no_barrier_load(st) != READY &&
                 gpr_atm_no_barrier_load(st) != NOT_READY);
      callbacks[(*ncallbacks)++] = (grpc_iomgr_closure *)state;
      gpr_atm_rel_store(st, NOT_READY);
      return;
  }
}

static void set_ready(grpc_fd *fd, gpr_atm *st,
                      int allow_synchronous_callback) {
  /* only one set_ready can be active at once (but there may be a racing
     notify_on) */
  int success;
  grpc_iomgr_closure *closure;
  size_t ncb = 0;

  gpr_mu_lock(&fd->set_state_mu);
  set_ready_locked(st, &closure, &ncb);
  gpr_mu_unlock(&fd->set_state_mu);
  success = !gpr_atm_acq_load(&fd->shutdown);
  GPR_ASSERT(ncb <= 1);
  if (ncb > 0) {
    process_callbacks(closure, ncb, success, allow_synchronous_callback);
  }
}

void grpc_fd_shutdown(grpc_fd *fd) {
  size_t ncb = 0;
  gpr_mu_lock(&fd->set_state_mu);
  GPR_ASSERT(!gpr_atm_no_barrier_load(&fd->shutdown));
  gpr_atm_rel_store(&fd->shutdown, 1);
  set_ready_locked(&fd->readst, &fd->shutdown_closures[0], &ncb);
  set_ready_locked(&fd->writest, &fd->shutdown_closures[0], &ncb);
  gpr_mu_unlock(&fd->set_state_mu);
  GPR_ASSERT(ncb <= 2);
  process_callbacks(fd->shutdown_closures[0], ncb, 0 /* GPR_FALSE */,
                    0 /* GPR_FALSE */);
}

void grpc_fd_notify_on_read(grpc_fd *fd, grpc_iomgr_closure *closure) {
  notify_on(fd, &fd->readst, closure, 0);
}

void grpc_fd_notify_on_write(grpc_fd *fd, grpc_iomgr_closure *closure) {
  notify_on(fd, &fd->writest, closure, 0);
}

gpr_uint32 grpc_fd_begin_poll(grpc_fd *fd, grpc_pollset *pollset,
                              gpr_uint32 read_mask, gpr_uint32 write_mask,
                              grpc_fd_watcher *watcher) {
  gpr_uint32 mask = 0;
  /* keep track of pollers that have requested our events, in case they change
   */
  GRPC_FD_REF(fd, "poll");

  gpr_mu_lock(&fd->watcher_mu);
  /* if we are shutdown, then don't add to the watcher set */
  if (gpr_atm_no_barrier_load(&fd->shutdown)) {
    watcher->fd = NULL;
    watcher->pollset = NULL;
    gpr_mu_unlock(&fd->watcher_mu);
    GRPC_FD_UNREF(fd, "poll");
    return 0;
  }
  /* if there is nobody polling for read, but we need to, then start doing so */
  if (read_mask && !fd->read_watcher && gpr_atm_acq_load(&fd->readst) > READY) {
    fd->read_watcher = watcher;
    mask |= read_mask;
  }
  /* if there is nobody polling for write, but we need to, then start doing so
   */
  if (write_mask && !fd->write_watcher && gpr_atm_acq_load(&fd->writest) > READY) {
    fd->write_watcher = watcher;
    mask |= write_mask;
  }
  /* if not polling, remember this watcher in case we need someone to later */
  if (mask == 0) {
    watcher->next = &fd->inactive_watcher_root;
    watcher->prev = watcher->next->prev;
    watcher->next->prev = watcher->prev->next = watcher;
  }
  watcher->pollset = pollset;
  watcher->fd = fd;
  gpr_mu_unlock(&fd->watcher_mu);

  return mask;
}

void grpc_fd_end_poll(grpc_fd_watcher *watcher, int got_read, int got_write) {
  int was_polling = 0;
  int kick = 0;
  grpc_fd *fd = watcher->fd;

  if (fd == NULL) {
    return;
  }

  gpr_mu_lock(&fd->watcher_mu);
  if (watcher == fd->read_watcher) {
    /* remove read watcher, kick if we still need a read */
    was_polling = 1;
    kick = kick || !got_read;
    fd->read_watcher = NULL;
  }
  if (watcher == fd->write_watcher) {
    /* remove write watcher, kick if we still need a write */
    was_polling = 1;
    kick = kick || !got_write;
    fd->write_watcher = NULL;
  }
  if (!was_polling) {
    /* remove from inactive list */
    watcher->next->prev = watcher->prev;
    watcher->prev->next = watcher->next;
  }
  if (kick) {
    maybe_wake_one_watcher_locked(fd);
  }
  if (grpc_fd_is_orphaned(fd) && !has_watchers(fd)) {
    close(fd->fd);
    if (fd->on_done_closure != NULL) {
      grpc_iomgr_add_callback(fd->on_done_closure);
    }
  }
  gpr_mu_unlock(&fd->watcher_mu);

  GRPC_FD_UNREF(fd, "poll");
}

void grpc_fd_become_readable(grpc_fd *fd, int allow_synchronous_callback) {
  set_ready(fd, &fd->readst, allow_synchronous_callback);
}

void grpc_fd_become_writable(grpc_fd *fd, int allow_synchronous_callback) {
  set_ready(fd, &fd->writest, allow_synchronous_callback);
}

#endif
