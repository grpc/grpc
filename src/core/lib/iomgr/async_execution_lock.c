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
  gpr_atm state;
  grpc_closure continue_finishing;
};

static void continue_finishing(grpc_exec_ctx *exec_ctx, void *arg,
                               bool success);

grpc_aelock *grpc_aelock_create(grpc_workqueue *optional_workqueue) {
  grpc_aelock *lock = gpr_malloc(sizeof(*lock));
  lock->optional_workqueue = optional_workqueue;
  gpr_atm_no_barrier_store(&lock->state, 1);
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
  if (gpr_atm_full_fetch_add(&lock->state, -1) == 1) {
    really_destroy(lock);
  }
}

static bool maybe_finish_one(grpc_exec_ctx *exec_ctx, grpc_aelock *lock) {
  gpr_mpscq_node *n = gpr_mpscq_pop(&lock->queue);
  if (n == NULL) {
    // queue is in an inconsistant state: use this as a cue that we should
    // go off and do something else for a while (and come back later)
    grpc_exec_ctx_enqueue(exec_ctx, &lock->continue_finishing, true,
                          lock->optional_workqueue);
    return false;
  }
  grpc_aelock_qnode *ln = (grpc_aelock_qnode *)n;
  ln->action(exec_ctx, ln->arg);
  gpr_free(ln);
  return true;
}

static void finish(grpc_exec_ctx *exec_ctx, grpc_aelock *lock) {
  do {
    switch (gpr_atm_full_fetch_add(&lock->state, -2)) {
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
  } while (maybe_finish_one(exec_ctx, lock));
}

static void continue_finishing(grpc_exec_ctx *exec_ctx, void *arg,
                               bool success) {
  if (maybe_finish_one(exec_ctx, arg)) finish(exec_ctx, arg);
}

void grpc_aelock_execute(grpc_exec_ctx *exec_ctx, grpc_aelock *lock,
                         grpc_aelock_action action, void *arg,
                         size_t sizeof_arg) {
  gpr_atm last = gpr_atm_full_fetch_add(&lock->state, 2);
  GPR_ASSERT(last & 1);  // ensure lock has not been destroyed
  if (last == 1) {
    action(exec_ctx, arg);
    finish(exec_ctx, lock);
  } else {
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
