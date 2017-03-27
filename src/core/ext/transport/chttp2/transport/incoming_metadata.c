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
    grpc_chttp2_incoming_metadata_buffer *buffer, gpr_arena *arena) {
  buffer->arena = arena;
  grpc_metadata_batch_init(&buffer->batch);
  buffer->batch.deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
}

void grpc_chttp2_incoming_metadata_buffer_destroy(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_metadata_buffer *buffer) {
  grpc_metadata_batch_destroy(exec_ctx, &buffer->batch);
}

grpc_error *grpc_chttp2_incoming_metadata_buffer_add(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_metadata_buffer *buffer,
    grpc_mdelem elem) {
  buffer->size += GRPC_MDELEM_LENGTH(elem);
  return grpc_metadata_batch_add_tail(
      exec_ctx, &buffer->batch,
      gpr_arena_alloc(buffer->arena, sizeof(grpc_linked_mdelem)), elem);
}

grpc_error *grpc_chttp2_incoming_metadata_buffer_replace_or_add(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_metadata_buffer *buffer,
    grpc_mdelem elem) {
  for (grpc_linked_mdelem *l = buffer->batch.list.head; l != NULL;
       l = l->next) {
    if (grpc_slice_eq(GRPC_MDKEY(l->md), GRPC_MDKEY(elem))) {
      GRPC_MDELEM_UNREF(exec_ctx, l->md);
      l->md = elem;
      return GRPC_ERROR_NONE;
    }
  }
  return grpc_chttp2_incoming_metadata_buffer_add(exec_ctx, buffer, elem);
}

void grpc_chttp2_incoming_metadata_buffer_set_deadline(
    grpc_chttp2_incoming_metadata_buffer *buffer, gpr_timespec deadline) {
  buffer->batch.deadline = deadline;
}

void grpc_chttp2_incoming_metadata_buffer_publish(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_metadata_buffer *buffer,
    grpc_metadata_batch *batch) {
  *batch = buffer->batch;
  grpc_metadata_batch_init(&buffer->batch);
}
