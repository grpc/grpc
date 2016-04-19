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

#ifndef GRPC_CORE_LIB_IOMGR_TIMER_HEAP_H
#define GRPC_CORE_LIB_IOMGR_TIMER_HEAP_H

#include "src/core/lib/iomgr/timer.h"

typedef struct {
  grpc_timer **timers;
  uint32_t timer_count;
  uint32_t timer_capacity;
} grpc_timer_heap;

/* return 1 if the new timer is the first timer in the heap */
int grpc_timer_heap_add(grpc_timer_heap *heap, grpc_timer *timer);

void grpc_timer_heap_init(grpc_timer_heap *heap);
void grpc_timer_heap_destroy(grpc_timer_heap *heap);

void grpc_timer_heap_remove(grpc_timer_heap *heap, grpc_timer *timer);
grpc_timer *grpc_timer_heap_top(grpc_timer_heap *heap);
void grpc_timer_heap_pop(grpc_timer_heap *heap);

int grpc_timer_heap_is_empty(grpc_timer_heap *heap);

#endif /* GRPC_CORE_LIB_IOMGR_TIMER_HEAP_H */
