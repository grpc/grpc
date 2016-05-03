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

#define NO_CONSUMER ((gpr_atm)1)

void grpc_aelock_init(grpc_aelock *lock, grpc_workqueue *optional_workqueue) {
  lock->optional_workqueue = optional_workqueue;
  gpr_atm_no_barrier_store(&lock->locked, 0);
  gpr_mpscq_init(&lock->queue);
}

void grpc_aelock_destroy(grpc_aelock *lock) {
  GPR_ASSERT(gpr_atm_no_barrier_load(&lock->locked) == 0);
  gpr_mpscq_destroy(&lock->queue);
}

static void finish(grpc_exec_ctx *exec_ctx, grpc_aelock *lock) {
  while (gpr_atm_full_fetch_add(&lock->locked, -1) != 1) {
    gpr_mpscq_node *n;
    while ((n = gpr_mpscq_pop(&lock->queue)) == NULL) {
      // TODO(ctiller): find something to fill in the time
    }
    grpc_aelock_qnode *ln = (grpc_aelock_qnode*)n;
    ln->action(exec_ctx, ln->arg);
    gpr_free(ln);
  }
}

void grpc_aelock_execute(grpc_exec_ctx *exec_ctx, grpc_aelock *lock,
                         grpc_aelock_action action, void *arg,
                         size_t sizeof_arg) {
  if (gpr_atm_full_fetch_add(&lock->locked, 1) == 0) {
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
