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

#ifndef GRPC_CORE_LIB_SUPPORT_MPSCQ_H
#define GRPC_CORE_LIB_SUPPORT_MPSCQ_H

#include <grpc/support/atm.h>
#include <stdbool.h>
#include <stddef.h>

// Multiple-producer single-consumer lock free queue, based upon the
// implementation from Dmitry Vyukov here:
// http://www.1024cores.net/home/lock-free-algorithms/queues/intrusive-mpsc-node-based-queue

// List node (include this in a data structure at the top, and add application
// fields after it - to simulate inheritance)
typedef struct gpr_mpscq_node { gpr_atm next; } gpr_mpscq_node;

// Actual queue type
typedef struct gpr_mpscq {
  gpr_atm head;
  // make sure head & tail don't share a cacheline
  char padding[GPR_CACHELINE_SIZE];
  gpr_mpscq_node *tail;
  gpr_mpscq_node stub;
} gpr_mpscq;

void gpr_mpscq_init(gpr_mpscq *q);
void gpr_mpscq_destroy(gpr_mpscq *q);
// Push a node
void gpr_mpscq_push(gpr_mpscq *q, gpr_mpscq_node *n);
// Pop a node (returns NULL if no node is ready - which doesn't indicate that
// the queue is empty!!)
gpr_mpscq_node *gpr_mpscq_pop(gpr_mpscq *q);
// Pop a node; sets *empty to true if the queue is empty, or false if it is not
gpr_mpscq_node *gpr_mpscq_pop_and_check_end(gpr_mpscq *q, bool *empty);

#endif /* GRPC_CORE_LIB_SUPPORT_MPSCQ_H */
