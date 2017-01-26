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

#include "src/core/ext/transport/chttp2/transport/incoming_metadata.h"

#include <string.h>

#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

void grpc_chttp2_incoming_metadata_buffer_init(
    grpc_chttp2_incoming_metadata_buffer *buffer) {
  buffer->deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
}

void grpc_chttp2_incoming_metadata_buffer_destroy(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_metadata_buffer *buffer) {
  size_t i;
  if (!buffer->published) {
    for (i = 0; i < buffer->count; i++) {
      GRPC_MDELEM_UNREF(exec_ctx, buffer->elems[i].md);
    }
  }
  gpr_free(buffer->elems);
}

void grpc_chttp2_incoming_metadata_buffer_add(
    grpc_chttp2_incoming_metadata_buffer *buffer, grpc_mdelem elem) {
  GPR_ASSERT(!buffer->published);
  if (buffer->capacity == buffer->count) {
    buffer->capacity = GPR_MAX(8, 2 * buffer->capacity);
    buffer->elems =
        gpr_realloc(buffer->elems, sizeof(*buffer->elems) * buffer->capacity);
  }
  buffer->elems[buffer->count++].md = elem;
  buffer->size += GRPC_MDELEM_LENGTH(elem);
}

void grpc_chttp2_incoming_metadata_buffer_replace_or_add(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_metadata_buffer *buffer,
    grpc_mdelem elem) {
  for (size_t i = 0; i < buffer->count; i++) {
    if (grpc_slice_eq(GRPC_MDKEY(buffer->elems[i].md), GRPC_MDKEY(elem))) {
      GRPC_MDELEM_UNREF(exec_ctx, buffer->elems[i].md);
      buffer->elems[i].md = elem;
      return;
    }
  }
  grpc_chttp2_incoming_metadata_buffer_add(buffer, elem);
}

void grpc_chttp2_incoming_metadata_buffer_set_deadline(
    grpc_chttp2_incoming_metadata_buffer *buffer, gpr_timespec deadline) {
  GPR_ASSERT(!buffer->published);
  buffer->deadline = deadline;
}

void grpc_chttp2_incoming_metadata_buffer_publish(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_metadata_buffer *buffer,
    grpc_metadata_batch *batch) {
  GPR_ASSERT(!buffer->published);
  buffer->published = 1;
  if (buffer->count > 0) {
    size_t i;
    for (i = 0; i < buffer->count; i++) {
      /* TODO(ctiller): do something better here */
      if (!GRPC_LOG_IF_ERROR("grpc_chttp2_incoming_metadata_buffer_publish",
                             grpc_metadata_batch_link_tail(
                                 exec_ctx, batch, &buffer->elems[i]))) {
        GRPC_MDELEM_UNREF(exec_ctx, buffer->elems[i].md);
      }
    }
  } else {
    batch->list.head = batch->list.tail = NULL;
  }
  batch->deadline = buffer->deadline;
}
