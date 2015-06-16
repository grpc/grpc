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

void grpc_chttp2_incoming_metadata_buffer_init(grpc_chttp2_incoming_metadata_buffer *buffer) {
  buffer->deadline = gpr_inf_future;
}

void grpc_chttp2_incoming_metadata_buffer_destroy(grpc_chttp2_incoming_metadata_buffer *buffer) {
  gpr_free(buffer->elems);
}

void grpc_chttp2_incoming_metadata_buffer_add(grpc_chttp2_incoming_metadata_buffer *buffer,
                                  grpc_mdelem *elem) {
  if (buffer->capacity == buffer->count) {
    buffer->capacity =
        GPR_MAX(8, 2 * buffer->capacity);
    buffer->elems =
        gpr_realloc(buffer->elems,
                    sizeof(*buffer->elems) *
                        buffer->capacity);
  }
  buffer->elems[buffer->count++]
      .md = elem;
}

void grpc_chttp2_incoming_metadata_buffer_set_deadline(grpc_chttp2_incoming_metadata_buffer *buffer, gpr_timespec deadline) {
  buffer->deadline = deadline;
}

#if 0
void grpc_chttp2_parsing_add_metadata_batch(
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing) {
  grpc_metadata_batch b;

  b.list.head = NULL;
  /* Store away the last element of the list, so that in patch_metadata_ops
     we can reconstitute the list.
     We can't do list building here as later incoming metadata may reallocate
     the underlying array. */
  b.list.tail = (void *)(gpr_intptr)stream_parsing->incoming_metadata_count;
  b.garbage.head = b.garbage.tail = NULL;
  b.deadline = stream_parsing->incoming_deadline;
  stream_parsing->incoming_deadline = gpr_inf_future;

  grpc_sopb_add_metadata(&stream_parsing->data_parser.incoming_sopb, b);
}
#endif

#if 0
static void patch_metadata_ops(grpc_chttp2_stream_global *stream_global,
                               grpc_chttp2_stream_parsing *stream_parsing) {
  grpc_stream_op *ops = stream_global->incoming_sopb->ops;
  size_t nops = stream_global->incoming_sopb->nops;
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
    GPR_ASSERT(last_mdidx <= stream_parsing->incoming_metadata_count);
    /* turn the array into a doubly linked list */
    op->data.metadata.list.head = &stream_parsing->incoming_metadata[mdidx];
    op->data.metadata.list.tail =
        &stream_parsing->incoming_metadata[last_mdidx - 1];
    for (j = mdidx + 1; j < last_mdidx; j++) {
      stream_parsing->incoming_metadata[j].prev =
          &stream_parsing->incoming_metadata[j - 1];
      stream_parsing->incoming_metadata[j - 1].next =
          &stream_parsing->incoming_metadata[j];
    }
    stream_parsing->incoming_metadata[mdidx].prev = NULL;
    stream_parsing->incoming_metadata[last_mdidx - 1].next = NULL;
    /* track where we're up to */
    mdidx = last_mdidx;
  }
  if (found_metadata) {
    stream_parsing->old_incoming_metadata = stream_parsing->incoming_metadata;
    if (mdidx != stream_parsing->incoming_metadata_count) {
      /* we have a partially read metadata batch still in incoming_metadata */
      size_t new_count = stream_parsing->incoming_metadata_count - mdidx;
      size_t copy_bytes =
          sizeof(*stream_parsing->incoming_metadata) * new_count;
      GPR_ASSERT(mdidx < stream_parsing->incoming_metadata_count);
      stream_parsing->incoming_metadata = gpr_malloc(copy_bytes);
      memcpy(stream_parsing->old_incoming_metadata + mdidx,
             stream_parsing->incoming_metadata, copy_bytes);
      stream_parsing->incoming_metadata_count =
          stream_parsing->incoming_metadata_capacity = new_count;
    } else {
      stream_parsing->incoming_metadata = NULL;
      stream_parsing->incoming_metadata_count = 0;
      stream_parsing->incoming_metadata_capacity = 0;
    }
  }
}
#endif
