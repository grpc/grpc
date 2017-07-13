/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/transport/byte_stream.h"

#include <stdlib.h>

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
