/*
 *
 * Copyright 2014, Google Inc.
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
#include <unistd.h>

#include "src/core/iomgr/iomgr_internal.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

enum descriptor_state { NOT_READY, READY, WAITING };

static void destroy(grpc_fd *fd) {
  grpc_iomgr_add_callback(fd->on_done, fd->on_done_user_data);
  gpr_mu_destroy(&fd->set_state_mu);
  gpr_free(fd->watchers);
  gpr_free(fd);
  grpc_iomgr_unref();
}

static void ref_by(grpc_fd *fd, int n) {
  gpr_atm_no_barrier_fetch_add(&fd->refst, n);
}

static void unref_by(grpc_fd *fd, int n) {
  if (gpr_atm_full_fetch_add(&fd->refst, -n) == n) {
    destroy(fd);
  }
}

static void do_nothing(void *ignored, int success) {}

grpc_fd *grpc_fd_create(int fd) {
  grpc_fd *r = gpr_malloc(sizeof(grpc_fd));
  grpc_iomgr_ref();
  gpr_atm_rel_store(&r->refst, 1);
  gpr_atm_rel_store(&r->readst.state, NOT_READY);
  gpr_atm_rel_store(&r->writest.state, NOT_READY);
  gpr_mu_init(&r->set_state_mu);
  gpr_mu_init(&r->watcher_mu);
  gpr_atm_rel_store(&r->shutdown, 0);
  r->fd = fd;
  r->watchers = NULL;
  r->watcher_count = 0;
  r->watcher_capacity = 0;
  grpc_pollset_add_fd(grpc_backup_pollset(), r);
  return r;
}

int grpc_fd_is_orphaned(grpc_fd *fd) {
  return (gpr_atm_acq_load(&fd->refst) & 1) == 0;
}

static void wake_watchers(grpc_fd *fd) {
  size_t i, n;
  gpr_mu_lock(&fd->watcher_mu);
  n = fd->watcher_count;
  for (i = 0; i < n; i++) {
    grpc_pollset_force_kick(fd->watchers[i]);
  }
  gpr_mu_unlock(&fd->watcher_mu);
}

void grpc_fd_orphan(grpc_fd *fd, grpc_iomgr_cb_func on_done, void *user_data) {
  fd->on_done = on_done ? on_done : do_nothing;
  fd->on_done_user_data = user_data;
  ref_by(fd, 1); /* remove active status, but keep referenced */
  wake_watchers(fd);
  close(fd->fd);
  unref_by(fd, 2); /* drop the reference */
}

/* increment refcount by two to avoid changing the orphan bit */
void grpc_fd_ref(grpc_fd *fd) { ref_by(fd, 2); }

void grpc_fd_unref(grpc_fd *fd) { unref_by(fd, 2); }

typedef struct {
  grpc_iomgr_cb_func cb;
  void *arg;
} callback;

static void make_callback(grpc_iomgr_cb_func cb, void *arg, int success,
                          int allow_synchronous_callback) {
  if (allow_synchronous_callback) {
    cb(arg, success);
  } else {
    grpc_iomgr_add_delayed_callback(cb, arg, success);
  }
}

static void make_callbacks(callback *callbacks, size_t n, int success,
                           int allow_synchronous_callback) {
  size_t i;
  for (i = 0; i < n; i++) {
    make_callback(callbacks[i].cb, callbacks[i].arg, success,
                  allow_synchronous_callback);
  }
}

static void notify_on(grpc_fd *fd, grpc_fd_state *st, grpc_iomgr_cb_func cb,
                      void *arg, int allow_synchronous_callback) {
  switch ((enum descriptor_state)gpr_atm_acq_load(&st->state)) {
    case NOT_READY:
      /* There is no race if the descriptor is already ready, so we skip
         the interlocked op in that case.  As long as the app doesn't
         try to set the same upcall twice (which it shouldn't) then
         oldval should never be anything other than READY or NOT_READY.  We
         don't
         check for user error on the fast path. */
      st->cb = cb;
      st->cb_arg = arg;
      if (gpr_atm_rel_cas(&st->state, NOT_READY, WAITING)) {
        /* swap was successful -- the closure will run after the next
           set_ready call.  NOTE: we don't have an ABA problem here,
           since we should never have concurrent calls to the same
           notify_on function. */
        wake_watchers(fd);
        return;
      }
    /* swap was unsuccessful due to an intervening set_ready call.
       Fall through to the READY code below */
    case READY:
      assert(gpr_atm_acq_load(&st->state) == READY);
      gpr_atm_rel_store(&st->state, NOT_READY);
      make_callback(cb, arg, !gpr_atm_acq_load(&fd->shutdown),
                    allow_synchronous_callback);
      return;
    case WAITING:
      /* upcallptr was set to a different closure.  This is an error! */
      gpr_log(GPR_ERROR,
              "User called a notify_on function with a previous callback still "
              "pending");
      abort();
  }
  gpr_log(GPR_ERROR, "Corrupt memory in &st->state");
  abort();
}

