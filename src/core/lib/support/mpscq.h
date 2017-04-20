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
// Push a node, returns true if this was the first addition
// (may spuriously return true, but will never spuriously return false)
bool gpr_mpscq_push(gpr_mpscq *q, gpr_mpscq_node *n);
// Pop a node
// If the queue is empty, returns false (the value of *n is undefined)
// If the queue is non-empty, returns true and:
//    sets *n to the node that was retrieved
// OR sets *n to NULL if a node cannot be retrieved right now
bool gpr_mpscq_pop(gpr_mpscq *q, gpr_mpscq_node **n);

#endif /* GRPC_CORE_LIB_SUPPORT_MPSCQ_H */
