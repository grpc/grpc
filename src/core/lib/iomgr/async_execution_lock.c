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

#include "src/core/lib/iomgr/async_execution_lock.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#define STATE_BIT_ALIVE 1
#define STATE_BIT_REFS 2

typedef struct grpc_aelock_qnode {
  gpr_mpscq_node mpscq_node;
  grpc_aelock_action action;
  void *arg;
} grpc_aelock_qnode;

struct grpc_aelock {
  grpc_workqueue *optional_workqueue;
  gpr_mpscq queue;
  // state is:
  // lower bit - zero if orphaned
  // other bits - number of items queued on the lock
  // see: STATE_BIT_xxx
  gpr_atm state;
  grpc_aelock_action before_idle_action;
  void *before_idle_action_arg;
  grpc_closure continue_finishing;
};

static void continue_finishing(grpc_exec_ctx *exec_ctx, void *arg,
                               bool success);

grpc_aelock *grpc_aelock_create(grpc_workqueue *optional_workqueue,
                                grpc_aelock_action before_idle_action,
                                void *before_idle_action_arg) {
  grpc_aelock *lock = gpr_malloc(sizeof(*lock));
  lock->before_idle_action = before_idle_action;
  lock->before_idle_action_arg = before_idle_action_arg;
  lock->optional_workqueue = optional_workqueue;
  gpr_atm_no_barrier_store(&lock->state, STATE_BIT_ALIVE);
  gpr_mpscq_init(&lock->queue);
  grpc_closure_init(&lock->continue_finishing, continue_finishing, lock);
  return lock;
}

static void really_destroy(grpc_aelock *lock) {
  GPR_ASSERT(gpr_atm_no_barrier_load(&lock->state) == 0);
  gpr_mpscq_destroy(&lock->queue);
  gpr_free(lock);
}

void grpc_aelock_destroy(grpc_aelock *lock) {
  if (gpr_atm_full_fetch_add(&lock->state, -STATE_BIT_ALIVE) ==
      STATE_BIT_ALIVE) {
    really_destroy(lock);
  }
}

static bool maybe_finish_one(grpc_exec_ctx *exec_ctx, grpc_aelock *lock) {
  gpr_mpscq_node *n = gpr_mpscq_pop(&lock->queue);
  if (n == NULL) {
    return false;
  }
  grpc_aelock_qnode *ln = (grpc_aelock_qnode *)n;
  ln->action(exec_ctx, ln->arg);
  gpr_free(ln);
  return true;
}

static void finish(grpc_exec_ctx *exec_ctx, grpc_aelock *lock) {
  for (;;) {
    gpr_atm last_state = gpr_atm_full_fetch_add(&lock->state, -STATE_BIT_REFS);
    switch (last_state) {
      default:
      perform_one_step:
        gpr_log(GPR_DEBUG, "ls=%d execute", last_state);
        if (!maybe_finish_one(exec_ctx, lock)) {
          // perform the idle action before going off to do something else
          lock->before_idle_action(exec_ctx, lock->before_idle_action_arg);
          // quick peek to see if we can immediately resume
          if (!maybe_finish_one(exec_ctx, lock)) {
            // queue is in an inconsistant state: use this as a cue that we
            // should
            // go off and do something else for a while (and come back later)
            grpc_exec_ctx_enqueue(exec_ctx, &lock->continue_finishing, true,
                                  lock->optional_workqueue);
            return;
          }
        }
        break;
      case STATE_BIT_ALIVE | (2 * STATE_BIT_REFS):
        gpr_log(GPR_DEBUG, "ls=%d final", last_state);
        lock->before_idle_action(exec_ctx, lock->before_idle_action_arg);
        switch (gpr_atm_full_fetch_add(&lock->state, -STATE_BIT_REFS)) {
          case STATE_BIT_ALIVE | STATE_BIT_REFS:
            return;
          case STATE_BIT_REFS:
            really_destroy(lock);
            return;
          default:
            gpr_log(GPR_DEBUG, "retry");
            // oops: did the before action, but something else came in
            // better add another ref so we remember to do this again
            gpr_atm_full_fetch_add(&lock->state, STATE_BIT_REFS);
            goto perform_one_step;
        }
        break;
      case STATE_BIT_ALIVE | STATE_BIT_REFS:
        gpr_log(GPR_DEBUG, "ls=%d unlock", last_state);
        return;
      case 2 * STATE_BIT_REFS:
        gpr_log(GPR_DEBUG, "ls=%d idle", last_state);
        lock->before_idle_action(exec_ctx, lock->before_idle_action_arg);
        GPR_ASSERT(gpr_atm_full_fetch_add(&lock->state, -STATE_BIT_REFS) ==
                   STATE_BIT_REFS);
      case STATE_BIT_REFS:
        gpr_log(GPR_DEBUG, "ls=%d destroy", last_state);
        really_destroy(lock);
        return;
      case STATE_BIT_ALIVE:
      case 0:
        // these values are illegal - representing an already unlocked or
        // deleted lock
        GPR_UNREACHABLE_CODE(return );
    }
  }

  // while (maybe_finish_one(exec_ctx, lock));
}

static void continue_finishing(grpc_exec_ctx *exec_ctx, void *arg,
                               bool success) {
  grpc_aelock *lock = arg;
  if (maybe_finish_one(exec_ctx, lock)) {
    finish(exec_ctx, lock);
  } else {
    // queue is in an inconsistant state: use this as a cue that we should
    // go off and do something else for a while (and come back later)
    grpc_exec_ctx_enqueue(exec_ctx, &lock->continue_finishing, true,
                          lock->optional_workqueue);
  }
}

void grpc_aelock_execute(grpc_exec_ctx *exec_ctx, grpc_aelock *lock,
                         grpc_aelock_action action, void *arg,
                         size_t sizeof_arg) {
  gpr_atm last = gpr_atm_full_fetch_add(&lock->state, 2 * STATE_BIT_REFS);
  GPR_ASSERT(last & STATE_BIT_ALIVE);  // ensure lock has not been destroyed
  if (last == STATE_BIT_ALIVE) {
    action(exec_ctx, arg);
    finish(exec_ctx, lock);
  } else {
    gpr_atm_full_fetch_add(&lock->state, -STATE_BIT_REFS);
    grpc_aelock_qnode *n = gpr_malloc(sizeof(*n) + sizeof_arg);
    n->action = action;
    if (sizeof_arg > 0) {
      memcpy(n + 1, arg, sizeof_arg);
      n->arg = n + 1;
    } else {
      n->arg = arg;
    }
    gpr_mpscq_push(&lock->queue, &n->mpscq_node);
  }
}
