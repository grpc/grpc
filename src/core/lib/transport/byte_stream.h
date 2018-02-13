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

typedef struct grpc_byte_stream grpc_byte_stream;

typedef struct {
  bool (*next)(grpc_byte_stream* byte_stream, size_t max_size_hint,
               grpc_closure* on_complete);
  grpc_error* (*pull)(grpc_byte_stream* byte_stream, grpc_slice* slice);
  void (*shutdown)(grpc_byte_stream* byte_stream, grpc_error* error);
  void (*destroy)(grpc_byte_stream* byte_stream);
} grpc_byte_stream_vtable;

struct grpc_byte_stream {
  uint32_t length;
  uint32_t flags;
  const grpc_byte_stream_vtable* vtable;
};

// Returns true if the bytes are available immediately (in which case
// on_complete will not be called), false if the bytes will be available
// asynchronously.
//
// max_size_hint can be set as a hint as to the maximum number
// of bytes that would be acceptable to read.
bool grpc_byte_stream_next(grpc_byte_stream* byte_stream, size_t max_size_hint,
                           grpc_closure* on_complete);

// Returns the next slice in the byte stream when it is ready (indicated by
// either grpc_byte_stream_next returning true or on_complete passed to
// grpc_byte_stream_next is called).
//
// Once a slice is returned into *slice, it is owned by the caller.
grpc_error* grpc_byte_stream_pull(grpc_byte_stream* byte_stream,
                                  grpc_slice* slice);

// Shuts down the byte stream.
//
// If there is a pending call to on_complete from grpc_byte_stream_next(),
// it will be invoked with the error passed to grpc_byte_stream_shutdown().
//
// The next call to grpc_byte_stream_pull() (if any) will return the error
// passed to grpc_byte_stream_shutdown().
void grpc_byte_stream_shutdown(grpc_byte_stream* byte_stream,
                               grpc_error* error);

void grpc_byte_stream_destroy(grpc_byte_stream* byte_stream);

// grpc_slice_buffer_stream
//
// A grpc_byte_stream that wraps a slice buffer.  The stream takes
// ownership of the slices in the buffer, and on destruction will
// reset the contents of the buffer.

typedef struct grpc_slice_buffer_stream {
  grpc_byte_stream base;
  grpc_slice_buffer backing_buffer;
  size_t cursor;
  grpc_error* shutdown_error;
} grpc_slice_buffer_stream;

void grpc_slice_buffer_stream_init(grpc_slice_buffer_stream* stream,
                                   grpc_slice_buffer* slice_buffer,
                                   uint32_t flags);

// grpc_caching_byte_stream
//
// A grpc_byte_stream that that wraps an underlying byte stream but caches
// the resulting slices in a slice buffer.  If an initial attempt fails
// without fully draining the underlying stream, a new caching stream
// can be created from the same underlying cache, in which case it will
// return whatever is in the backing buffer before continuing to read the
// underlying stream.
//
// NOTE: No synchronization is done, so it is not safe to have multiple
// grpc_caching_byte_streams simultaneously drawing from the same underlying
// grpc_byte_stream_cache at the same time.

typedef struct {
  grpc_byte_stream* underlying_stream;
  grpc_slice_buffer cache_buffer;
} grpc_byte_stream_cache;

// Takes ownership of underlying_stream.
void grpc_byte_stream_cache_init(grpc_byte_stream_cache* cache,
                                 grpc_byte_stream* underlying_stream);

// Must not be called while still in use by a grpc_caching_byte_stream.
void grpc_byte_stream_cache_destroy(grpc_byte_stream_cache* cache);

typedef struct {
  grpc_byte_stream base;
  grpc_byte_stream_cache* cache;
  size_t cursor;
  grpc_error* shutdown_error;
} grpc_caching_byte_stream;

void grpc_caching_byte_stream_init(grpc_caching_byte_stream* stream,
                                   grpc_byte_stream_cache* cache);

// Resets the byte stream to the start of the underlying stream.
void grpc_caching_byte_stream_reset(grpc_caching_byte_stream* stream);

#endif /* GRPC_CORE_LIB_TRANSPORT_BYTE_STREAM_H */
