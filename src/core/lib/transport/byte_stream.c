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

#include "src/core/lib/transport/byte_stream.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/support/log.h>

bool grpc_byte_stream_next_slice(grpc_exec_ctx *exec_ctx,
                                 grpc_byte_stream *byte_stream,
                                 gpr_slice *slice, size_t max_size_hint,
                                 grpc_closure *on_complete) {
  return byte_stream->next_slice(exec_ctx, byte_stream, slice, max_size_hint,
                                 on_complete);
}

bool grpc_byte_stream_next_buffer(grpc_exec_ctx *exec_ctx,
                                  grpc_byte_stream *byte_stream, void *buffer,
                                  size_t size, grpc_closure *on_complete) {
  return byte_stream->next_buffer(exec_ctx, byte_stream, buffer, size,
                                  on_complete);
}

void grpc_byte_stream_destroy(grpc_exec_ctx *exec_ctx,
                              grpc_byte_stream *byte_stream) {
  byte_stream->destroy(exec_ctx, byte_stream);
}

/* slice_buffer_stream */

static bool slice_buffer_stream_next_slice(grpc_exec_ctx *exec_ctx,
                                           grpc_byte_stream *byte_stream,
                                           gpr_slice *slice,
                                           size_t max_size_hint,
                                           grpc_closure *on_complete) {
  grpc_slice_buffer_stream *stream = (grpc_slice_buffer_stream *)byte_stream;
  GPR_ASSERT(stream->cursor < stream->backing_buffer->count);
  *slice = gpr_slice_ref(stream->backing_buffer->slices[stream->cursor]);
  stream->cursor++;
  return true;
}

static bool slice_buffer_stream_next_buffer(grpc_exec_ctx *exec_ctx,
                                            grpc_byte_stream *byte_stream,
                                            void *buffer, size_t size,
                                            grpc_closure *on_complete) {
  grpc_slice_buffer_stream *stream = (grpc_slice_buffer_stream *)byte_stream;
  uint8_t *out = buffer;
  size_t remaining = size;
  for (;;) {
    GPR_ASSERT(stream->cursor < stream->backing_buffer->count);
    gpr_slice cur = stream->backing_buffer->slices[stream->cursor];
    if (GPR_SLICE_LENGTH(cur) > remaining) {
      memcpy(out, GPR_SLICE_START_PTR(cur), remaining);
      stream->backing_buffer->slices[stream->cursor] =
          gpr_slice_sub_no_ref(cur, remaining, GPR_SLICE_LENGTH(cur));
      return true;
    } else if (GPR_SLICE_LENGTH(cur) == remaining) {
      memcpy(out, GPR_SLICE_START_PTR(cur), remaining);
      stream->cursor++;
      return true;
    } else {
      memcpy(out, GPR_SLICE_START_PTR(cur), GPR_SLICE_LENGTH(cur));
      out += GPR_SLICE_LENGTH(cur);
      remaining -= GPR_SLICE_LENGTH(cur);
    }
  }
}

static void slice_buffer_stream_destroy(grpc_exec_ctx *exec_ctx,
                                        grpc_byte_stream *byte_stream) {}

void grpc_slice_buffer_stream_init(grpc_slice_buffer_stream *stream,
                                   gpr_slice_buffer *slice_buffer,
                                   uint32_t flags) {
  GPR_ASSERT(slice_buffer->length <= UINT32_MAX);
  stream->base.length = (uint32_t)slice_buffer->length;
  stream->base.flags = flags;
  stream->base.next_slice = slice_buffer_stream_next_slice;
  stream->base.next_buffer = slice_buffer_stream_next_buffer;
  stream->base.destroy = slice_buffer_stream_destroy;
  stream->backing_buffer = slice_buffer;
  stream->cursor = 0;
}
