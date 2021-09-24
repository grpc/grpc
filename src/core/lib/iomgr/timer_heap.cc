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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/timer_heap.h"

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/port.h"

/* Adjusts a heap so as to move a hole at position i closer to the root,
   until a suitable position is found for element t. Then, copies t into that
   position. This functor is called each time immediately after modifying a
   value in the underlying container, with the offset of the modified element as
   its argument. */
static void adjust_upwards(grpc_timer** first, uint32_t i, grpc_timer* t) {
  while (i > 0) {
    uint32_t parent = static_cast<uint32_t>((static_cast<int>(i) - 1) / 2);
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
static void adjust_downwards(grpc_timer** first, uint32_t i, uint32_t length,
                             grpc_timer* t) {
  for (;;) {
    uint32_t left_child = 1u + 2u * i;
    if (left_child >= length) break;
    uint32_t right_child = left_child + 1;
    uint32_t next_i = right_child < length && first[left_child]->deadline >
                                                  first[right_child]->deadline
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

static void maybe_shrink(grpc_timer_heap* heap) {
  if (heap->timer_count >= 8 &&
      heap->timer_count <= heap->timer_capacity / SHRINK_FULLNESS_FACTOR / 2) {
    heap->timer_capacity = heap->timer_count * SHRINK_FULLNESS_FACTOR;
    heap->timers = static_cast<grpc_timer**>(
        gpr_realloc(heap->timers, heap->timer_capacity * sizeof(grpc_timer*)));
  }
}

static void note_changed_priority(grpc_timer_heap* heap, grpc_timer* timer) {
  uint32_t i = timer->heap_index;
  uint32_t parent = static_cast<uint32_t>((static_cast<int>(i) - 1) / 2);
  if (heap->timers[parent]->deadline > timer->deadline) {
    adjust_upwards(heap->timers, i, timer);
  } else {
    adjust_downwards(heap->timers, i, heap->timer_count, timer);
  }
}

void grpc_timer_heap_init(grpc_timer_heap* heap) {
  memset(heap, 0, sizeof(*heap));
}

void grpc_timer_heap_destroy(grpc_timer_heap* heap) { gpr_free(heap->timers); }

bool grpc_timer_heap_add(grpc_timer_heap* heap, grpc_timer* timer) {
  if (heap->timer_count == heap->timer_capacity) {
    heap->timer_capacity =
        std::max(heap->timer_capacity + 1, heap->timer_capacity * 3 / 2);
    heap->timers = static_cast<grpc_timer**>(
        gpr_realloc(heap->timers, heap->timer_capacity * sizeof(grpc_timer*)));
  }
  timer->heap_index = heap->timer_count;
  adjust_upwards(heap->timers, heap->timer_count, timer);
  heap->timer_count++;
  return timer->heap_index == 0;
}

void grpc_timer_heap_remove(grpc_timer_heap* heap, grpc_timer* timer) {
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

bool grpc_timer_heap_is_empty(grpc_timer_heap* heap) {
  return heap->timer_count == 0;
}

grpc_timer* grpc_timer_heap_top(grpc_timer_heap* heap) {
  return heap->timers[0];
}

void grpc_timer_heap_pop(grpc_timer_heap* heap) {
  grpc_timer_heap_remove(heap, grpc_timer_heap_top(heap));
}
