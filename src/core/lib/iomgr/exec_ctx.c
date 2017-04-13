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

#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/workqueue.h"
#include "src/core/lib/profiling/timers.h"

bool grpc_exec_ctx_ready_to_finish(grpc_exec_ctx *exec_ctx) {
  if ((exec_ctx->flags & GRPC_EXEC_CTX_FLAG_IS_FINISHED) == 0) {
    if (exec_ctx->check_ready_to_finish(exec_ctx,
                                        exec_ctx->check_ready_to_finish_arg)) {
      exec_ctx->flags |= GRPC_EXEC_CTX_FLAG_IS_FINISHED;
      return true;
    }
    return false;
  } else {
    return true;
  }
}

bool grpc_never_ready_to_finish(grpc_exec_ctx *exec_ctx, void *arg_ignored) {
  return false;
}

bool grpc_always_ready_to_finish(grpc_exec_ctx *exec_ctx, void *arg_ignored) {
  return true;
}

bool grpc_exec_ctx_flush(grpc_exec_ctx *exec_ctx) {
  bool did_something = 0;
  GPR_TIMER_BEGIN("grpc_exec_ctx_flush", 0);
  for (;;) {
    if (!grpc_closure_list_empty(exec_ctx->closure_list)) {
      grpc_closure *c = exec_ctx->closure_list.head;
      exec_ctx->closure_list.head = exec_ctx->closure_list.tail = NULL;
      while (c != NULL) {
        grpc_closure *next = c->next_data.next;
        grpc_error *error = c->error_data.error;
        did_something = true;
#ifndef NDEBUG
        c->scheduled = false;
#endif
        c->cb(exec_ctx, c->cb_arg, error);
        GRPC_ERROR_UNREF(error);
        c = next;
      }
    } else if (!grpc_combiner_continue_exec_ctx(exec_ctx)) {
      break;
    }
  }
  GPR_ASSERT(exec_ctx->active_combiner == NULL);
  GPR_TIMER_END("grpc_exec_ctx_flush", 0);
  return did_something;
}

void grpc_exec_ctx_finish(grpc_exec_ctx *exec_ctx) {
  exec_ctx->flags |= GRPC_EXEC_CTX_FLAG_IS_FINISHED;
  grpc_exec_ctx_flush(exec_ctx);
}

static void exec_ctx_run(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                         grpc_error *error) {
#ifndef NDEBUG
  closure->scheduled = false;
#endif
  closure->cb(exec_ctx, closure->cb_arg, error);
  GRPC_ERROR_UNREF(error);
}

static void exec_ctx_sched(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                           grpc_error *error) {
  grpc_closure_list_append(&exec_ctx->closure_list, closure, error);
}

void grpc_exec_ctx_global_init(void) {}
void grpc_exec_ctx_global_shutdown(void) {}

static const grpc_closure_scheduler_vtable exec_ctx_scheduler_vtable = {
    exec_ctx_run, exec_ctx_sched, "exec_ctx"};
static grpc_closure_scheduler exec_ctx_scheduler = {&exec_ctx_scheduler_vtable};
grpc_closure_scheduler *grpc_schedule_on_exec_ctx = &exec_ctx_scheduler;
