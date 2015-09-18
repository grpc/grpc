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

#include "src/core/surface/byte_buffer_queue.h"
#include <grpc/support/alloc.h>
#include <grpc/support/useful.h>

static void bba_destroy(grpc_bbq_array *array, size_t start_pos) {
  size_t i;
  for (i = start_pos; i < array->count; i++) {
    grpc_byte_buffer_destroy(array->data[i]);
  }
  gpr_free(array->data);
}

/* Append an operation to an array, expanding as needed */
static void bba_push(grpc_bbq_array *a, grpc_byte_buffer *buffer) {
  if (a->count == a->capacity) {
    a->capacity = GPR_MAX(a->capacity * 2, 8);
    a->data = gpr_realloc(a->data, sizeof(grpc_byte_buffer *) * a->capacity);
  }
  a->data[a->count++] = buffer;
}

void grpc_bbq_destroy(grpc_byte_buffer_queue *q) {
  bba_destroy(&q->filling, 0);
  bba_destroy(&q->draining, q->drain_pos);
}

int grpc_bbq_empty(grpc_byte_buffer_queue *q) {
  return (q->drain_pos == q->draining.count && q->filling.count == 0);
}

void grpc_bbq_push(grpc_byte_buffer_queue *q, grpc_byte_buffer *buffer) {
  q->bytes += grpc_byte_buffer_length(buffer);
  bba_push(&q->filling, buffer);
}

void grpc_bbq_flush(grpc_byte_buffer_queue *q) {
  grpc_byte_buffer *bb;
  while ((bb = grpc_bbq_pop(q))) {
    grpc_byte_buffer_destroy(bb);
  }
}

size_t grpc_bbq_bytes(grpc_byte_buffer_queue *q) { return q->bytes; }

grpc_byte_buffer *grpc_bbq_pop(grpc_byte_buffer_queue *q) {
  grpc_bbq_array temp_array;
  grpc_byte_buffer *out;

  if (q->drain_pos == q->draining.count) {
    if (q->filling.count == 0) {
      return NULL;
    }
    q->draining.count = 0;
    q->drain_pos = 0;
    /* swap arrays */
    temp_array = q->filling;
    q->filling = q->draining;
    q->draining = temp_array;
  }

  out = q->draining.data[q->drain_pos++];
  q->bytes -= grpc_byte_buffer_length(out);
  return out;
}
