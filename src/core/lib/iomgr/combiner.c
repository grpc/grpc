/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/iomgr/combiner.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/workqueue.h"
#include "src/core/lib/profiling/timers.h"

int grpc_combiner_trace = 0;

#define GRPC_COMBINER_TRACE(fn) \
  do {                          \
    if (grpc_combiner_trace) {  \
      fn;                       \
    }                           \
  } while (0)

struct grpc_combiner {
  grpc_workqueue *optional_workqueue;
  gpr_mpscq queue;
  // state is:
  // lower bit - zero if orphaned
  // other bits - number of items queued on the lock
  gpr_atm state;
  bool take_async_break_before_final_list;
  grpc_closure_list final_list;
  grpc_closure continue_finishing;
};

grpc_combiner *grpc_combiner_create(grpc_workqueue *optional_workqueue) {
  grpc_combiner *lock = gpr_malloc(sizeof(*lock));
  lock->optional_workqueue = optional_workqueue;
  gpr_atm_no_barrier_store(&lock->state, 1);
  gpr_mpscq_init(&lock->queue);
  lock->take_async_break_before_final_list = false;
  grpc_closure_list_init(&lock->final_list);
  GRPC_COMBINER_TRACE(gpr_log(GPR_DEBUG, "C:%p create", lock));
  return lock;
}

static void really_destroy(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  GRPC_COMBINER_TRACE(gpr_log(GPR_DEBUG, "C:%p really_destroy", lock));
  GPR_ASSERT(gpr_atm_no_barrier_load(&lock->state) == 0);
  gpr_mpscq_destroy(&lock->queue);
  GRPC_WORKQUEUE_UNREF(exec_ctx, lock->optional_workqueue, "combiner");
  gpr_free(lock);
}

void grpc_combiner_destroy(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  gpr_atm old_state = gpr_atm_full_fetch_add(&lock->state, -1);
  GRPC_COMBINER_TRACE(gpr_log(
      GPR_DEBUG, "C:%p really_destroy old_state=%" PRIdPTR, lock, old_state));
  if (old_state == 1) {
    really_destroy(exec_ctx, lock);
  }
}

static bool maybe_finish_one(grpc_exec_ctx *exec_ctx, grpc_combiner *lock);
static void finish(grpc_exec_ctx *exec_ctx, grpc_combiner *lock);

static void continue_finishing_mainline(grpc_exec_ctx *exec_ctx, void *arg,
                                        grpc_error *error) {
  GPR_TIMER_BEGIN("combiner.continue_executing_mainline", 0);
  grpc_combiner *lock = arg;
  GRPC_COMBINER_TRACE(
      gpr_log(GPR_DEBUG, "C:%p continue_finishing_mainline", lock));
  GPR_ASSERT(exec_ctx->active_combiner == NULL);
  exec_ctx->active_combiner = lock;
  if (maybe_finish_one(exec_ctx, lock)) finish(exec_ctx, lock);
  GPR_ASSERT(exec_ctx->active_combiner == lock);
  exec_ctx->active_combiner = NULL;
  GPR_TIMER_END("combiner.continue_executing_mainline", 0);
}

static void execute_final(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  GPR_TIMER_BEGIN("combiner.execute_final", 0);
  grpc_closure *c = lock->final_list.head;
  GPR_ASSERT(c != NULL);
  grpc_closure_list_init(&lock->final_list);
  lock->take_async_break_before_final_list = false;
  int loops = 0;
  while (c != NULL) {
    GRPC_COMBINER_TRACE(
        gpr_log(GPR_DEBUG, "C:%p execute_final[%d] c=%p", lock, loops, c));
    grpc_closure *next = c->next_data.next;
    grpc_error *error = c->error;
    c->cb(exec_ctx, c->cb_arg, error);
    GRPC_ERROR_UNREF(error);
    c = next;
    loops++;
  }
  GPR_TIMER_END("combiner.execute_final", 0);
}

static void continue_executing_final(grpc_exec_ctx *exec_ctx, void *arg,
                                     grpc_error *error) {
  GPR_TIMER_BEGIN("combiner.continue_executing_final", 0);
  grpc_combiner *lock = arg;
  GRPC_COMBINER_TRACE(
      gpr_log(GPR_DEBUG, "C:%p continue_executing_final", lock));
  GPR_ASSERT(exec_ctx->active_combiner == NULL);
  exec_ctx->active_combiner = lock;
  // quick peek to see if new things have turned up on the queue: if so, go back
  // to executing them before the final list
  if ((gpr_atm_acq_load(&lock->state) >> 1) > 1) {
    if (maybe_finish_one(exec_ctx, lock)) finish(exec_ctx, lock);
  } else {
    execute_final(exec_ctx, lock);
    finish(exec_ctx, lock);
  }
  GPR_ASSERT(exec_ctx->active_combiner == lock);
  exec_ctx->active_combiner = NULL;
  GPR_TIMER_END("combiner.continue_executing_final", 0);
}

static bool start_execute_final(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  GPR_TIMER_BEGIN("combiner.start_execute_final", 0);
  GPR_ASSERT(exec_ctx->active_combiner == lock);
  GRPC_COMBINER_TRACE(
      gpr_log(GPR_DEBUG,
              "C:%p start_execute_final take_async_break_before_final_list=%d",
              lock, lock->take_async_break_before_final_list));
  if (lock->take_async_break_before_final_list) {
    grpc_closure_init(&lock->continue_finishing, continue_executing_final,
                      lock);
    grpc_exec_ctx_sched(exec_ctx, &lock->continue_finishing, GRPC_ERROR_NONE,
                        GRPC_WORKQUEUE_REF(lock->optional_workqueue, "sched"));
    GPR_TIMER_END("combiner.start_execute_final", 0);
    return false;
  } else {
    execute_final(exec_ctx, lock);
    GPR_TIMER_END("combiner.start_execute_final", 0);
    return true;
  }
}

