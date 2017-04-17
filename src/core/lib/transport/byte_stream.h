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

/* grpc_multi_attempt_byte_stream -- a grpc_byte_stream that wraps an
 * underlying byte stream but caches the resulting slices in a slice
 * buffer.  If an initial attempt fails without fully draining the
 * underlying stream, a new multi-attempt stream can be created from the
 * same underlying cache, in which case it will return whatever is in the
 * backing buffer before continuing to read the underlying stream.
 *
 * NOTE: No synchronization is done, so it is not safe to have multiple
 * multi-attempt streams simultaneously drawing from the same underlying
 * cache at the same time.
 */

typedef struct {
  grpc_byte_stream *underlying_stream;
  uint32_t length;
  uint32_t flags;
  grpc_slice_buffer cache_buffer;
} grpc_multi_attempt_byte_stream_cache;

void grpc_multi_attempt_byte_stream_cache_init(
    grpc_multi_attempt_byte_stream_cache *cache,
    grpc_byte_stream *underlying_stream);

/* Must not be called while still in use by a grpc_multi_attempt_byte_stream. */
void grpc_multi_attempt_byte_stream_cache_destroy(
    grpc_exec_ctx *exec_ctx, grpc_multi_attempt_byte_stream_cache *cache);

typedef struct {
  grpc_byte_stream base;
  grpc_multi_attempt_byte_stream_cache *cache;
  size_t cursor;
} grpc_multi_attempt_byte_stream;

void grpc_multi_attempt_byte_stream_init(
    grpc_multi_attempt_byte_stream *stream,
    grpc_multi_attempt_byte_stream_cache *cache);

#endif /* GRPC_CORE_LIB_TRANSPORT_BYTE_STREAM_H */
