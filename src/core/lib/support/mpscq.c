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

#include "src/core/lib/support/mpscq.h"

#include <grpc/support/log.h>

void gpr_mpscq_init(gpr_mpscq *q) {
  gpr_atm_no_barrier_store(&q->head, (gpr_atm)&q->stub);
  q->tail = &q->stub;
  gpr_atm_no_barrier_store(&q->stub.next, (gpr_atm)NULL);
}

void gpr_mpscq_destroy(gpr_mpscq *q) {
  GPR_ASSERT(gpr_atm_no_barrier_load(&q->head) == (gpr_atm)&q->stub);
  GPR_ASSERT(q->tail == &q->stub);
}

bool gpr_mpscq_push(gpr_mpscq *q, gpr_mpscq_node *n) {
  gpr_atm_no_barrier_store(&n->next, (gpr_atm)NULL);
  gpr_mpscq_node *prev =
      (gpr_mpscq_node *)gpr_atm_full_xchg(&q->head, (gpr_atm)n);
  gpr_atm_rel_store(&prev->next, (gpr_atm)n);
  return prev == &q->stub;
}

gpr_mpscq_node *gpr_mpscq_pop(gpr_mpscq *q) {
  bool empty;
  return gpr_mpscq_pop_and_check_end(q, &empty);
}

gpr_mpscq_node *gpr_mpscq_pop_and_check_end(gpr_mpscq *q, bool *empty) {
  gpr_mpscq_node *tail = q->tail;
  gpr_mpscq_node *next = (gpr_mpscq_node *)gpr_atm_acq_load(&tail->next);
  if (tail == &q->stub) {
    // indicates the list is actually (ephemerally) empty
    if (next == NULL) {
      *empty = true;
      return NULL;
    }
    q->tail = next;
    tail = next;
    next = (gpr_mpscq_node *)gpr_atm_acq_load(&tail->next);
  }
  if (next != NULL) {
    *empty = false;
    q->tail = next;
    return tail;
  }
  gpr_mpscq_node *head = (gpr_mpscq_node *)gpr_atm_acq_load(&q->head);
  if (tail != head) {
    *empty = false;
    // indicates a retry is in order: we're still adding
    return NULL;
  }
  gpr_mpscq_push(q, &q->stub);
  next = (gpr_mpscq_node *)gpr_atm_acq_load(&tail->next);
  if (next != NULL) {
    q->tail = next;
    return tail;
  }
  // indicates a retry is in order: we're still adding
  *empty = false;
  return NULL;
}

void gpr_locked_mpscq_init(gpr_locked_mpscq *q) {
  gpr_mpscq_init(&q->queue);
  q->read_lock = GPR_SPINLOCK_INITIALIZER;
}

void gpr_locked_mpscq_destroy(gpr_locked_mpscq *q) {
  gpr_mpscq_destroy(&q->queue);
}

bool gpr_locked_mpscq_push(gpr_locked_mpscq *q, gpr_mpscq_node *n) {
  return gpr_mpscq_push(&q->queue, n);
}

gpr_mpscq_node *gpr_locked_mpscq_pop(gpr_locked_mpscq *q) {
  if (gpr_spinlock_trylock(&q->read_lock)) {
    gpr_mpscq_node *n = gpr_mpscq_pop(&q->queue);
    gpr_spinlock_unlock(&q->read_lock);
    return n;
  }
  return NULL;
}
