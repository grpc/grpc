/*
 *
 * Copyright 2014, Google Inc.
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

#define INITIAL_PENDING_READ_COUNT 4

static void pra_init(pending_read_array *array) {
  array->data = gpr_malloc(sizeof(pending_read) * INITIAL_PENDING_READ_COUNT);
  array->count = 0;
  array->capacity = INITIAL_PENDING_READ_COUNT;
}

static void pra_destroy(pending_read_array *array,
                        size_t finish_starting_from) {
  size_t i;
  for (i = finish_starting_from; i < array->count; i++) {
    array->data[i].on_finish(array->data[i].user_data, GRPC_OP_ERROR);
  }
  gpr_free(array->data);
}

/* Append an operation to an array, expanding as needed */
static void pra_push(pending_read_array *a, grpc_byte_buffer *buffer,
                     void (*on_finish)(void *user_data, grpc_op_error error),
                     void *user_data) {
  if (a->count == a->capacity) {
    a->capacity *= 2;
    a->data = gpr_realloc(a->data, sizeof(pending_read) * a->capacity);
  }
  a->data[a->count].byte_buffer = buffer;
  a->data[a->count].user_data = user_data;
  a->data[a->count].on_finish = on_finish;
  a->count++;
}

static void prq_init(pending_read_queue *q) {
  q->drain_pos = 0;
  pra_init(&q->filling);
  pra_init(&q->draining);
}

static void prq_destroy(pending_read_queue *q) {
  pra_destroy(&q->filling, 0);
  pra_destroy(&q->draining, q->drain_pos);
}

static int prq_is_empty(pending_read_queue *q) {
  return (q->drain_pos == q->draining.count && q->filling.count == 0);
}

static void prq_push(pending_read_queue *q, grpc_byte_buffer *buffer,
                     void (*on_finish)(void *user_data, grpc_op_error error),
                     void *user_data) {
  pra_push(&q->filling, buffer, on_finish, user_data);
}

/* Take the first queue element and move it to the completion queue. Do nothing
   if q is empty */
static int prq_pop_to_cq(pending_read_queue *q, void *tag, grpc_call *call,
                         grpc_completion_queue *cq) {
  pending_read_array temp_array;
  pending_read *pr;

  if (q->drain_pos == q->draining.count) {
    if (q->filling.count == 0) {
      return 0;
    }
    q->draining.count = 0;
    q->drain_pos = 0;
    /* swap arrays */
    temp_array = q->filling;
    q->filling = q->draining;
    q->draining = temp_array;
  }

  pr = q->draining.data + q->drain_pos;
  q->drain_pos++;
  grpc_cq_end_read(cq, tag, call, pr->on_finish, pr->user_data,
                   pr->byte_buffer);
  return 1;
}