static bool maybe_finish_one(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  GPR_TIMER_BEGIN("combiner.maybe_finish_one", 0);
  gpr_mpscq_node *n = gpr_mpscq_pop(&lock->queue);
  GRPC_COMBINER_TRACE(
      gpr_log(GPR_DEBUG, "C:%p maybe_finish_one n=%p", lock, n));
  GPR_ASSERT(exec_ctx->active_combiner == lock);
  if (n == NULL) {
    // Queue is in an transiently inconsistent state: a new item is being queued
    // but is not visible to this thread yet.
    // Use this as a cue that we should go off and do something else for a while
    // (and come back later)
    grpc_closure_init(&lock->continue_finishing, continue_finishing_mainline,
                      lock);
    grpc_exec_ctx_sched(exec_ctx, &lock->continue_finishing, GRPC_ERROR_NONE,
                        GRPC_WORKQUEUE_REF(lock->optional_workqueue, "sched"));
    GPR_TIMER_END("combiner.maybe_finish_one", 0);
    return false;
  }
  grpc_closure *cl = (grpc_closure *)n;
  grpc_error *error = cl->error;
  cl->cb(exec_ctx, cl->cb_arg, error);
  GRPC_ERROR_UNREF(error);
  GPR_TIMER_END("combiner.maybe_finish_one", 0);
  return true;
}

static void finish(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  bool (*executor)(grpc_exec_ctx * exec_ctx, grpc_combiner * lock);
  GPR_TIMER_BEGIN("combiner.finish", 0);
  int loops = 0;
  do {
    executor = maybe_finish_one;
    gpr_atm old_state = gpr_atm_full_fetch_add(&lock->state, -2);
    GRPC_COMBINER_TRACE(gpr_log(GPR_DEBUG,
                                "C:%p finish[%d] old_state=%" PRIdPTR, lock,
                                loops, old_state));
    switch (old_state) {
      default:
        // we have multiple queued work items: just continue executing them
        break;
      case 5:  // we're down to one queued item: if it's the final list we
      case 4:  // should do that
        if (!grpc_closure_list_empty(lock->final_list)) {
          executor = start_execute_final;
        }
        break;
      case 3:  // had one count, one unorphaned --> unlocked unorphaned
        GPR_TIMER_END("combiner.finish", 0);
        return;
      case 2:  // and one count, one orphaned --> unlocked and orphaned
        really_destroy(exec_ctx, lock);
        GPR_TIMER_END("combiner.finish", 0);
        return;
      case 1:
      case 0:
        // these values are illegal - representing an already unlocked or
        // deleted lock
        GPR_UNREACHABLE_CODE(return );
    }
    loops++;
  } while (executor(exec_ctx, lock));
  GPR_TIMER_END("combiner.finish", 0);
}

void grpc_combiner_execute(grpc_exec_ctx *exec_ctx, grpc_combiner *lock,
                           grpc_closure *cl, grpc_error *error) {
  GRPC_COMBINER_TRACE(
      gpr_log(GPR_DEBUG, "C:%p grpc_combiner_execute c=%p", lock, cl));
  GPR_TIMER_BEGIN("combiner.execute", 0);
  gpr_atm last = gpr_atm_full_fetch_add(&lock->state, 2);
  GPR_ASSERT(last & 1);  // ensure lock has not been destroyed
  if (last == 1) {
    exec_ctx->active_combiner = lock;
    GPR_TIMER_BEGIN("combiner.execute_first_cb", 0);
    cl->cb(exec_ctx, cl->cb_arg, error);
    GPR_TIMER_END("combiner.execute_first_cb", 0);
    GRPC_ERROR_UNREF(error);
    finish(exec_ctx, lock);
    GPR_ASSERT(exec_ctx->active_combiner == lock);
    exec_ctx->active_combiner = NULL;
  } else {
    cl->error = error;
    gpr_mpscq_push(&lock->queue, &cl->next_data.atm_next);
  }
  GPR_TIMER_END("combiner.execute", 0);
}

static void enqueue_finally(grpc_exec_ctx *exec_ctx, void *closure,
                            grpc_error *error) {
  grpc_combiner_execute_finally(exec_ctx, exec_ctx->active_combiner, closure,
                                GRPC_ERROR_REF(error), false);
}

void grpc_combiner_execute_finally(grpc_exec_ctx *exec_ctx, grpc_combiner *lock,
                                   grpc_closure *closure, grpc_error *error,
                                   bool force_async_break) {
  GRPC_COMBINER_TRACE(gpr_log(
      GPR_DEBUG,
      "C:%p grpc_combiner_execute_finally c=%p force_async_break=%d; ac=%p",
      lock, closure, force_async_break, exec_ctx->active_combiner));
  GPR_TIMER_BEGIN("combiner.execute_finally", 0);
  if (exec_ctx->active_combiner != lock) {
    GPR_TIMER_MARK("slowpath", 0);
    grpc_combiner_execute(exec_ctx, lock,
                          grpc_closure_create(enqueue_finally, closure), error);
    GPR_TIMER_END("combiner.execute_finally", 0);
    return;
  }

  if (force_async_break) {
    lock->take_async_break_before_final_list = true;
  }
  if (grpc_closure_list_empty(lock->final_list)) {
    gpr_atm_full_fetch_add(&lock->state, 2);
  }
  grpc_closure_list_append(&lock->final_list, closure, error);
  GPR_TIMER_END("combiner.execute_finally", 0);
}

void grpc_combiner_force_async_finally(grpc_combiner *lock) {
  lock->take_async_break_before_final_list = true;
}
