/*
 *
 * Copyright 2015 gRPC authors.
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

#ifdef GRPC_WINSOCK_SOCKET

#include <grpc/support/log.h>
#include <grpc/support/thd.h>

#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_windows.h"

#define GRPC_POLLSET_KICK_BROADCAST ((grpc_pollset_worker *)1)

#ifndef NDEBUG
grpc_tracer_flag grpc_trace_fd_refcount =
    GRPC_TRACER_INITIALIZER(false, "fd_refcount");
#endif

gpr_mu grpc_polling_mu;
static grpc_pollset_worker *g_active_poller;
static grpc_pollset_worker g_global_root_worker;

void grpc_pollset_global_init(void) {
  gpr_mu_init(&grpc_polling_mu);
  g_active_poller = NULL;
  g_global_root_worker.links[GRPC_POLLSET_WORKER_LINK_GLOBAL].next =
      g_global_root_worker.links[GRPC_POLLSET_WORKER_LINK_GLOBAL].prev =
          &g_global_root_worker;
}

void grpc_pollset_global_shutdown(void) { gpr_mu_destroy(&grpc_polling_mu); }

static void remove_worker(grpc_pollset_worker *worker,
                          grpc_pollset_worker_link_type type) {
  worker->links[type].prev->links[type].next = worker->links[type].next;
  worker->links[type].next->links[type].prev = worker->links[type].prev;
  worker->links[type].next = worker->links[type].prev = worker;
}

static int has_workers(grpc_pollset_worker *root,
                       grpc_pollset_worker_link_type type) {
  return root->links[type].next != root;
}

static grpc_pollset_worker *pop_front_worker(
    grpc_pollset_worker *root, grpc_pollset_worker_link_type type) {
  if (has_workers(root, type)) {
    grpc_pollset_worker *w = root->links[type].next;
    remove_worker(w, type);
    return w;
  } else {
    return NULL;
  }
}

static void push_front_worker(grpc_pollset_worker *root,
                              grpc_pollset_worker_link_type type,
                              grpc_pollset_worker *worker) {
  worker->links[type].prev = root;
  worker->links[type].next = worker->links[type].prev->links[type].next;
  worker->links[type].prev->links[type].next =
      worker->links[type].next->links[type].prev = worker;
}

size_t grpc_pollset_size(void) { return sizeof(grpc_pollset); }

/* There isn't really any such thing as a pollset under Windows, due to the
   nature of the IO completion ports. We're still going to provide a minimal
   set of features for the sake of the rest of grpc. But grpc_pollset_work
   won't actually do any polling, and return as quickly as possible. */

void grpc_pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  *mu = &grpc_polling_mu;
  pollset->root_worker.links[GRPC_POLLSET_WORKER_LINK_POLLSET].next =
      pollset->root_worker.links[GRPC_POLLSET_WORKER_LINK_POLLSET].prev =
          &pollset->root_worker;
}

void grpc_pollset_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_closure *closure) {
  pollset->shutting_down = 1;
  grpc_pollset_kick(pollset, GRPC_POLLSET_KICK_BROADCAST);
  if (!pollset->is_iocp_worker) {
    GRPC_CLOSURE_SCHED(exec_ctx, closure, GRPC_ERROR_NONE);
  } else {
    pollset->on_shutdown = closure;
  }
}

void grpc_pollset_destroy(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset) {}

