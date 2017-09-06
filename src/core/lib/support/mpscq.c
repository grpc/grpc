/*
 *
 * Copyright 2016 gRPC authors.
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

void gpr_mpscq_push(gpr_mpscq *q, gpr_mpscq_node *n) {
  gpr_atm_no_barrier_store(&n->next, (gpr_atm)NULL);
  gpr_mpscq_node *prev =
      (gpr_mpscq_node *)gpr_atm_full_xchg(&q->head, (gpr_atm)n);
  gpr_atm_rel_store(&prev->next, (gpr_atm)n);
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
