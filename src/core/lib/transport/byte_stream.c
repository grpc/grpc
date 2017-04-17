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

#include "src/core/lib/slice/slice_internal.h"

int grpc_byte_stream_next(grpc_exec_ctx *exec_ctx,
                          grpc_byte_stream *byte_stream, size_t max_size_hint,
                          grpc_closure *on_complete) {
  return byte_stream->next(exec_ctx, byte_stream, max_size_hint, on_complete);
}

grpc_error *grpc_byte_stream_pull(grpc_exec_ctx *exec_ctx,
                                  grpc_byte_stream *byte_stream,
                                  grpc_slice *slice) {
  return byte_stream->pull(exec_ctx, byte_stream, slice);
}

void grpc_byte_stream_destroy(grpc_exec_ctx *exec_ctx,
                              grpc_byte_stream *byte_stream) {
  byte_stream->destroy(exec_ctx, byte_stream);
}

/* slice_buffer_stream */

static bool slice_buffer_stream_next(grpc_exec_ctx *exec_ctx,
                                     grpc_byte_stream *byte_stream,
                                     size_t max_size_hint,
                                     grpc_closure *on_complete) {
  grpc_slice_buffer_stream *stream = (grpc_slice_buffer_stream *)byte_stream;
  GPR_ASSERT(stream->cursor < stream->backing_buffer->count);
  return true;
}

static grpc_error *slice_buffer_stream_pull(grpc_exec_ctx *exec_ctx,
                                            grpc_byte_stream *byte_stream,
                                            grpc_slice *slice) {
  grpc_slice_buffer_stream *stream = (grpc_slice_buffer_stream *)byte_stream;
  GPR_ASSERT(stream->cursor < stream->backing_buffer->count);
  *slice =
      grpc_slice_ref_internal(stream->backing_buffer->slices[stream->cursor]);
  stream->cursor++;
  return GRPC_ERROR_NONE;
}

static void slice_buffer_stream_destroy(grpc_exec_ctx *exec_ctx,
                                        grpc_byte_stream *byte_stream) {}

void grpc_slice_buffer_stream_init(grpc_slice_buffer_stream *stream,
                                   grpc_slice_buffer *slice_buffer,
                                   uint32_t flags) {
  GPR_ASSERT(slice_buffer->length <= UINT32_MAX);
  stream->base.length = (uint32_t)slice_buffer->length;
  stream->base.flags = flags;
  stream->base.next = slice_buffer_stream_next;
  stream->base.pull = slice_buffer_stream_pull;
  stream->base.destroy = slice_buffer_stream_destroy;
  stream->backing_buffer = slice_buffer;
  stream->cursor = 0;
}

/* grpc_tee_byte_stream */

static void tee_byte_stream_on_complete(grpc_exec_ctx *exec_ctx, void *arg,
                                        grpc_error *error) {
  grpc_tee_byte_stream *stream = arg;
  stream->bytes_read += GRPC_SLICE_LENGTH(*stream->slice);
  stream->cb(exec_ctx, stream->cb_arg, *stream->slice);
  grpc_closure_run(exec_ctx, stream->original_on_complete, error);
}

static int tee_byte_stream_next(grpc_exec_ctx *exec_ctx,
                                grpc_byte_stream *byte_stream,
                                grpc_slice *slice, size_t max_size_hint,
                                grpc_closure *on_complete) {
  grpc_tee_byte_stream *stream = (grpc_tee_byte_stream *)byte_stream;
  stream->slice = slice;
  stream->original_on_complete = on_complete;
  int retval = grpc_byte_stream_next(exec_ctx, stream->underlying_stream, slice,
                                     max_size_hint, &stream->on_complete);
  if (retval) {
    stream->bytes_read += GRPC_SLICE_LENGTH(*stream->slice);
    stream->cb(exec_ctx, stream->cb_arg, *slice);
  }
  return retval;
}

static void tee_byte_stream_destroy(grpc_exec_ctx *exec_ctx,
                                    grpc_byte_stream *byte_stream) {
  grpc_tee_byte_stream *stream = (grpc_tee_byte_stream *)byte_stream;
  stream->destroy_cb(exec_ctx, stream->cb_arg, stream->bytes_read,
                     stream->underlying_stream);
}

void grpc_tee_byte_stream_init(grpc_tee_byte_stream *stream,
                               grpc_byte_stream *underlying_stream,
                               tee_byte_stream_cb cb,
                               tee_byte_stream_destroy_cb destroy_cb,
                               void *cb_arg) {
  memset(stream, 0, sizeof(*stream));
  stream->base.length = underlying_stream->length;
  stream->base.flags = underlying_stream->flags;
  stream->base.next = tee_byte_stream_next;
  stream->base.destroy = tee_byte_stream_destroy;
  stream->underlying_stream = underlying_stream;
  stream->cb = cb;
  stream->destroy_cb = destroy_cb;
  stream->cb_arg = cb_arg;
  grpc_closure_init(&stream->on_complete, tee_byte_stream_on_complete, stream,
                    grpc_schedule_on_exec_ctx);
}
