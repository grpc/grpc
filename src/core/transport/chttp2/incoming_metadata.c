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

#include "src/core/transport/chttp2/incoming_metadata.h"

#include <string.h>

#include "src/core/transport/chttp2/internal.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

void grpc_chttp2_incoming_metadata_buffer_init(
    grpc_chttp2_incoming_metadata_buffer *buffer) {
  buffer->deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
}

void grpc_chttp2_incoming_metadata_buffer_destroy(
    grpc_chttp2_incoming_metadata_buffer *buffer) {
  size_t i;
  for (i = 0; i < buffer->count; i++) {
    GRPC_MDELEM_UNREF(buffer->elems[i].md);
  }
  gpr_free(buffer->elems);
}

void grpc_chttp2_incoming_metadata_buffer_add(
    grpc_chttp2_incoming_metadata_buffer *buffer, grpc_mdelem *elem) {
  if (buffer->capacity == buffer->count) {
    buffer->capacity = GPR_MAX(8, 2 * buffer->capacity);
    buffer->elems =
        gpr_realloc(buffer->elems, sizeof(*buffer->elems) * buffer->capacity);
  }
  buffer->elems[buffer->count++].md = elem;
}

void grpc_chttp2_incoming_metadata_buffer_set_deadline(
    grpc_chttp2_incoming_metadata_buffer *buffer, gpr_timespec deadline) {
  buffer->deadline = deadline;
}

void grpc_chttp2_incoming_metadata_live_op_buffer_end(
    grpc_chttp2_incoming_metadata_live_op_buffer *buffer) {
  gpr_free(buffer->elems);
  buffer->elems = NULL;
}

void grpc_chttp2_incoming_metadata_buffer_place_metadata_batch_into(
    grpc_chttp2_incoming_metadata_buffer *buffer, grpc_stream_op_buffer *sopb) {
  grpc_metadata_batch b;

  b.list.head = NULL;
  /* Store away the last element of the list, so that in patch_metadata_ops
     we can reconstitute the list.
     We can't do list building here as later incoming metadata may reallocate
     the underlying array. */
  b.list.tail = (void *)(gpr_intptr)buffer->count;
  b.garbage.head = b.garbage.tail = NULL;
  b.deadline = buffer->deadline;
  buffer->deadline = gpr_inf_future(GPR_CLOCK_REALTIME);

  grpc_sopb_add_metadata(sopb, b);
}

void grpc_chttp2_incoming_metadata_buffer_swap(
    grpc_chttp2_incoming_metadata_buffer *a,
    grpc_chttp2_incoming_metadata_buffer *b) {
  GPR_SWAP(grpc_chttp2_incoming_metadata_buffer, *a, *b);
}

void grpc_incoming_metadata_buffer_move_to_referencing_sopb(
    grpc_chttp2_incoming_metadata_buffer *src,
    grpc_chttp2_incoming_metadata_buffer *dst, grpc_stream_op_buffer *sopb) {
  size_t delta;
  size_t i;
  dst->deadline = gpr_time_min(src->deadline, dst->deadline);

  if (src->count == 0) {
    return;
  }
  if (dst->count == 0) {
    grpc_chttp2_incoming_metadata_buffer_swap(src, dst);
    return;
  }
  delta = dst->count;
  if (dst->capacity < src->count + dst->count) {
    dst->capacity = GPR_MAX(dst->capacity * 2, src->count + dst->count);
    dst->elems = gpr_realloc(dst->elems, dst->capacity * sizeof(*dst->elems));
  }
  memcpy(dst->elems + dst->count, src->elems, src->count * sizeof(*src->elems));
  dst->count += src->count;
  for (i = 0; i < sopb->nops; i++) {
    if (sopb->ops[i].type != GRPC_OP_METADATA) continue;
    sopb->ops[i].data.metadata.list.tail =
        (void *)(delta + (gpr_uintptr)sopb->ops[i].data.metadata.list.tail);
  }
  src->count = 0;
}

void grpc_chttp2_incoming_metadata_buffer_postprocess_sopb_and_begin_live_op(
    grpc_chttp2_incoming_metadata_buffer *buffer, grpc_stream_op_buffer *sopb,
    grpc_chttp2_incoming_metadata_live_op_buffer *live_op_buffer) {
  grpc_stream_op *ops = sopb->ops;
  size_t nops = sopb->nops;
  size_t i;
  size_t j;
  size_t mdidx = 0;
  size_t last_mdidx;
  int found_metadata = 0;

  /* rework the array of metadata into a linked list, making use
     of the breadcrumbs we left in metadata batches during
     add_metadata_batch */
  for (i = 0; i < nops; i++) {
    grpc_stream_op *op = &ops[i];
    if (op->type != GRPC_OP_METADATA) continue;
    found_metadata = 1;
    /* we left a breadcrumb indicating where the end of this list is,
       and since we add sequentially, we know from the end of the last
       segment where this segment begins */
    last_mdidx = (size_t)(gpr_intptr)(op->data.metadata.list.tail);
    GPR_ASSERT(last_mdidx > mdidx);
    GPR_ASSERT(last_mdidx <= buffer->count);
    /* turn the array into a doubly linked list */
    op->data.metadata.list.head = &buffer->elems[mdidx];
    op->data.metadata.list.tail = &buffer->elems[last_mdidx - 1];
    for (j = mdidx + 1; j < last_mdidx; j++) {
      buffer->elems[j].prev = &buffer->elems[j - 1];
      buffer->elems[j - 1].next = &buffer->elems[j];
    }
    buffer->elems[mdidx].prev = NULL;
    buffer->elems[last_mdidx - 1].next = NULL;
    /* track where we're up to */
    mdidx = last_mdidx;
  }
  if (found_metadata) {
    live_op_buffer->elems = buffer->elems;
    if (mdidx != buffer->count) {
      /* we have a partially read metadata batch still in incoming_metadata */
      size_t new_count = buffer->count - mdidx;
      size_t copy_bytes = sizeof(*buffer->elems) * new_count;
      GPR_ASSERT(mdidx < buffer->count);
      buffer->elems = gpr_malloc(copy_bytes);
      memcpy(buffer->elems, live_op_buffer->elems + mdidx, copy_bytes);
      buffer->count = buffer->capacity = new_count;
    } else {
      buffer->elems = NULL;
      buffer->count = 0;
      buffer->capacity = 0;
    }
  }
}
