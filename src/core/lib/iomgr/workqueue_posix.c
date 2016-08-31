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

#include "src/core/lib/iomgr/workqueue.h"

#include <stdio.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/profiling/timers.h"

static void on_readable(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error);

grpc_error *grpc_workqueue_create(grpc_exec_ctx *exec_ctx,
                                  grpc_workqueue **workqueue) {
  char name[32];
  *workqueue = gpr_malloc(sizeof(grpc_workqueue));
  gpr_ref_init(&(*workqueue)->refs, 1);
  gpr_atm_no_barrier_store(&(*workqueue)->state, 1);
  grpc_error *err = grpc_wakeup_fd_init(&(*workqueue)->wakeup_fd);
  if (err != GRPC_ERROR_NONE) {
    gpr_free(*workqueue);
    return err;
  }
  sprintf(name, "workqueue:%p", (void *)(*workqueue));
  (*workqueue)->wakeup_read_fd = grpc_fd_create(
      GRPC_WAKEUP_FD_GET_READ_FD(&(*workqueue)->wakeup_fd), name);
  gpr_mpscq_init(&(*workqueue)->queue);
  grpc_closure_init(&(*workqueue)->read_closure, on_readable, *workqueue);
  grpc_fd_notify_on_read(exec_ctx, (*workqueue)->wakeup_read_fd,
                         &(*workqueue)->read_closure);
  return GRPC_ERROR_NONE;
}

static void workqueue_destroy(grpc_exec_ctx *exec_ctx,
                              grpc_workqueue *workqueue) {
  grpc_fd_shutdown(exec_ctx, workqueue->wakeup_read_fd);
}

static void workqueue_orphan(grpc_exec_ctx *exec_ctx,
                             grpc_workqueue *workqueue) {
  if (gpr_atm_full_fetch_add(&workqueue->state, -1) == 1) {
    workqueue_destroy(exec_ctx, workqueue);
  }
}

#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
void grpc_workqueue_ref(grpc_workqueue *workqueue, const char *file, int line,
                        const char *reason) {
  if (workqueue == NULL) return;
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "WORKQUEUE:%p   ref %d -> %d %s",
          workqueue, (int)workqueue->refs.count, (int)workqueue->refs.count + 1,
          reason);
  gpr_ref(&workqueue->refs);
}
#else
void grpc_workqueue_ref(grpc_workqueue *workqueue) {
  if (workqueue == NULL) return;
  gpr_ref(&workqueue->refs);
}
#endif

#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
void grpc_workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue,
                          const char *file, int line, const char *reason) {
  if (workqueue == NULL) return;
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "WORKQUEUE:%p unref %d -> %d %s",
          workqueue, (int)workqueue->refs.count, (int)workqueue->refs.count - 1,
          reason);
  if (gpr_unref(&workqueue->refs)) {
    workqueue_orphan(exec_ctx, workqueue);
  }
}
#else
void grpc_workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue) {
  if (workqueue == NULL) return;
  if (gpr_unref(&workqueue->refs)) {
    workqueue_orphan(exec_ctx, workqueue);
  }
}
#endif

static void drain(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue) {
  abort();
}

static void wakeup(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue) {
  GPR_TIMER_MARK("workqueue.wakeup", 0);
  grpc_error *err = grpc_wakeup_fd_wakeup(&workqueue->wakeup_fd);
  if (!GRPC_LOG_IF_ERROR("wakeupfd_wakeup", err)) {
    drain(exec_ctx, workqueue);
  }
}

static void on_readable(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  GPR_TIMER_BEGIN("workqueue.on_readable", 0);

  grpc_workqueue *workqueue = arg;

  if (error != GRPC_ERROR_NONE) {
    /* HACK: let wakeup_fd code know that we stole the fd */
    workqueue->wakeup_fd.read_fd = 0;
    grpc_wakeup_fd_destroy(&workqueue->wakeup_fd);
    grpc_fd_orphan(exec_ctx, workqueue->wakeup_read_fd, NULL, NULL, "destroy");
    GPR_ASSERT(gpr_atm_no_barrier_load(&workqueue->state) == 0);
    gpr_free(workqueue);
  } else {
    error = grpc_wakeup_fd_consume_wakeup(&workqueue->wakeup_fd);
    gpr_mpscq_node *n = gpr_mpscq_pop(&workqueue->queue);
    if (error == GRPC_ERROR_NONE) {
      grpc_fd_notify_on_read(exec_ctx, workqueue->wakeup_read_fd,
                             &workqueue->read_closure);
    } else {
      /* recurse to get error handling */
      on_readable(exec_ctx, arg, error);
    }
    if (n == NULL) {
      /* try again - queue in an inconsistant state */
      wakeup(exec_ctx, workqueue);
    } else {
      switch (gpr_atm_full_fetch_add(&workqueue->state, -2)) {
        case 3:  // had one count, one unorphaned --> done, unorphaned
          break;
        case 2:  // had one count, one orphaned --> done, orphaned
          workqueue_destroy(exec_ctx, workqueue);
          break;
        case 1:
        case 0:
          // these values are illegal - representing an already done or
          // deleted workqueue
          GPR_UNREACHABLE_CODE(break);
        default:
          // schedule a wakeup since there's more to do
          wakeup(exec_ctx, workqueue);
      }
      grpc_closure *cl = (grpc_closure *)n;
      grpc_error *clerr = cl->error;
      cl->cb(exec_ctx, cl->cb_arg, clerr);
      GRPC_ERROR_UNREF(clerr);
    }
  }

  GPR_TIMER_END("workqueue.on_readable", 0);
}

void grpc_workqueue_enqueue(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue,
                            grpc_closure *closure, grpc_error *error) {
  GPR_TIMER_BEGIN("workqueue.enqueue", 0);
  gpr_atm last = gpr_atm_full_fetch_add(&workqueue->state, 2);
  GPR_ASSERT(last & 1);
  closure->error = error;
  gpr_mpscq_push(&workqueue->queue, &closure->next_data.atm_next);
  if (last == 1) {
    wakeup(exec_ctx, workqueue);
  }
  GPR_TIMER_END("workqueue.enqueue", 0);
}

#endif /* GPR_POSIX_SOCKET */
