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

#include "src/core/iomgr/exec_ctx.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/iomgr/workqueue.h"
#include "src/core/profiling/timers.h"

bool grpc_always_offload(void *ignored) { return true; }

bool grpc_never_offload(void *ignored) { return false; }

bool grpc_exec_ctx_flush(grpc_exec_ctx *exec_ctx) {
  bool did_something = false;
  GPR_TIMER_BEGIN("grpc_exec_ctx_flush", 0);
  for (;;) {
    // Flush the non-offloadable closure list
    while (!grpc_closure_list_empty(exec_ctx->closure_list)) {
      grpc_closure *c = exec_ctx->closure_list.head;
      exec_ctx->closure_list.head = exec_ctx->closure_list.tail = NULL;
      while (c != NULL) {
        bool success = (bool)(c->final_data & 1);
        grpc_closure *next = (grpc_closure *)(c->final_data & ~(uintptr_t)1);
        did_something = true;
        GPR_TIMER_BEGIN("grpc_exec_ctx_flush.cb", 0);
        c->cb(exec_ctx, c->cb_arg, success);
        GPR_TIMER_END("grpc_exec_ctx_flush.cb", 0);
        c = next;
      }
    }

    // Check offloadability
    if (exec_ctx->offload_check(exec_ctx->offload_check_arg)) {
      // everything should be offloaded
      for (;;) {
        grpc_offloadable_closure_list *take = exec_ctx->offloadables;
        if (take == NULL) {
          break;
        }
        exec_ctx->offloadables = take->next;
        grpc_workqueue_push_list(take->workqueue, &take->closure_list);
        // may add things to our offload lists, and closure lists
        GRPC_WORKQUEUE_UNREF(exec_ctx, take->workqueue, "exec_ctx");
        take->next = exec_ctx->free_offloadables;
        exec_ctx->free_offloadables = take;
      }

      // if we didn't pick up new work, then we're provably done
      if (grpc_closure_list_empty(exec_ctx->closure_list)) {
        break;
      }
    } else {
      // not offloading yet: pull one thing in and continue executing on this
      // thread
      grpc_offloadable_closure_list *take = exec_ctx->offloadables;
      if (take == NULL) {
        // nothing to take => we're done
        break;
      }
      exec_ctx->offloadables = take->next;
      GPR_SWAP(grpc_closure_list, take->closure_list, exec_ctx->closure_list);
      GRPC_WORKQUEUE_UNREF(exec_ctx, take->workqueue, "exec_ctx");
      take->next = exec_ctx->free_offloadables;
      exec_ctx->free_offloadables = take;
    }
  }
  GPR_TIMER_END("grpc_exec_ctx_flush", 0);
  return did_something;
}

static void free_offloadable_list(grpc_exec_ctx *exec_ctx,
                                  grpc_offloadable_closure_list *del) {
  while (del != NULL) {
    grpc_offloadable_closure_list *next = del->next;
    if (del >= exec_ctx->inlined &&
        del < exec_ctx->inlined + GRPC_OFFLOADABLE_INLINE_COUNT) {
      // do nothing
    } else {
      gpr_free(del);
    }
    del = next;
  }
}

void grpc_exec_ctx_finish(grpc_exec_ctx *exec_ctx) {
  // offload any pending work if possible: the library is trying to return
  // control elsewhere.
  exec_ctx->offload_check = grpc_always_offload;
  grpc_exec_ctx_flush(exec_ctx);

  GPR_ASSERT(exec_ctx->offloadables == NULL);
  free_offloadable_list(exec_ctx, exec_ctx->free_offloadables);
}

static grpc_closure_list *closure_list_for_workqueue(
    grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue) {
  // no workqueue => default closure list
  if (workqueue == NULL) {
    return &exec_ctx->closure_list;
  }

  // check to see if we've seen this workqueue in this exec_ctx before
  for (grpc_offloadable_closure_list *check = exec_ctx->offloadables;
       check != NULL; check = check->next) {
    if (check->workqueue == workqueue) {
      return &check->closure_list;
    }
  }

  // allocate a new offloadable list, add it to the exec_ctx, and we're done
  grpc_offloadable_closure_list *offloadable;
  if (exec_ctx->free_offloadables != NULL) {
    // recycle an old offloadable
    offloadable = exec_ctx->free_offloadables;
    exec_ctx->free_offloadables = offloadable->next;
  } else if (exec_ctx->inlined_used < GRPC_OFFLOADABLE_INLINE_COUNT) {
    // allocate from the inlined segment within the exec ctx
    offloadable = &exec_ctx->inlined[exec_ctx->inlined_used];
    exec_ctx->inlined_used++;
  } else {
    // allocate a new element
    offloadable = gpr_malloc(sizeof(*offloadable));
    grpc_closure_list initialized_list = GRPC_CLOSURE_LIST_INIT;
    offloadable->closure_list = initialized_list;
  }

  offloadable->next = exec_ctx->offloadables;
  offloadable->workqueue = GRPC_WORKQUEUE_REF(workqueue, "exec_ctx");
  exec_ctx->offloadables = offloadable;

  return &offloadable->closure_list;
}

void grpc_exec_ctx_enqueue(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                           bool success,
                           grpc_workqueue *offload_target_or_null) {
  grpc_closure_list_add(
      closure_list_for_workqueue(exec_ctx, offload_target_or_null), closure,
      success);
}

void grpc_exec_ctx_enqueue_list(grpc_exec_ctx *exec_ctx,
                                grpc_closure_list *list,
                                grpc_workqueue *offload_target_or_null) {
  grpc_closure_list_move(
      list, closure_list_for_workqueue(exec_ctx, offload_target_or_null));
}
