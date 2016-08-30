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

#include "src/core/lib/iomgr/exec_ctx.h"

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>

#include "src/core/lib/iomgr/workqueue.h"
#include "src/core/lib/profiling/timers.h"

bool grpc_exec_ctx_ready_to_finish(grpc_exec_ctx *exec_ctx) {
  if (!exec_ctx->cached_ready_to_finish) {
    exec_ctx->cached_ready_to_finish = exec_ctx->check_ready_to_finish(
        exec_ctx, exec_ctx->check_ready_to_finish_arg);
  }
  return exec_ctx->cached_ready_to_finish;
}

bool grpc_never_ready_to_finish(grpc_exec_ctx *exec_ctx, void *arg_ignored) {
  return false;
}

bool grpc_always_ready_to_finish(grpc_exec_ctx *exec_ctx, void *arg_ignored) {
  return true;
}

#ifndef GRPC_EXECUTION_CONTEXT_SANITIZER
bool grpc_exec_ctx_flush(grpc_exec_ctx *exec_ctx) {
  bool did_something = 0;
  GPR_TIMER_BEGIN("grpc_exec_ctx_flush", 0);
  while (!grpc_closure_list_empty(exec_ctx->closure_list)) {
    grpc_closure *c = exec_ctx->closure_list.head;
    exec_ctx->closure_list.head = exec_ctx->closure_list.tail = NULL;
    while (c != NULL) {
      grpc_closure *next = c->next_data.next;
      grpc_error *error = c->error;
      did_something = true;
      GPR_TIMER_BEGIN("grpc_exec_ctx_flush.cb", 0);
      c->cb(exec_ctx, c->cb_arg, error);
      GRPC_ERROR_UNREF(error);
      GPR_TIMER_END("grpc_exec_ctx_flush.cb", 0);
      c = next;
    }
  }
  GPR_TIMER_END("grpc_exec_ctx_flush", 0);
  return did_something;
}

void grpc_exec_ctx_finish(grpc_exec_ctx *exec_ctx) {
  exec_ctx->cached_ready_to_finish = true;
  grpc_exec_ctx_flush(exec_ctx);
}

void grpc_exec_ctx_sched(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                         grpc_error *error,
                         grpc_workqueue *offload_target_or_null) {
  if (offload_target_or_null == NULL) {
    grpc_closure_list_append(&exec_ctx->closure_list, closure, error);
  } else {
    grpc_workqueue_enqueue(exec_ctx, offload_target_or_null, closure, error);
    GRPC_WORKQUEUE_UNREF(exec_ctx, offload_target_or_null, "exec_ctx_sched");
  }
}

void grpc_exec_ctx_enqueue_list(grpc_exec_ctx *exec_ctx,
                                grpc_closure_list *list,
                                grpc_workqueue *offload_target_or_null) {
  grpc_closure_list_move(list, &exec_ctx->closure_list);
}

void grpc_exec_ctx_global_init(void) {}
void grpc_exec_ctx_global_shutdown(void) {}
#else
static gpr_mu g_mu;
static gpr_cv g_cv;
static int g_threads = 0;

static void run_closure(void *arg) {
  grpc_closure *closure = arg;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  closure->cb(&exec_ctx, closure->cb_arg, (closure->final_data & 1) != 0);
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_mu_lock(&g_mu);
  if (--g_threads == 0) {
    gpr_cv_signal(&g_cv);
  }
  gpr_mu_unlock(&g_mu);
}

static void start_closure(grpc_closure *closure) {
  gpr_thd_id id;
  gpr_mu_lock(&g_mu);
  g_threads++;
  gpr_mu_unlock(&g_mu);
  gpr_thd_new(&id, run_closure, closure, NULL);
}

bool grpc_exec_ctx_flush(grpc_exec_ctx *exec_ctx) { return false; }

void grpc_exec_ctx_finish(grpc_exec_ctx *exec_ctx) {}

void grpc_exec_ctx_enqueue(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                           bool success,
                           grpc_workqueue *offload_target_or_null) {
  GPR_ASSERT(offload_target_or_null == NULL);
  if (closure == NULL) return;
  closure->final_data = success;
  start_closure(closure);
}

void grpc_exec_ctx_enqueue_list(grpc_exec_ctx *exec_ctx,
                                grpc_closure_list *list,
                                grpc_workqueue *offload_target_or_null) {
  GPR_ASSERT(offload_target_or_null == NULL);
  if (list == NULL) return;
  grpc_closure *p = list->head;
  while (p) {
    grpc_closure *start = p;
    p = grpc_closure_next(start);
    start_closure(start);
  }
  grpc_closure_list r = GRPC_CLOSURE_LIST_INIT;
  *list = r;
}

void grpc_exec_ctx_global_init(void) {
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_cv);
}

void grpc_exec_ctx_global_shutdown(void) {
  gpr_mu_lock(&g_mu);
  while (g_threads != 0) {
    gpr_cv_wait(&g_cv, &g_mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&g_mu);

  gpr_mu_destroy(&g_mu);
  gpr_cv_destroy(&g_cv);
}
#endif