static void set_ready_locked(grpc_fd_state *st, callback *callbacks,
                             size_t *ncallbacks) {
  callback *c;

  switch ((enum descriptor_state)gpr_atm_acq_load(&st->state)) {
    case NOT_READY:
      if (gpr_atm_rel_cas(&st->state, NOT_READY, READY)) {
        /* swap was successful -- the closure will run after the next
           notify_on call. */
        return;
      }
    /* swap was unsuccessful due to an intervening set_ready call.
       Fall through to the WAITING code below */
    case WAITING:
      assert(gpr_atm_acq_load(&st->state) == WAITING);
      c = &callbacks[(*ncallbacks)++];
      c->cb = st->cb;
      c->arg = st->cb_arg;
      gpr_atm_rel_store(&st->state, NOT_READY);
      return;
    case READY:
      /* duplicate ready, ignore */
      return;
  }
}

static void set_ready(grpc_fd *fd, grpc_fd_state *st,
                      int allow_synchronous_callback) {
  /* only one set_ready can be active at once (but there may be a racing
     notify_on) */
  int success;
  callback cb;
  size_t ncb = 0;
  gpr_mu_lock(&fd->set_state_mu);
  set_ready_locked(st, &cb, &ncb);
  gpr_mu_unlock(&fd->set_state_mu);
  success = !gpr_atm_acq_load(&fd->shutdown);
  make_callbacks(&cb, ncb, success, allow_synchronous_callback);
}

void grpc_fd_shutdown(grpc_fd *fd) {
  callback cb[2];
  size_t ncb = 0;
  gpr_mu_lock(&fd->set_state_mu);
  GPR_ASSERT(!gpr_atm_acq_load(&fd->shutdown));
  gpr_atm_rel_store(&fd->shutdown, 1);
  set_ready_locked(&fd->readst, cb, &ncb);
  set_ready_locked(&fd->writest, cb, &ncb);
  gpr_mu_unlock(&fd->set_state_mu);
  make_callbacks(cb, ncb, 0, 0);
}

void grpc_fd_notify_on_read(grpc_fd *fd, grpc_iomgr_cb_func read_cb,
                            void *read_cb_arg) {
  notify_on(fd, &fd->readst, read_cb, read_cb_arg, 0);
}

void grpc_fd_notify_on_write(grpc_fd *fd, grpc_iomgr_cb_func write_cb,
                             void *write_cb_arg) {
  notify_on(fd, &fd->writest, write_cb, write_cb_arg, 0);
}

gpr_uint32 grpc_fd_begin_poll(grpc_fd *fd, grpc_pollset *pollset,
                              gpr_uint32 read_mask, gpr_uint32 write_mask) {
  /* keep track of pollers that have requested our events, in case they change
   */
  gpr_mu_lock(&fd->watcher_mu);
  if (fd->watcher_capacity == fd->watcher_count) {
    fd->watcher_capacity =
        GPR_MAX(fd->watcher_capacity + 8, fd->watcher_capacity * 3 / 2);
    fd->watchers = gpr_realloc(fd->watchers,
                               fd->watcher_capacity * sizeof(grpc_pollset *));
  }
  fd->watchers[fd->watcher_count++] = pollset;
  gpr_mu_unlock(&fd->watcher_mu);

  return (gpr_atm_acq_load(&fd->readst.state) != READY ? read_mask : 0) |
         (gpr_atm_acq_load(&fd->writest.state) != READY ? write_mask : 0);
}

void grpc_fd_end_poll(grpc_fd *fd, grpc_pollset *pollset) {
  size_t r, w, n;

  gpr_mu_lock(&fd->watcher_mu);
  n = fd->watcher_count;
  for (r = 0, w = 0; r < n; r++) {
    if (fd->watchers[r] == pollset) {
      fd->watcher_count--;
      continue;
    }
    fd->watchers[w++] = fd->watchers[r];
  }
  gpr_mu_unlock(&fd->watcher_mu);
}

void grpc_fd_become_readable(grpc_fd *fd, int allow_synchronous_callback) {
  set_ready(fd, &fd->readst, allow_synchronous_callback);
}

void grpc_fd_become_writable(grpc_fd *fd, int allow_synchronous_callback) {
  set_ready(fd, &fd->writest, allow_synchronous_callback);
}

#endif
