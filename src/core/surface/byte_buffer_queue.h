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

#ifndef GRPC_INTERNAL_CORE_SURFACE_BYTE_BUFFER_QUEUE_H
#define GRPC_INTERNAL_CORE_SURFACE_BYTE_BUFFER_QUEUE_H

#include <grpc/byte_buffer.h>

/* TODO(ctiller): inline an element or two into this struct to avoid per-call
                  allocations */
typedef struct {
  grpc_byte_buffer **data;
  size_t count;
  size_t capacity;
} grpc_bbq_array;

/* should be initialized by zeroing memory */
typedef struct {
  size_t drain_pos;
  grpc_bbq_array filling;
  grpc_bbq_array draining;
  size_t bytes;
} grpc_byte_buffer_queue;

void grpc_bbq_destroy(grpc_byte_buffer_queue *q);
grpc_byte_buffer *grpc_bbq_pop(grpc_byte_buffer_queue *q);
void grpc_bbq_flush(grpc_byte_buffer_queue *q);
int grpc_bbq_empty(grpc_byte_buffer_queue *q);
void grpc_bbq_push(grpc_byte_buffer_queue *q, grpc_byte_buffer *bb);
size_t grpc_bbq_bytes(grpc_byte_buffer_queue *q);

#endif  /* GRPC_INTERNAL_CORE_SURFACE_BYTE_BUFFER_QUEUE_H */