grpc_error *grpc_pollset_work(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                              grpc_pollset_worker **worker_hdl,
                              gpr_timespec now, gpr_timespec deadline) {
  grpc_pollset_worker worker;
  if (worker_hdl) *worker_hdl = &worker;

  int added_worker = 0;
  worker.links[GRPC_POLLSET_WORKER_LINK_POLLSET].next =
      worker.links[GRPC_POLLSET_WORKER_LINK_POLLSET].prev =
          worker.links[GRPC_POLLSET_WORKER_LINK_GLOBAL].next =
              worker.links[GRPC_POLLSET_WORKER_LINK_GLOBAL].prev = NULL;
  worker.kicked = 0;
  worker.pollset = pollset;
  gpr_cv_init(&worker.cv);
  if (!pollset->kicked_without_pollers && !pollset->shutting_down) {
    if (g_active_poller == NULL) {
      grpc_pollset_worker *next_worker;
      /* become poller */
      pollset->is_iocp_worker = 1;
      g_active_poller = &worker;
      gpr_mu_unlock(&grpc_polling_mu);
      grpc_iocp_work(exec_ctx, deadline);
      grpc_exec_ctx_flush(exec_ctx);
      gpr_mu_lock(&grpc_polling_mu);
      pollset->is_iocp_worker = 0;
      g_active_poller = NULL;
      /* try to get a worker from this pollsets worker list */
      next_worker = pop_front_worker(&pollset->root_worker,
                                     GRPC_POLLSET_WORKER_LINK_POLLSET);
      if (next_worker == NULL) {
        /* try to get a worker from the global list */
        next_worker = pop_front_worker(&g_global_root_worker,
                                       GRPC_POLLSET_WORKER_LINK_GLOBAL);
      }
      if (next_worker != NULL) {
        next_worker->kicked = 1;
        gpr_cv_signal(&next_worker->cv);
      }

      if (pollset->shutting_down && pollset->on_shutdown != NULL) {
        GRPC_CLOSURE_SCHED(exec_ctx, pollset->on_shutdown, GRPC_ERROR_NONE);
        pollset->on_shutdown = NULL;
      }
      goto done;
    }
    push_front_worker(&g_global_root_worker, GRPC_POLLSET_WORKER_LINK_GLOBAL,
                      &worker);
    push_front_worker(&pollset->root_worker, GRPC_POLLSET_WORKER_LINK_POLLSET,
                      &worker);
    added_worker = 1;
    while (!worker.kicked) {
      if (gpr_cv_wait(&worker.cv, &grpc_polling_mu, deadline)) {
        break;
      }
    }
  } else {
    pollset->kicked_without_pollers = 0;
  }
done:
  if (!grpc_closure_list_empty(exec_ctx->closure_list)) {
    gpr_mu_unlock(&grpc_polling_mu);
    grpc_exec_ctx_flush(exec_ctx);
    gpr_mu_lock(&grpc_polling_mu);
  }
  if (added_worker) {
    remove_worker(&worker, GRPC_POLLSET_WORKER_LINK_GLOBAL);
    remove_worker(&worker, GRPC_POLLSET_WORKER_LINK_POLLSET);
  }
  gpr_cv_destroy(&worker.cv);
  if (worker_hdl) *worker_hdl = NULL;
  return GRPC_ERROR_NONE;
}

grpc_error *grpc_pollset_kick(grpc_pollset *p,
                              grpc_pollset_worker *specific_worker) {
  if (specific_worker != NULL) {
    if (specific_worker == GRPC_POLLSET_KICK_BROADCAST) {
      for (specific_worker =
               p->root_worker.links[GRPC_POLLSET_WORKER_LINK_POLLSET].next;
           specific_worker != &p->root_worker;
           specific_worker =
               specific_worker->links[GRPC_POLLSET_WORKER_LINK_POLLSET].next) {
        specific_worker->kicked = 1;
        gpr_cv_signal(&specific_worker->cv);
      }
      p->kicked_without_pollers = 1;
      if (p->is_iocp_worker) {
        grpc_iocp_kick();
      }
    } else {
      if (p->is_iocp_worker && g_active_poller == specific_worker) {
        grpc_iocp_kick();
      } else {
        specific_worker->kicked = 1;
        gpr_cv_signal(&specific_worker->cv);
      }
    }
  } else {
    specific_worker =
        pop_front_worker(&p->root_worker, GRPC_POLLSET_WORKER_LINK_POLLSET);
    if (specific_worker != NULL) {
      grpc_pollset_kick(p, specific_worker);
    } else if (p->is_iocp_worker) {
      grpc_iocp_kick();
    } else {
      p->kicked_without_pollers = 1;
    }
  }
  return GRPC_ERROR_NONE;
}

#endif /* GRPC_WINSOCK_SOCKET */
