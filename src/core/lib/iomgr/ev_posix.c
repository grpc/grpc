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

#include "src/core/lib/iomgr/ev_posix.h"

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/ev_poll_and_epoll_posix.h"

static const grpc_event_engine_vtable *g_event_engine;

grpc_poll_function_type grpc_poll_function = poll;

void grpc_event_engine_init(void) {
  if ((g_event_engine = grpc_init_poll_and_epoll_posix())) {
    return;
  }
  gpr_log(GPR_ERROR, "No event engine could be initialized");
  abort();
}

void grpc_event_engine_shutdown(void) { g_event_engine->shutdown_engine(); }

grpc_fd *grpc_fd_create(int fd, const char *name) {
  return g_event_engine->fd_create(fd, name);
}

int grpc_fd_wrapped_fd(grpc_fd *fd) {
  return g_event_engine->fd_wrapped_fd(fd);
}

void grpc_fd_orphan(grpc_exec_ctx *exec_ctx, grpc_fd *fd, grpc_closure *on_done,
                    int *release_fd, const char *reason) {
  g_event_engine->fd_orphan(exec_ctx, fd, on_done, release_fd, reason);
}

void grpc_fd_shutdown(grpc_exec_ctx *exec_ctx, grpc_fd *fd) {
  g_event_engine->fd_shutdown(exec_ctx, fd);
}

void grpc_fd_notify_on_read(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                            grpc_closure *closure) {
  g_event_engine->fd_notify_on_read(exec_ctx, fd, closure);
}

void grpc_fd_notify_on_write(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                             grpc_closure *closure) {
  g_event_engine->fd_notify_on_write(exec_ctx, fd, closure);
}

grpc_pollset *grpc_fd_get_read_notifier_pollset(grpc_exec_ctx *exec_ctx,
                                                grpc_fd *fd) {
  return g_event_engine->fd_get_read_notifier_pollset(exec_ctx, fd);
}

size_t grpc_pollset_size(void) { return g_event_engine->pollset_size; }

void grpc_pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  g_event_engine->pollset_init(pollset, mu);
}

void grpc_pollset_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_closure *closure) {
  g_event_engine->pollset_shutdown(exec_ctx, pollset, closure);
}

void grpc_pollset_reset(grpc_pollset *pollset) {
  g_event_engine->pollset_reset(pollset);
}

void grpc_pollset_destroy(grpc_pollset *pollset) {
  g_event_engine->pollset_destroy(pollset);
}

void grpc_pollset_work(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                       grpc_pollset_worker **worker, gpr_timespec now,
                       gpr_timespec deadline) {
  g_event_engine->pollset_work(exec_ctx, pollset, worker, now, deadline);
}

void grpc_pollset_kick(grpc_pollset *pollset,
                       grpc_pollset_worker *specific_worker) {
  g_event_engine->pollset_kick(pollset, specific_worker);
}

void grpc_pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                         struct grpc_fd *fd) {
  g_event_engine->pollset_add_fd(exec_ctx, pollset, fd);
}

grpc_pollset_set *grpc_pollset_set_create(void) {
  return g_event_engine->pollset_set_create();
}

void grpc_pollset_set_destroy(grpc_pollset_set *pollset_set) {
  g_event_engine->pollset_set_destroy(pollset_set);
}

void grpc_pollset_set_add_pollset(grpc_exec_ctx *exec_ctx,
                                  grpc_pollset_set *pollset_set,
                                  grpc_pollset *pollset) {
  g_event_engine->pollset_set_add_pollset(exec_ctx, pollset_set, pollset);
}

void grpc_pollset_set_del_pollset(grpc_exec_ctx *exec_ctx,
                                  grpc_pollset_set *pollset_set,
                                  grpc_pollset *pollset) {
  g_event_engine->pollset_set_del_pollset(exec_ctx, pollset_set, pollset);
}

void grpc_pollset_set_add_pollset_set(grpc_exec_ctx *exec_ctx,
                                      grpc_pollset_set *bag,
                                      grpc_pollset_set *item) {
  g_event_engine->pollset_set_add_pollset_set(exec_ctx, bag, item);
}

void grpc_pollset_set_del_pollset_set(grpc_exec_ctx *exec_ctx,
                                      grpc_pollset_set *bag,
                                      grpc_pollset_set *item) {
  g_event_engine->pollset_set_del_pollset_set(exec_ctx, bag, item);
}

void grpc_pollset_set_add_fd(grpc_exec_ctx *exec_ctx,
                             grpc_pollset_set *pollset_set, grpc_fd *fd) {
  g_event_engine->pollset_set_add_fd(exec_ctx, pollset_set, fd);
}

void grpc_pollset_set_del_fd(grpc_exec_ctx *exec_ctx,
                             grpc_pollset_set *pollset_set, grpc_fd *fd) {
  g_event_engine->pollset_set_del_fd(exec_ctx, pollset_set, fd);
}

void grpc_kick_poller(void) { g_event_engine->kick_poller(); }

#endif  // GPR_POSIX_SOCKET
