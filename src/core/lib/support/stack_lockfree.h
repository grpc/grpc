/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SUPPORT_STACK_LOCKFREE_H
#define GRPC_CORE_LIB_SUPPORT_STACK_LOCKFREE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gpr_stack_lockfree gpr_stack_lockfree;

/* This stack must specify the maximum number of entries to track.
   The current implementation only allows up to 65534 entries */
gpr_stack_lockfree* gpr_stack_lockfree_create(size_t entries);
void gpr_stack_lockfree_destroy(gpr_stack_lockfree* stack);

/* Pass in a valid entry number for the next stack entry */
/* Returns 1 if this is the first element on the stack, 0 otherwise */
int gpr_stack_lockfree_push(gpr_stack_lockfree*, int entry);

/* Returns -1 on empty or the actual entry number */
int gpr_stack_lockfree_pop(gpr_stack_lockfree* stack);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_SUPPORT_STACK_LOCKFREE_H */
