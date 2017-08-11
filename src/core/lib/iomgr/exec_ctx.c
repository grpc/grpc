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

#include "src/core/lib/iomgr/exec_ctx.h"

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>

#include "src/core/lib/iomgr/combiner.h"
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

bool grpc_exec_ctx_has_work(grpc_exec_ctx *exec_ctx) {
  return exec_ctx->active_combiner != NULL ||
         !grpc_closure_list_empty(exec_ctx->closure_list);
}

void grpc_exec_ctx_finish(grpc_exec_ctx *exec_ctx) {
  exec_ctx->flags |= GRPC_EXEC_CTX_FLAG_IS_FINISHED;
  grpc_exec_ctx_flush(exec_ctx);
}

static void exec_ctx_run(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                         grpc_error *error) {
#ifndef NDEBUG
  closure->scheduled = false;
  if (GRPC_TRACER_ON(grpc_trace_closure)) {
    gpr_log(GPR_DEBUG, "running closure %p: created [%s:%d]: %s [%s:%d]",
            closure, closure->file_created, closure->line_created,
            closure->run ? "run" : "scheduled", closure->file_initiated,
            closure->line_initiated);
  }
#endif
  closure->cb(exec_ctx, closure->cb_arg, error);
#ifndef NDEBUG
  if (GRPC_TRACER_ON(grpc_trace_closure)) {
    gpr_log(GPR_DEBUG, "closure %p finished", closure);
  }
#endif
  GRPC_ERROR_UNREF(error);
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
        exec_ctx_run(exec_ctx, c, error);
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
