/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include "src/core/lib/iomgr/ev_epoll_linux.h"

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>

#include "src/core/iomgr/iomgr_internal.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions

#define STATE_NOT_READY ((gpr_atm)0)
#define STATE_READY ((gpr_atm)1)

typedef enum { POLLABLE_FD, POLLABLE_EPOLL_SET } pollable_type;

typedef struct {
  pollable_type type;
  int fd;
  grpc_iomgr_object iomgr_object;
} pollable_object;

typedef struct polling_island {
  pollable_object pollable;
  gpr_refcount refs;
  gpr_mu mu;
  grpc_fd *only_fd;
  struct polling_island *became;
  struct polling_island *next;
} polling_island;

struct grpc_fd {
  pollable_object pollable;

  // each event atomic is a tri state:
  //   STATE_NOT_READY - no event received, nobody waiting for it either
  //   STATE_READY - event received, nobody waiting for it
  //   closure pointer - no event received, upper layer is waiting for it
  gpr_atm on_readable;
  gpr_atm on_writable;

  // mutex protecting polling_island
  gpr_mu polling_island_mu;
  // current polling island
  polling_island *polling_island;

  grpc_fd *next_free;
};

struct grpc_pollset_worker {};

struct grpc_pollset {
  gpr_mu mu;
  // current polling island
  polling_island *polling_island;
};

////////////////////////////////////////////////////////////////////////////////
// Polling island implementation

static gpr_mu g_pi_freelist_mu;
static polling_island *g_first_free_pi;

static void add_pollable_to_epoll_set(pollable_object *pollable, int epoll_set,
                                      uint32_t events) {
  struct epoll_event ev;
  ev.events = events;
  ev.data.ptr = pollable;
  int err = epoll_ctl(epoll_set, EPOLL_CTL_ADD, pollable->fd, &ev);
  if (err < 0) {
    gpr_log(GPR_ERROR, "epoll_ctl add for %d faild: %s", pollable->fd,
            strerror(errno));
  }
}

static void add_fd_to_epoll_set(grpc_fd *fd, int epoll_set) {
  add_pollable_to_epoll_set(&fd->pollable, epoll_set,
                            EPOLLIN | EPOLLOUT | EPOLLET);
}

static polling_island *polling_island_create(grpc_fd *initial_fd) {
  polling_island *r = NULL;
  gpr_mu_lock(&g_pi_freelist_mu);
  if (g_first_free_pi == NULL) {
    r = gpr_malloc(sizeof(*r));
    r->pollable.type = POLLABLE_EPOLL_SET;
    gpr_mu_init(&r->mu);
    gpr_ref_init(&r->refs, 0);
  } else {
    r = g_first_free_pi;
    g_first_free_pi = r->next_free;
  }
  gpr_mu_unlock(&g_pi_freelist_mu);

  r->pollable.fd = epoll_create1(EPOLL_CLOEXEC);
  r->only_fd = initial_fd;
  GPR_ASSERT(r->pollable.fd >= 0);

  add_fd_to_epoll_set(initial_fd, r->pollable.fd);
  return r;
}

static polling_island *polling_island_add(polling_island *p, grpc_fd *fd) {
  gpr_ref(&p->refs);

  gpr_mu_lock(&p->mu);
  p->only_fd = NULL;
  gpr_mu_unlock(&p->mu);

  add_fd_to_epoll_set(fd, p->pollable.fd);
}

static void add_siblings_to(polling_island *siblings, polling_island *dest) {
  polling_island *sibling_tail = dest;
  while (sibling_tail->next != NULL) {
    sibling_tail = sibling_tail->next;
  }
  sibling_tail->next = siblings;
}

static polling_island *polling_island_merge(polling_island *a,
                                            polling_island *b) {
  GPR_ASSERT(a != b);
  polling_island *out;

  gpr_mu_lock(&GPR_MIN(a, b)->mu);
  gpr_mu_lock(&GPR_MAX(a, b)->mu);

  GPR_ASSERT(a->became == NULL);
  GPR_ASSERT(b->became == NULL);

  if (a->only_fd == NULL && b->only_fd == NULL) {
    b->became = a;
    add_siblings_to(b, a);
    add_island_to_epoll_set(b, a->pollable.fd);
    out = a;
  } else if (a->only_fd == NULL) {
    GPR_ASSERT(b->only_fd != NULL);
    add_fd_to_epoll_set(b->only_fd, a->pollable.fd);
    b->became = a;
    out = a;
  } else if (b->only_fd == NULL) {
    GPR_ASSERT(a->only_fd != NULL);
    add_fd_to_epoll_set(a->only_fd, b->pollable.fd);
    a->became = b;
    out = b;
  } else {
    add_fd_to_epoll_set(b->only_fd, a->pollable.fd);
    a->only_fd = NULL;
    b->only_fd = NULL;
    b->became = a;
    out = a;
  }

  gpr_mu_unlock(&a->mu);
  gpr_mu_unlock(&b->mu);

  return out;
}

static void polling_island_drop(polling_island *p) {}

