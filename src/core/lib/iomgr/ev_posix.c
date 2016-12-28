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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/ev_posix.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/ev_epoll_linux.h"
#include "src/core/lib/iomgr/ev_poll_posix.h"
#include "src/core/lib/support/env.h"

/** Default poll() function - a pointer so that it can be overridden by some
 *  tests */
grpc_poll_function_type grpc_poll_function = poll;

static const grpc_event_engine_vtable *g_event_engine;
static const char *g_poll_strategy_name = NULL;

typedef const grpc_event_engine_vtable *(*event_engine_factory_fn)(void);

typedef struct {
  const char *name;
  event_engine_factory_fn factory;
} event_engine_factory;

static const event_engine_factory g_factories[] = {
    {"epoll", grpc_init_epoll_linux},
    {"poll", grpc_init_poll_posix},
    {"poll-cv", grpc_init_poll_cv_posix},
};

static void add(const char *beg, const char *end, char ***ss, size_t *ns) {
  size_t n = *ns;
  size_t np = n + 1;
  char *s;
  size_t len;
  GPR_ASSERT(end >= beg);
  len = (size_t)(end - beg);
  s = gpr_malloc(len + 1);
  memcpy(s, beg, len);
  s[len] = 0;
  *ss = gpr_realloc(*ss, sizeof(char **) * np);
  (*ss)[n] = s;
  *ns = np;
}

static void split(const char *s, char ***ss, size_t *ns) {
  const char *c = strchr(s, ',');
  if (c == NULL) {
    add(s, s + strlen(s), ss, ns);
  } else {
    add(s, c, ss, ns);
    split(c + 1, ss, ns);
  }
}

static bool is(const char *want, const char *have) {
  return 0 == strcmp(want, "all") || 0 == strcmp(want, have);
}

static void try_engine(const char *engine) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(g_factories); i++) {
    if (is(engine, g_factories[i].name)) {
      if ((g_event_engine = g_factories[i].factory())) {
        g_poll_strategy_name = g_factories[i].name;
        gpr_log(GPR_DEBUG, "Using polling engine: %s", g_factories[i].name);
        return;
      }
    }
  }
}

/* Call this only after calling grpc_event_engine_init() */
const char *grpc_get_poll_strategy_name() { return g_poll_strategy_name; }

void grpc_event_engine_init(void) {
  char *s = gpr_getenv("GRPC_POLL_STRATEGY");
  if (s == NULL) {
    s = gpr_strdup("all");
  }

  char **strings = NULL;
  size_t nstrings = 0;
  split(s, &strings, &nstrings);

  for (size_t i = 0; g_event_engine == NULL && i < nstrings; i++) {
    try_engine(strings[i]);
  }

  for (size_t i = 0; i < nstrings; i++) {
    gpr_free(strings[i]);
  }
  gpr_free(strings);
  gpr_free(s);

  if (g_event_engine == NULL) {
    gpr_log(GPR_ERROR, "No event engine could be initialized");
    abort();
  }
}

void grpc_event_engine_shutdown(void) {
  g_event_engine->shutdown_engine();
  g_event_engine = NULL;
}

grpc_fd *grpc_fd_create(int fd, const char *name) {
  return g_event_engine->fd_create(fd, name);
}

grpc_workqueue *grpc_fd_get_workqueue(grpc_fd *fd) {
  return g_event_engine->fd_get_workqueue(fd);
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

bool grpc_fd_is_shutdown(grpc_fd *fd) {
  return g_event_engine->fd_is_shutdown(fd);
}

void grpc_fd_notify_on_read(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                            grpc_closure *closure) {
  g_event_engine->fd_notify_on_read(exec_ctx, fd, closure);
}

void grpc_fd_notify_on_write(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                             grpc_closure *closure) {
  g_event_engine->fd_notify_on_write(exec_ctx, fd, closure);
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

grpc_error *grpc_pollset_work(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                              grpc_pollset_worker **worker, gpr_timespec now,
                              gpr_timespec deadline) {
  return g_event_engine->pollset_work(exec_ctx, pollset, worker, now, deadline);
}

grpc_error *grpc_pollset_kick(grpc_pollset *pollset,
                              grpc_pollset_worker *specific_worker) {
  return g_event_engine->pollset_kick(pollset, specific_worker);
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

grpc_error *grpc_kick_poller(void) { return g_event_engine->kick_poller(); }

#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
grpc_workqueue *grpc_workqueue_ref(grpc_workqueue *workqueue, const char *file,
                                   int line, const char *reason) {
  return g_event_engine->workqueue_ref(workqueue, file, line, reason);
}
void grpc_workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue,
                          const char *file, int line, const char *reason) {
  g_event_engine->workqueue_unref(exec_ctx, workqueue, file, line, reason);
}
#else
grpc_workqueue *grpc_workqueue_ref(grpc_workqueue *workqueue) {
  return g_event_engine->workqueue_ref(workqueue);
}
void grpc_workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue) {
  g_event_engine->workqueue_unref(exec_ctx, workqueue);
}
#endif

grpc_closure_scheduler *grpc_workqueue_scheduler(grpc_workqueue *workqueue) {
  return g_event_engine->workqueue_scheduler(workqueue);
}

#endif  // GRPC_POSIX_SOCKET
