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
#include <string.h>

#include <grpc/support/log.h>

#include "src/core/lib/slice/slice_internal.h"

bool grpc_byte_stream_next(grpc_byte_stream* byte_stream, size_t max_size_hint,
                           grpc_closure* on_complete) {
  return byte_stream->vtable->next(byte_stream, max_size_hint, on_complete);
}

grpc_error* grpc_byte_stream_pull(grpc_byte_stream* byte_stream,
                                  grpc_slice* slice) {
  return byte_stream->vtable->pull(byte_stream, slice);
}

void grpc_byte_stream_shutdown(grpc_byte_stream* byte_stream,
                               grpc_error* error) {
  byte_stream->vtable->shutdown(byte_stream, error);
}

void grpc_byte_stream_destroy(grpc_byte_stream* byte_stream) {
  byte_stream->vtable->destroy(byte_stream);
}

// grpc_slice_buffer_stream

static bool slice_buffer_stream_next(grpc_byte_stream* byte_stream,
                                     size_t max_size_hint,
                                     grpc_closure* on_complete) {
  grpc_slice_buffer_stream* stream = (grpc_slice_buffer_stream*)byte_stream;
  GPR_ASSERT(stream->cursor < stream->backing_buffer->count);
  return true;
}

static grpc_error* slice_buffer_stream_pull(grpc_byte_stream* byte_stream,
                                            grpc_slice* slice) {
  grpc_slice_buffer_stream* stream = (grpc_slice_buffer_stream*)byte_stream;
  if (stream->shutdown_error != GRPC_ERROR_NONE) {
    return GRPC_ERROR_REF(stream->shutdown_error);
  }
  GPR_ASSERT(stream->cursor < stream->backing_buffer->count);
  *slice =
      grpc_slice_ref_internal(stream->backing_buffer->slices[stream->cursor]);
  stream->cursor++;
  return GRPC_ERROR_NONE;
}

static void slice_buffer_stream_shutdown(grpc_byte_stream* byte_stream,
                                         grpc_error* error) {
  grpc_slice_buffer_stream* stream = (grpc_slice_buffer_stream*)byte_stream;
  GRPC_ERROR_UNREF(stream->shutdown_error);
  stream->shutdown_error = error;
}

static void slice_buffer_stream_destroy(grpc_byte_stream* byte_stream) {
  grpc_slice_buffer_stream* stream = (grpc_slice_buffer_stream*)byte_stream;
  grpc_slice_buffer_reset_and_unref_internal(stream->backing_buffer);
  GRPC_ERROR_UNREF(stream->shutdown_error);
}

static const grpc_byte_stream_vtable slice_buffer_stream_vtable = {
    slice_buffer_stream_next, slice_buffer_stream_pull,
    slice_buffer_stream_shutdown, slice_buffer_stream_destroy};

void grpc_slice_buffer_stream_init(grpc_slice_buffer_stream* stream,
                                   grpc_slice_buffer* slice_buffer,
                                   uint32_t flags) {
  GPR_ASSERT(slice_buffer->length <= UINT32_MAX);
  stream->base.length = (uint32_t)slice_buffer->length;
  stream->base.flags = flags;
  stream->base.vtable = &slice_buffer_stream_vtable;
  stream->backing_buffer = slice_buffer;
  stream->cursor = 0;
  stream->shutdown_error = GRPC_ERROR_NONE;
}

// grpc_caching_byte_stream

void grpc_byte_stream_cache_init(grpc_byte_stream_cache* cache,
                                 grpc_byte_stream* underlying_stream) {
  cache->underlying_stream = underlying_stream;
  grpc_slice_buffer_init(&cache->cache_buffer);
}

void grpc_byte_stream_cache_destroy(grpc_byte_stream_cache* cache) {
  grpc_byte_stream_destroy(cache->underlying_stream);
  grpc_slice_buffer_destroy_internal(&cache->cache_buffer);
}

static bool caching_byte_stream_next(grpc_byte_stream* byte_stream,
                                     size_t max_size_hint,
                                     grpc_closure* on_complete) {
  grpc_caching_byte_stream* stream = (grpc_caching_byte_stream*)byte_stream;
  if (stream->shutdown_error != GRPC_ERROR_NONE) return true;
  if (stream->cursor < stream->cache->cache_buffer.count) return true;
  return grpc_byte_stream_next(stream->cache->underlying_stream, max_size_hint,
                               on_complete);
}

static grpc_error* caching_byte_stream_pull(grpc_byte_stream* byte_stream,
                                            grpc_slice* slice) {
  grpc_caching_byte_stream* stream = (grpc_caching_byte_stream*)byte_stream;
  if (stream->shutdown_error != GRPC_ERROR_NONE) {
    return GRPC_ERROR_REF(stream->shutdown_error);
  }
  if (stream->cursor < stream->cache->cache_buffer.count) {
    *slice = grpc_slice_ref_internal(
        stream->cache->cache_buffer.slices[stream->cursor]);
    ++stream->cursor;
    return GRPC_ERROR_NONE;
  }
  grpc_error* error =
      grpc_byte_stream_pull(stream->cache->underlying_stream, slice);
  if (error == GRPC_ERROR_NONE) {
    ++stream->cursor;
    grpc_slice_buffer_add(&stream->cache->cache_buffer,
                          grpc_slice_ref_internal(*slice));
  }
  return error;
}

static void caching_byte_stream_shutdown(grpc_byte_stream* byte_stream,
                                         grpc_error* error) {
  grpc_caching_byte_stream* stream = (grpc_caching_byte_stream*)byte_stream;
  GRPC_ERROR_UNREF(stream->shutdown_error);
  stream->shutdown_error = GRPC_ERROR_REF(error);
  grpc_byte_stream_shutdown(stream->cache->underlying_stream, error);
}

static void caching_byte_stream_destroy(grpc_byte_stream* byte_stream) {
  grpc_caching_byte_stream* stream = (grpc_caching_byte_stream*)byte_stream;
  GRPC_ERROR_UNREF(stream->shutdown_error);
}

static const grpc_byte_stream_vtable caching_byte_stream_vtable = {
    caching_byte_stream_next, caching_byte_stream_pull,
    caching_byte_stream_shutdown, caching_byte_stream_destroy};

void grpc_caching_byte_stream_init(grpc_caching_byte_stream* stream,
                                   grpc_byte_stream_cache* cache) {
  memset(stream, 0, sizeof(*stream));
  stream->base.length = cache->underlying_stream->length;
  stream->base.flags = cache->underlying_stream->flags;
  stream->base.vtable = &caching_byte_stream_vtable;
  stream->cache = cache;
  stream->shutdown_error = GRPC_ERROR_NONE;
}

void grpc_caching_byte_stream_reset(grpc_caching_byte_stream* stream) {
  stream->cursor = 0;
}
