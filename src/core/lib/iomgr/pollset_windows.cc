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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_windows.h"

#define GRPC_POLLSET_KICK_BROADCAST ((grpc_pollset_worker*)1)

grpc_core::DebugOnlyTraceFlag grpc_trace_fd_refcount(false, "fd_refcount");

gpr_mu grpc_polling_mu;
static grpc_pollset_worker* g_active_poller;
static grpc_pollset_worker g_global_root_worker;

static void pollset_global_init(void) {
  gpr_mu_init(&grpc_polling_mu);
  g_active_poller = NULL;
  g_global_root_worker.links[GRPC_POLLSET_WORKER_LINK_GLOBAL].next =
      g_global_root_worker.links[GRPC_POLLSET_WORKER_LINK_GLOBAL].prev =
          &g_global_root_worker;
}

static void pollset_global_shutdown(void) { gpr_mu_destroy(&grpc_polling_mu); }

static void remove_worker(grpc_pollset_worker* worker,
                          grpc_pollset_worker_link_type type) {
  worker->links[type].prev->links[type].next = worker->links[type].next;
  worker->links[type].next->links[type].prev = worker->links[type].prev;
  worker->links[type].next = worker->links[type].prev = worker;
}

static int has_workers(grpc_pollset_worker* root,
                       grpc_pollset_worker_link_type type) {
  return root->links[type].next != root;
}

static grpc_pollset_worker* pop_front_worker(
    grpc_pollset_worker* root, grpc_pollset_worker_link_type type) {
  if (has_workers(root, type)) {
    grpc_pollset_worker* w = root->links[type].next;
    remove_worker(w, type);
    return w;
  } else {
    return NULL;
  }
}

static void push_front_worker(grpc_pollset_worker* root,
                              grpc_pollset_worker_link_type type,
                              grpc_pollset_worker* worker) {
  worker->links[type].prev = root;
  worker->links[type].next = worker->links[type].prev->links[type].next;
  worker->links[type].prev->links[type].next =
      worker->links[type].next->links[type].prev = worker;
}

static size_t pollset_size(void) { return sizeof(grpc_pollset); }

/* There isn't really any such thing as a pollset under Windows, due to the
   nature of the IO completion ports. We're still going to provide a minimal
   set of features for the sake of the rest of grpc. But grpc_pollset_work
   won't actually do any polling, and return as quickly as possible. */

static void pollset_init(grpc_pollset* pollset, gpr_mu** mu) {
  *mu = &grpc_polling_mu;
  pollset->root_worker.links[GRPC_POLLSET_WORKER_LINK_POLLSET].next =
      pollset->root_worker.links[GRPC_POLLSET_WORKER_LINK_POLLSET].prev =
          &pollset->root_worker;
}

static void pollset_shutdown(grpc_pollset* pollset, grpc_closure* closure) {
  pollset->shutting_down = 1;
  grpc_pollset_kick(pollset, GRPC_POLLSET_KICK_BROADCAST);
  if (!pollset->is_iocp_worker) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, absl::OkStatus());
  } else {
    pollset->on_shutdown = closure;
  }
}

static void pollset_destroy(grpc_pollset* pollset) {}

static grpc_error_handle pollset_work(grpc_pollset* pollset,
                                      grpc_pollset_worker** worker_hdl,
                                      grpc_core::Timestamp deadline) {
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
      grpc_pollset_worker* next_worker;
      /* become poller */
      pollset->is_iocp_worker = 1;
      g_active_poller = &worker;
      gpr_mu_unlock(&grpc_polling_mu);
      grpc_iocp_work(deadline);
      grpc_core::ExecCtx::Get()->Flush();
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
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, pollset->on_shutdown,
                                absl::OkStatus());
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
      if (gpr_cv_wait(&worker.cv, &grpc_polling_mu,
                      deadline.as_timespec(GPR_CLOCK_REALTIME))) {
        grpc_core::ExecCtx::Get()->InvalidateNow();
        break;
      }
      grpc_core::ExecCtx::Get()->InvalidateNow();
    }
  } else {
    pollset->kicked_without_pollers = 0;
  }
done:
  if (!grpc_closure_list_empty(*grpc_core::ExecCtx::Get()->closure_list())) {
    gpr_mu_unlock(&grpc_polling_mu);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(&grpc_polling_mu);
  }
  if (added_worker) {
    remove_worker(&worker, GRPC_POLLSET_WORKER_LINK_GLOBAL);
    remove_worker(&worker, GRPC_POLLSET_WORKER_LINK_POLLSET);
  }
  gpr_cv_destroy(&worker.cv);
  if (worker_hdl) *worker_hdl = NULL;
  return absl::OkStatus();
}

static grpc_error_handle pollset_kick(grpc_pollset* p,
                                      grpc_pollset_worker* specific_worker) {
  bool should_kick_global = false;
  if (specific_worker != NULL) {
    if (specific_worker == GRPC_POLLSET_KICK_BROADCAST) {
      should_kick_global = true;
      for (specific_worker =
               p->root_worker.links[GRPC_POLLSET_WORKER_LINK_POLLSET].next;
           specific_worker != &p->root_worker;
           specific_worker =
               specific_worker->links[GRPC_POLLSET_WORKER_LINK_POLLSET].next) {
        specific_worker->kicked = 1;
        should_kick_global = false;
        gpr_cv_signal(&specific_worker->cv);
      }
      p->kicked_without_pollers = 1;
      if (p->is_iocp_worker) {
        grpc_iocp_kick();
        should_kick_global = false;
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
      should_kick_global = true;
    }
  }
  if (should_kick_global && g_active_poller == NULL) {
    grpc_pollset_worker* next_global_worker = pop_front_worker(
        &g_global_root_worker, GRPC_POLLSET_WORKER_LINK_GLOBAL);
    if (next_global_worker != NULL) {
      next_global_worker->kicked = 1;
      gpr_cv_signal(&next_global_worker->cv);
    }
  }
  return absl::OkStatus();
}

grpc_pollset_vtable grpc_windows_pollset_vtable = {
    pollset_global_init, pollset_global_shutdown,
    pollset_init,        pollset_shutdown,
    pollset_destroy,     pollset_work,
    pollset_kick,        pollset_size};

#endif /* GRPC_WINSOCK_SOCKET */
