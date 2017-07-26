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

#ifndef GRPC_CORE_LIB_TRANSPORT_BYTE_STREAM_H
#define GRPC_CORE_LIB_TRANSPORT_BYTE_STREAM_H

#include <grpc/slice_buffer.h>
#include "src/core/lib/iomgr/exec_ctx.h"

/** Internal bit flag for grpc_begin_message's \a flags signaling the use of
 * compression for the message */
#define GRPC_WRITE_INTERNAL_COMPRESS (0x80000000u)
/** Mask of all valid internal flags. */
#define GRPC_WRITE_INTERNAL_USED_MASK (GRPC_WRITE_INTERNAL_COMPRESS)

struct grpc_byte_stream;
typedef struct grpc_byte_stream grpc_byte_stream;

struct grpc_byte_stream {
  uint32_t length;
  uint32_t flags;
  bool (*next)(grpc_exec_ctx *exec_ctx, grpc_byte_stream *byte_stream,
               size_t max_size_hint, grpc_closure *on_complete);
  grpc_error *(*pull)(grpc_exec_ctx *exec_ctx, grpc_byte_stream *byte_stream,
                      grpc_slice *slice);
  void (*destroy)(grpc_exec_ctx *exec_ctx, grpc_byte_stream *byte_stream);
};

/* returns true if the bytes are available immediately (in which case
 * on_complete will not be called), false if the bytes will be available
 * asynchronously.
 *
 * max_size_hint can be set as a hint as to the maximum number
 * of bytes that would be acceptable to read.
 */
bool grpc_byte_stream_next(grpc_exec_ctx *exec_ctx,
                           grpc_byte_stream *byte_stream, size_t max_size_hint,
                           grpc_closure *on_complete);

/* returns the next slice in the byte stream when it is ready (indicated by
 * either grpc_byte_stream_next returning true or on_complete passed to
 * grpc_byte_stream_next is called).
 *
 * once a slice is returned into *slice, it is owned by the caller.
 */
grpc_error *grpc_byte_stream_pull(grpc_exec_ctx *exec_ctx,
                                  grpc_byte_stream *byte_stream,
                                  grpc_slice *slice);

void grpc_byte_stream_destroy(grpc_exec_ctx *exec_ctx,
                              grpc_byte_stream *byte_stream);

/* grpc_slice_buffer_stream -- a grpc_byte_stream that wraps a slice buffer */

typedef struct grpc_slice_buffer_stream {
  grpc_byte_stream base;
  grpc_slice_buffer *backing_buffer;
  size_t cursor;
} grpc_slice_buffer_stream;

void grpc_slice_buffer_stream_init(grpc_slice_buffer_stream *stream,
                                   grpc_slice_buffer *slice_buffer,
                                   uint32_t flags);

/* grpc_tee_byte_stream -- a grpc_byte_stream that wraps an underlying byte
 * stream, but invokes a callback for each slice returned via next(). */

/* Callback does NOT take ownership of \a slice. */
typedef void (*tee_byte_stream_cb)(grpc_exec_ctx *exec_ctx, void *arg,
                                   grpc_slice slice);
/* Callback is responsible for destroying \a underlying_stream. */
typedef void (*tee_byte_stream_destroy_cb)(grpc_exec_ctx *exec_ctx, void *arg,
                                           size_t bytes_read,
                                           grpc_byte_stream *underlying_stream);

typedef struct {
  grpc_byte_stream base;
  grpc_byte_stream *underlying_stream;
  tee_byte_stream_cb cb;
  tee_byte_stream_destroy_cb destroy_cb;
  void *cb_arg;
  size_t bytes_read;
} grpc_tee_byte_stream;

/* If \a destroy_cb is not NULL, it will be called when the stream is
 * destroyed.  This gives the caller an opportunity to finish draining
 * the underlying stream if a call fails in the middle of a write.
 * \a cb_arg is passed to both \a cb and \a destroy_cb.
 */
void grpc_tee_byte_stream_init(grpc_tee_byte_stream *stream,
                               grpc_byte_stream *underlying_stream,
                               tee_byte_stream_cb cb,
                               tee_byte_stream_destroy_cb destroy_cb,
                               void *cb_arg);

#endif /* GRPC_CORE_LIB_TRANSPORT_BYTE_STREAM_H */
