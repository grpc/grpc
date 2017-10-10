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

#include "src/core/lib/support/stack_lockfree.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

/* The lockfree node structure is a single architecture-level
   word that allows for an atomic CAS to set it up. */
struct lockfree_node_contents {
  /* next thing to look at. Actual index for head, next index otherwise */
  uint16_t index;
#ifdef GPR_ARCH_64
  uint16_t pad;
  uint32_t aba_ctr;
#else
#ifdef GPR_ARCH_32
  uint16_t aba_ctr;
#else
#error Unsupported bit width architecture
#endif
#endif
};

/* Use a union to make sure that these are in the same bits as an atm word */
typedef union lockfree_node {
  gpr_atm atm;
  struct lockfree_node_contents contents;
} lockfree_node;

/* make sure that entries aligned to 8-bytes */
#define ENTRY_ALIGNMENT_BITS 3
/* reserve this entry as invalid */
#define INVALID_ENTRY_INDEX ((1 << 16) - 1)

struct gpr_stack_lockfree {
  lockfree_node *entries;
  lockfree_node head; /* An atomic entry describing curr head */
};

gpr_stack_lockfree *gpr_stack_lockfree_create(size_t entries) {
  gpr_stack_lockfree *stack;
  stack = (gpr_stack_lockfree *)gpr_malloc(sizeof(*stack));
  /* Since we only allocate 16 bits to represent an entry number,
   * make sure that we are within the desired range */
  /* Reserve the highest entry number as a dummy */
  GPR_ASSERT(entries < INVALID_ENTRY_INDEX);
  stack->entries = (lockfree_node *)gpr_malloc_aligned(
      entries * sizeof(stack->entries[0]), ENTRY_ALIGNMENT_BITS);
  /* Clear out all entries */
  memset(stack->entries, 0, entries * sizeof(stack->entries[0]));
  memset(&stack->head, 0, sizeof(stack->head));

  GPR_ASSERT(sizeof(stack->entries->atm) == sizeof(stack->entries->contents));

  /* Point the head at reserved dummy entry */
  stack->head.contents.index = INVALID_ENTRY_INDEX;
/* Fill in the pad and aba_ctr to avoid confusing memcheck tools */
#ifdef GPR_ARCH_64
  stack->head.contents.pad = 0;
#endif
  stack->head.contents.aba_ctr = 0;
  return stack;
}

void gpr_stack_lockfree_destroy(gpr_stack_lockfree *stack) {
  gpr_free_aligned(stack->entries);
  gpr_free(stack);
}

int gpr_stack_lockfree_push(gpr_stack_lockfree *stack, int entry) {
  lockfree_node head;
  lockfree_node newhead;
  lockfree_node curent;
  lockfree_node newent;

  /* First fill in the entry's index and aba ctr for new head */
  newhead.contents.index = (uint16_t)entry;
#ifdef GPR_ARCH_64
  /* Fill in the pad to avoid confusing memcheck tools */
  newhead.contents.pad = 0;
#endif

  /* Also post-increment the aba_ctr */
  curent.atm = gpr_atm_no_barrier_load(&stack->entries[entry].atm);
  newhead.contents.aba_ctr = ++curent.contents.aba_ctr;
  gpr_atm_no_barrier_store(&stack->entries[entry].atm, curent.atm);

  do {
    /* Atomically get the existing head value for use */
    head.atm = gpr_atm_no_barrier_load(&(stack->head.atm));
    /* Point to it */
    newent.atm = gpr_atm_no_barrier_load(&stack->entries[entry].atm);
    newent.contents.index = head.contents.index;
    gpr_atm_no_barrier_store(&stack->entries[entry].atm, newent.atm);
  } while (!gpr_atm_rel_cas(&(stack->head.atm), head.atm, newhead.atm));
  /* Use rel_cas above to make sure that entry index is set properly */
  return head.contents.index == INVALID_ENTRY_INDEX;
}

int gpr_stack_lockfree_pop(gpr_stack_lockfree *stack) {
  lockfree_node head;
  lockfree_node newhead;

  do {
    head.atm = gpr_atm_acq_load(&(stack->head.atm));
    if (head.contents.index == INVALID_ENTRY_INDEX) {
      return -1;
    }
    newhead.atm =
        gpr_atm_no_barrier_load(&(stack->entries[head.contents.index].atm));

  } while (!gpr_atm_no_barrier_cas(&(stack->head.atm), head.atm, newhead.atm));

  return head.contents.index;
}
