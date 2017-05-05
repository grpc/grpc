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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_TIMER_USE_GENERIC

#include "src/core/lib/iomgr/timer_heap.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/useful.h>

/* Adjusts a heap so as to move a hole at position i closer to the root,
   until a suitable position is found for element t. Then, copies t into that
   position. This functor is called each time immediately after modifying a
   value in the underlying container, with the offset of the modified element as
   its argument. */
static void adjust_upwards(grpc_timer **first, uint32_t i, grpc_timer *t) {
  while (i > 0) {
    uint32_t parent = (uint32_t)(((int)i - 1) / 2);
    if (first[parent]->deadline <= t->deadline) break;
    first[i] = first[parent];
    first[i]->heap_index = i;
    i = parent;
  }
  first[i] = t;
  t->heap_index = i;
}

/* Adjusts a heap so as to move a hole at position i farther away from the root,
   until a suitable position is found for element t.  Then, copies t into that
   position. */
static void adjust_downwards(grpc_timer **first, uint32_t i, uint32_t length,
                             grpc_timer *t) {
  for (;;) {
    uint32_t left_child = 1u + 2u * i;
    if (left_child >= length) break;
    uint32_t right_child = left_child + 1;
    uint32_t next_i =
        right_child < length &&
                first[left_child]->deadline > first[right_child]->deadline
            ? right_child
            : left_child;
    if (t->deadline <= first[next_i]->deadline) break;
    first[i] = first[next_i];
    first[i]->heap_index = i;
    i = next_i;
  }
  first[i] = t;
  t->heap_index = i;
}

#define SHRINK_MIN_ELEMS 8
#define SHRINK_FULLNESS_FACTOR 2

static void maybe_shrink(grpc_timer_heap *heap) {
  if (heap->timer_count >= 8 &&
      heap->timer_count <= heap->timer_capacity / SHRINK_FULLNESS_FACTOR / 2) {
    heap->timer_capacity = heap->timer_count * SHRINK_FULLNESS_FACTOR;
    heap->timers =
        gpr_realloc(heap->timers, heap->timer_capacity * sizeof(grpc_timer *));
  }
}

static void note_changed_priority(grpc_timer_heap *heap, grpc_timer *timer) {
  uint32_t i = timer->heap_index;
  uint32_t parent = (uint32_t)(((int)i - 1) / 2);
  if (heap->timers[parent]->deadline > timer->deadline) {
    adjust_upwards(heap->timers, i, timer);
  } else {
    adjust_downwards(heap->timers, i, heap->timer_count, timer);
  }
}

void grpc_timer_heap_init(grpc_timer_heap *heap) {
  memset(heap, 0, sizeof(*heap));
}

void grpc_timer_heap_destroy(grpc_timer_heap *heap) { gpr_free(heap->timers); }

int grpc_timer_heap_add(grpc_timer_heap *heap, grpc_timer *timer) {
  if (heap->timer_count == heap->timer_capacity) {
    heap->timer_capacity =
        GPR_MAX(heap->timer_capacity + 1, heap->timer_capacity * 3 / 2);
    heap->timers =
        gpr_realloc(heap->timers, heap->timer_capacity * sizeof(grpc_timer *));
  }
  timer->heap_index = heap->timer_count;
  adjust_upwards(heap->timers, heap->timer_count, timer);
  heap->timer_count++;
  return timer->heap_index == 0;
}

void grpc_timer_heap_remove(grpc_timer_heap *heap, grpc_timer *timer) {
  uint32_t i = timer->heap_index;
  if (i == heap->timer_count - 1) {
    heap->timer_count--;
    maybe_shrink(heap);
    return;
  }
  heap->timers[i] = heap->timers[heap->timer_count - 1];
  heap->timers[i]->heap_index = i;
  heap->timer_count--;
  maybe_shrink(heap);
  note_changed_priority(heap, heap->timers[i]);
}

int grpc_timer_heap_is_empty(grpc_timer_heap *heap) {
  return heap->timer_count == 0;
}

grpc_timer *grpc_timer_heap_top(grpc_timer_heap *heap) {
  return heap->timers[0];
}

void grpc_timer_heap_pop(grpc_timer_heap *heap) {
  grpc_timer_heap_remove(heap, grpc_timer_heap_top(heap));
}

#endif /* GRPC_TIMER_USE_GENERIC */