static void polling_island_update(polling_island **p) {
  polling_island *cur = *p;

  if (cur != NULL) {
    bool done;
    do {
      done = true;
      gpr_mu_lock(&cur->mu);
      if (cur->became != NULL) {
        polling_island *next = cur->became;
        gpr_mu_unlock(&cur->mu);
        cur = next;
        done = false;
      } else {
        gpr_mu_unlock(&cur->mu);
      }
    } while (!done);
  }

  *p = cur;
}

////////////////////////////////////////////////////////////////////////////////
// FD implementation

static gpr_mu g_fd_freelist_mu;
static grpc_fd *g_first_free_fd;

static grpc_fd *fd_create(int fd, const char *name) {
  grpc_fd *r = NULL;
  gpr_mu_lock(&g_fd_freelist_mu);
  if (g_first_free_fd == NULL) {
    r = gpr_malloc(sizeof(*r));
    r->pollable.type = POLLABLE_FD;
    gpr_atm_rel_store(&r->on_readable, 0);
    gpr_atm_rel_store(&r->on_writable, 0);
    gpr_mu_init(&r->polling_island_mu);
  } else {
    r = g_first_free_fd;
    g_first_free_fd = r->next_free;
  }
  gpr_mu_unlock(&g_fd_freelist_mu);

  r->pollable.fd = fd;
  grpc_iomgr_register_object(&r->pollable.iomgr_object, name);
  r->next_free = NULL;
  return r;
}

static int fd_wrapped_fd(grpc_fd *fd) { return fd->pollable.fd; }

static void fd_orphan(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                      grpc_closure *on_done, int *release_fd,
                      const char *reason) {
  if (release_fd != NULL) {
    *release_fd = fd->pollable.fd;
  } else {
    close(fd->pollable.fd);
  }

  gpr_mu_lock(&fd->polling_island_mu);
  if (fd->polling_island != NULL) {
    polling_island_drop(fd->polling_island);
  }
  gpr_mu_unlock(&fd->polling_island_mu);

  gpr_mu_lock(&g_fd_freelist_mu);
  fd->next_free = g_first_free_fd;
  g_first_free_fd = fd;
  grpc_iomgr_unregister_object(&fd->pollable.iomgr_object);
  gpr_mu_unlock(&g_fd_freelist_mu);

  grpc_exec_ctx_enqueue(exec_ctx, on_done, true, NULL);
}

static void notify_on(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                      grpc_closure *closure, gpr_atm *state) {
  if (gpr_atm_acq_cas(state, STATE_NOT_READY, (gpr_atm)closure)) {
    // state was not ready, and is now the closure - we're done */
  } else {
    // cas failed - we MUST be in STATE_READY (can't request two notifications
    // for the same event)
    // flip back to not ready, enqueue the closure directly
    GPR_ASSERT(gpr_atm_rel_cas(state, STATE_READY, STATE_NOT_READY));
    grpc_exec_ctx_enqueue(exec_ctx, closure, true, NULL);
  }
}

static void fd_notify_on_read(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                              grpc_closure *closure) {
  notify_on(exec_ctx, fd, closure, &fd->on_readable);
}

static void fd_notify_on_write(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                               grpc_closure *closure) {
  notify_on(exec_ctx, fd, closure, &fd->on_readable);
}

static void destroy_fd_freelist(void) {
  while (g_first_free_fd) {
    grpc_fd *next = g_first_free_fd->next_free;
    gpr_mu_destroy(&g_first_free_fd->polling_island_mu);
    gpr_free(next);
    g_first_free_fd = next;
  }
}

////////////////////////////////////////////////////////////////////////////////
// Pollset implementation

static void pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  gpr_mu_init(&pollset->mu);
  *mu = &pollset->mu;
  pollset->polling_island = NULL;
}

static void pollset_destroy(grpc_pollset *pollset) {
  gpr_mu_destroy(&pollset->mu);
  if (pollset->polling_island) {
    polling_island_drop(pollset->polling_island);
  }
}

static void pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           struct grpc_fd *fd) {
  gpr_mu_lock(&pollset->mu);
  gpr_mu_lock(&fd->polling_island_mu);

  if (fd->polling_island == NULL) {
    if (pollset->polling_island == NULL) {
      fd->polling_island = pollset->polling_island = polling_island_create(fd);
    } else {
      fd->polling_island =
          polling_island_add(pollset->polling_island, fd->pollable.fd);
    }
  } else if (pollset->polling_island == NULL) {
    pollset->polling_island = polling_island_ref(fd->polling_island);
  } else if (pollset->polling_island != fd->polling_island) {
    pollset->polling_island = fd->polling_island =
        polling_island_merge(pollset->polling_island, fd->polling_island);
  }

  GPR_ASSERT(pollset->polling_island == fd->polling_island);

  gpr_mu_unlock(&fd->polling_island_mu);
  gpr_mu_unlock(&pollset->mu);
}

////////////////////////////////////////////////////////////////////////////////
// Engine binding

static void shutdown_engine(void) { destroy_fd_freelist(); }

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

static bool is_epoll_available(void) {
  abort();
  return false;
}

const grpc_event_engine_vtable *grpc_init_poll_posix(void) {
  if (!is_epoll_available()) {
    return NULL;
  }
  return &vtable;
}
