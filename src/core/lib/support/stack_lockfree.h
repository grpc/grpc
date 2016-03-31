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

#ifndef GRPC_CORE_LIB_SUPPORT_STACK_LOCKFREE_H
#define GRPC_CORE_LIB_SUPPORT_STACK_LOCKFREE_H

#include <stddef.h>

typedef struct gpr_stack_lockfree gpr_stack_lockfree;

/* This stack must specify the maximum number of entries to track.
   The current implementation only allows up to 65534 entries */
gpr_stack_lockfree *gpr_stack_lockfree_create(size_t entries);
void gpr_stack_lockfree_destroy(gpr_stack_lockfree *stack);

/* Pass in a valid entry number for the next stack entry */
/* Returns 1 if this is the first element on the stack, 0 otherwise */
int gpr_stack_lockfree_push(gpr_stack_lockfree *, int entry);

/* Returns -1 on empty or the actual entry number */
int gpr_stack_lockfree_pop(gpr_stack_lockfree *stack);

#endif /* GRPC_CORE_LIB_SUPPORT_STACK_LOCKFREE_H */
