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

#ifndef NDEBUG
  /* Bitmap of pushed entries to check for double-push or pop */
  gpr_atm pushed[(INVALID_ENTRY_INDEX + 1) / (8 * sizeof(gpr_atm))];
#endif
};

gpr_stack_lockfree *gpr_stack_lockfree_create(size_t entries) {
  gpr_stack_lockfree *stack;
  stack = gpr_malloc(sizeof(*stack));
  /* Since we only allocate 16 bits to represent an entry number,
   * make sure that we are within the desired range */
  /* Reserve the highest entry number as a dummy */
  GPR_ASSERT(entries < INVALID_ENTRY_INDEX);
  stack->entries = gpr_malloc_aligned(entries * sizeof(stack->entries[0]),
                                      ENTRY_ALIGNMENT_BITS);
  /* Clear out all entries */
  memset(stack->entries, 0, entries * sizeof(stack->entries[0]));
  memset(&stack->head, 0, sizeof(stack->head));
#ifndef NDEBUG
  memset(&stack->pushed, 0, sizeof(stack->pushed));
#endif

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

#ifndef NDEBUG
  /* Check for double push */
  {
    int pushed_index = entry / (int)(8 * sizeof(gpr_atm));
    int pushed_bit = entry % (int)(8 * sizeof(gpr_atm));
    gpr_atm old_val;

    old_val = gpr_atm_no_barrier_fetch_add(&stack->pushed[pushed_index],
                                           ((gpr_atm)1 << pushed_bit));
    GPR_ASSERT((old_val & (((gpr_atm)1) << pushed_bit)) == 0);
  }
#endif

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
#ifndef NDEBUG
  /* Check for valid pop */
  {
    int pushed_index = head.contents.index / (8 * sizeof(gpr_atm));
    int pushed_bit = head.contents.index % (8 * sizeof(gpr_atm));
    gpr_atm old_val;

    old_val = gpr_atm_no_barrier_fetch_add(&stack->pushed[pushed_index],
                                           -((gpr_atm)1 << pushed_bit));
    GPR_ASSERT((old_val & (((gpr_atm)1) << pushed_bit)) != 0);
  }
#endif

  return head.contents.index;
}
