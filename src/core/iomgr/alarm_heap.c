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

#include "src/core/iomgr/alarm_heap.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/useful.h>

/* Adjusts a heap so as to move a hole at position i closer to the root,
   until a suitable position is found for element t. Then, copies t into that
   position. This functor is called each time immediately after modifying a
   value in the underlying container, with the offset of the modified element as
   its argument. */
static void adjust_upwards(grpc_alarm **first, int i, grpc_alarm *t) {
  while (i > 0) {
    int parent = (i - 1) / 2;
    if (gpr_time_cmp(first[parent]->deadline, t->deadline) >= 0) break;
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
static void adjust_downwards(grpc_alarm **first, int i, int length,
                             grpc_alarm *t) {
  for (;;) {
    int left_child = 1 + 2 * i;
    int right_child;
    int next_i;
    if (left_child >= length) break;
    right_child = left_child + 1;
    next_i = right_child < length &&
                     gpr_time_cmp(first[left_child]->deadline,
                                  first[right_child]->deadline) < 0
                 ? right_child
                 : left_child;
    if (gpr_time_cmp(t->deadline, first[next_i]->deadline) >= 0) break;
    first[i] = first[next_i];
    first[i]->heap_index = i;
    i = next_i;
  }
  first[i] = t;
  t->heap_index = i;
}

#define SHRINK_MIN_ELEMS 8
#define SHRINK_FULLNESS_FACTOR 2

static void maybe_shrink(grpc_alarm_heap *heap) {
  if (heap->alarm_count >= 8 &&
      heap->alarm_count <= heap->alarm_capacity / SHRINK_FULLNESS_FACTOR / 2) {
    heap->alarm_capacity = heap->alarm_count * SHRINK_FULLNESS_FACTOR;
    heap->alarms =
        gpr_realloc(heap->alarms, heap->alarm_capacity * sizeof(grpc_alarm *));
  }
}

static void note_changed_priority(grpc_alarm_heap *heap, grpc_alarm *alarm) {
  int i = alarm->heap_index;
  int parent = (i - 1) / 2;
  if (gpr_time_cmp(heap->alarms[parent]->deadline, alarm->deadline) < 0) {
    adjust_upwards(heap->alarms, i, alarm);
  } else {
    adjust_downwards(heap->alarms, i, heap->alarm_count, alarm);
  }
}

void grpc_alarm_heap_init(grpc_alarm_heap *heap) {
  memset(heap, 0, sizeof(*heap));
}

void grpc_alarm_heap_destroy(grpc_alarm_heap *heap) { gpr_free(heap->alarms); }

int grpc_alarm_heap_add(grpc_alarm_heap *heap, grpc_alarm *alarm) {
  if (heap->alarm_count == heap->alarm_capacity) {
    heap->alarm_capacity =
        GPR_MAX(heap->alarm_capacity + 1, heap->alarm_capacity * 3 / 2);
    heap->alarms =
        gpr_realloc(heap->alarms, heap->alarm_capacity * sizeof(grpc_alarm *));
  }
  alarm->heap_index = heap->alarm_count;
  adjust_upwards(heap->alarms, heap->alarm_count, alarm);
  heap->alarm_count++;
  return alarm->heap_index == 0;
}

void grpc_alarm_heap_remove(grpc_alarm_heap *heap, grpc_alarm *alarm) {
  int i = alarm->heap_index;
  if (i == heap->alarm_count - 1) {
    heap->alarm_count--;
    maybe_shrink(heap);
    return;
  }
  heap->alarms[i] = heap->alarms[heap->alarm_count - 1];
  heap->alarms[i]->heap_index = i;
  heap->alarm_count--;
  maybe_shrink(heap);
  note_changed_priority(heap, heap->alarms[i]);
}

int grpc_alarm_heap_is_empty(grpc_alarm_heap *heap) {
  return heap->alarm_count == 0;
}

grpc_alarm *grpc_alarm_heap_top(grpc_alarm_heap *heap) {
  return heap->alarms[0];
}

void grpc_alarm_heap_pop(grpc_alarm_heap *heap) {
  grpc_alarm_heap_remove(heap, grpc_alarm_heap_top(heap));
}
