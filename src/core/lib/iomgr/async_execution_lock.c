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

#include "src/core/lib/iomgr/async_execution_lock.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#define NO_CONSUMER ((gpr_atm)1)

static void bad_action(grpc_exec_ctx *exec_ctx, void *arg) {
  GPR_UNREACHABLE_CODE(return );
}

void grpc_aelock_init(grpc_aelock *lock, grpc_workqueue *optional_workqueue) {
  lock->optional_workqueue = optional_workqueue;
  gpr_atm_no_barrier_store(&lock->head, NO_CONSUMER);
  gpr_atm_no_barrier_store(&lock->tombstone.next, 0);
  lock->tombstone.action = bad_action;
  lock->tail = &lock->tombstone;
}

void grpc_aelock_destroy(grpc_aelock *lock) {
  GPR_ASSERT(gpr_atm_no_barrier_load(&lock->head) == NO_CONSUMER);
}

static void finish(grpc_exec_ctx *exec_ctx, grpc_aelock *lock) {
  for (;;) {
    grpc_aelock_qnode *tail = lock->tail;
    grpc_aelock_qnode *next =
        (grpc_aelock_qnode *)gpr_atm_acq_load(&tail->next);
    if (tail == &lock->tombstone) {
      if (next == NULL) {
        if (gpr_atm_rel_cas(&lock->head, (gpr_atm)&lock->tombstone,
                            NO_CONSUMER)) {
          return;
        }
        // TODO(ctiller): consider sleeping
        continue;
      } else {
        // skip the tombstone: we'll re-add it later
        lock->tail = next;
        tail = next;
        next = (grpc_aelock_qnode *)gpr_atm_acq_load(&tail->next);
      }
    }
    if (next != NULL) {
      // found a node
      lock->tail = next;
      tail->action(exec_ctx, tail->arg);
      gpr_free(tail);
    } else {
      // nothing there: might be in an incosistant state
      grpc_aelock_qnode *head =
          (grpc_aelock_qnode *)gpr_atm_acq_load(&lock->head);
      if (head != tail) {
        // non-empty list: spin for a bit
        // TODO(ctiller): consider sleeping?
        continue;
      }
      // must have swallowed tombstone above: re-add it
      gpr_atm_no_barrier_store(&lock->tombstone.next, 0);
      while (!gpr_atm_rel_cas(&lock->head, (gpr_atm)head,
                              (gpr_atm)&lock->tombstone)) {
        head = (grpc_aelock_qnode *)gpr_atm_acq_load(&lock->head);
      }
      gpr_atm_rel_store(&head->next, (gpr_atm)&lock->tombstone);
    }
  }
}

void grpc_aelock_execute(grpc_exec_ctx *exec_ctx, grpc_aelock *lock,
                         grpc_aelock_action action, void *arg,
                         size_t sizeof_arg) {
  gpr_atm head;
retry_top:
  head = gpr_atm_acq_load(&lock->head);
  if (head == NO_CONSUMER) {
    if (!gpr_atm_rel_cas(&lock->head, NO_CONSUMER, (gpr_atm)&lock->tombstone)) {
      goto retry_top;
    }
    action(exec_ctx, arg);
    finish(exec_ctx, lock);
    return;  // early out
  }

  grpc_aelock_qnode *n = gpr_malloc(sizeof(*n) + sizeof_arg);
  n->action = action;
  if (sizeof_arg > 0) {
    memcpy(n + 1, arg, sizeof_arg);
    n->arg = n + 1;
  } else {
    n->arg = arg;
  }
  gpr_atm_rel_store(&n->next, 0);
  while (!gpr_atm_rel_cas(&lock->head, head, (gpr_atm)n)) {
  retry_queue_load:
    head = gpr_atm_acq_load(&lock->head);
    if (head == NO_CONSUMER) {
      if (!gpr_atm_rel_cas(&lock->head, NO_CONSUMER,
                           (gpr_atm)&lock->tombstone)) {
        goto retry_queue_load;
      }
      gpr_free(n);
      action(exec_ctx, arg);
      finish(exec_ctx, lock);
      return;  // early out
    }
  }
  GPR_ASSERT(gpr_atm_rel_cas(&((grpc_aelock_qnode *)head)->next, 0, (gpr_atm)n));
//  gpr_atm_rel_store(&((grpc_aelock_qnode *)head)->next, (gpr_atm)n);
}
