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

#include "src/core/iomgr/ev_epoll_linux.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions

struct grpc_fd {
  int fd;
  grpc_iomgr_object iomgr_object;
  grpc_fd *next_free;
};

struct grpc_pollset_worker {
};

struct grpc_pollset {
};

////////////////////////////////////////////////////////////////////////////////
// FD implementation

static gpr_mu g_fd_freelist_mu;
static grpc_fd *g_next_free;

static grpc_fd *fd_create(int fd, const char *name) {
  grpc_fd *r = NULL;
  gpr_mu_lock(&g_fd_freelist_mu);
  if (g_next_free == NULL) {
    r = gpr_malloc(sizeof(*r));
  } else {
    r = g_next_free;
    g_next_free = r->next;
  }
  gpr_mu_unlock(&g_fd_freelist_mu);

  r->fd = fd;
  grpc_iomgr_register_object(&r->iomgr_object, name);
  r->next = NULL;
  return r;
}

static int fd_wrapped_fd(grpc_fd *fd) {
  return fd->fd;
}

static void fd_orphan(grpc_exec_ctx *exec_ctx, grpc_fd *fd, grpc_closure *on_done,
                  int *release_fd, const char *reason) {
  grpc_exec_ctx_enqueue(exec_ctx, on_done, true);
}

////////////////////////////////////////////////////////////////////////////////
// Pollset implementation

////////////////////////////////////////////////////////////////////////////////
// Engine binding

static void shutdown_engine(void) { pollset_global_shutdown(); }

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
