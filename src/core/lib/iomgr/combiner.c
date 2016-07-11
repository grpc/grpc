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
  return lock;
}

static void really_destroy(grpc_combiner *lock) {
  GPR_ASSERT(gpr_atm_no_barrier_load(&lock->state) == 0);
  gpr_mpscq_destroy(&lock->queue);
  gpr_free(lock);
}

void grpc_combiner_destroy(grpc_combiner *lock) {
  if (gpr_atm_full_fetch_add(&lock->state, -1) == 1) {
    really_destroy(lock);
  }
}

static bool maybe_finish_one(grpc_exec_ctx *exec_ctx, grpc_combiner *lock);
static void finish(grpc_exec_ctx *exec_ctx, grpc_combiner *lock);

static void continue_finishing_mainline(grpc_exec_ctx *exec_ctx, void *arg,
                                        grpc_error *error) {
  if (maybe_finish_one(exec_ctx, arg)) finish(exec_ctx, arg);
}

static void execute_final(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  grpc_closure *c = lock->final_list.head;
  grpc_closure_list_init(&lock->final_list);
  lock->take_async_break_before_final_list = false;
  while (c != NULL) {
    grpc_closure *next = c->next_data.next;
    grpc_error *error = c->error;
    c->cb(exec_ctx, c->cb_arg, error);
    GRPC_ERROR_UNREF(error);
    c = next;
  }
}

static void continue_executing_final(grpc_exec_ctx *exec_ctx, void *arg,
                                     grpc_error *error) {
  grpc_combiner *lock = arg;
  // quick peek to see if new things have turned up on the queue: if so, go back
  // to executing them before the final list
  if ((gpr_atm_acq_load(&lock->state) >> 1) > 1) {
    if (maybe_finish_one(exec_ctx, lock)) finish(exec_ctx, lock);
  } else {
    execute_final(exec_ctx, lock);
    finish(exec_ctx, lock);
  }
}

static bool start_execute_final(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  if (lock->take_async_break_before_final_list) {
    grpc_closure_init(&lock->continue_finishing, continue_executing_final,
                      lock);
    grpc_exec_ctx_sched(exec_ctx, &lock->continue_finishing, GRPC_ERROR_NONE,
                        lock->optional_workqueue);
    return false;
  } else {
    execute_final(exec_ctx, lock);
    return true;
  }
}

static bool maybe_finish_one(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  gpr_mpscq_node *n = gpr_mpscq_pop(&lock->queue);
  if (n == NULL) {
    // queue is in an inconsistant state: use this as a cue that we should
    // go off and do something else for a while (and come back later)
    grpc_closure_init(&lock->continue_finishing, continue_finishing_mainline,
                      lock);
    grpc_exec_ctx_sched(exec_ctx, &lock->continue_finishing, GRPC_ERROR_NONE,
                        lock->optional_workqueue);
    return false;
  }
  grpc_closure *cl = (grpc_closure *)n;
  grpc_error *error = cl->error;
  cl->cb(exec_ctx, cl->cb_arg, error);
  GRPC_ERROR_UNREF(error);
  return true;
}

static void finish(grpc_exec_ctx *exec_ctx, grpc_combiner *lock) {
  bool (*executor)(grpc_exec_ctx * exec_ctx, grpc_combiner * lock) =
      maybe_finish_one;
  do {
    switch (gpr_atm_full_fetch_add(&lock->state, -2)) {
      case 5:  // we're down to one queued item: if it's the final list we
      case 4:  // should do that
        if (!grpc_closure_list_empty(lock->final_list)) {
          executor = start_execute_final;
        }
        break;
      case 3:  // had one count, one unorphaned --> unlocked unorphaned
        return;
      case 2:  // and one count, one orphaned --> unlocked and orphaned
        really_destroy(lock);
        return;
      case 1:
      case 0:
        // these values are illegal - representing an already unlocked or
        // deleted lock
        GPR_UNREACHABLE_CODE(return );
    }
  } while (executor(exec_ctx, lock));
}

void grpc_combiner_execute(grpc_exec_ctx *exec_ctx, grpc_combiner *lock,
                           grpc_closure *cl, grpc_error *error) {
  gpr_atm last = gpr_atm_full_fetch_add(&lock->state, 2);
  GPR_ASSERT(last & 1);  // ensure lock has not been destroyed
  if (last == 1) {
    cl->cb(exec_ctx, cl->cb_arg, error);
    GRPC_ERROR_UNREF(error);
    finish(exec_ctx, lock);
  } else {
    cl->error = error;
    gpr_mpscq_push(&lock->queue, &cl->next_data.atm_next);
  }
}

void grpc_combiner_execute_finally(grpc_exec_ctx *exec_ctx, grpc_combiner *lock,
                                   grpc_closure *closure, grpc_error *error,
                                   bool force_async_break) {
  if (force_async_break) {
    lock->take_async_break_before_final_list = true;
  }
  if (grpc_closure_list_empty(lock->final_list)) {
    gpr_atm_full_fetch_add(&lock->state, 2);
  }
  grpc_closure_list_append(&lock->final_list, closure, error);
}

void grpc_combiner_force_async_finally(grpc_combiner *lock) {
  lock->take_async_break_before_final_list = true;
}
