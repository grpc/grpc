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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/byte_stream.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

//
// SliceBufferByteStream
//

SliceBufferByteStream::SliceBufferByteStream(grpc_slice_buffer* slice_buffer,
                                             uint32_t flags)
    : ByteStream(static_cast<uint32_t>(slice_buffer->length), flags) {
  GPR_ASSERT(slice_buffer->length <= UINT32_MAX);
  grpc_slice_buffer_init(&backing_buffer_);
  grpc_slice_buffer_swap(slice_buffer, &backing_buffer_);
}

SliceBufferByteStream::~SliceBufferByteStream() {}

void SliceBufferByteStream::Orphan() {
  grpc_slice_buffer_destroy(&backing_buffer_);
  GRPC_ERROR_UNREF(shutdown_error_);
  // Note: We do not actually delete the object here, since
  // SliceBufferByteStream is usually allocated as part of a larger
  // object and has an OrphanablePtr of itself passed down through the
  // filter stack.
}

bool SliceBufferByteStream::Next(size_t max_size_hint,
                                 grpc_closure* on_complete) {
  GPR_ASSERT(cursor_ < backing_buffer_.count);
  return true;
}

grpc_error* SliceBufferByteStream::Pull(grpc_slice* slice) {
  if (shutdown_error_ != GRPC_ERROR_NONE) {
    return GRPC_ERROR_REF(shutdown_error_);
  }
  GPR_ASSERT(cursor_ < backing_buffer_.count);
  *slice = grpc_slice_ref_internal(backing_buffer_.slices[cursor_]);
  ++cursor_;
  return GRPC_ERROR_NONE;
}

void SliceBufferByteStream::Shutdown(grpc_error* error) {
  GRPC_ERROR_UNREF(shutdown_error_);
  shutdown_error_ = error;
}

//
// ByteStreamCache
//

ByteStreamCache::ByteStreamCache(OrphanablePtr<ByteStream> underlying_stream)
    : underlying_stream_(std::move(underlying_stream)),
      length_(underlying_stream_->length()),
      flags_(underlying_stream_->flags()) {
  grpc_slice_buffer_init(&cache_buffer_);
}

ByteStreamCache::~ByteStreamCache() { Destroy(); }

void ByteStreamCache::Destroy() {
  underlying_stream_.reset();
  if (cache_buffer_.length > 0) {
    grpc_slice_buffer_destroy_internal(&cache_buffer_);
  }
}

//
// ByteStreamCache::CachingByteStream
//

ByteStreamCache::CachingByteStream::CachingByteStream(ByteStreamCache* cache)
    : ByteStream(cache->length_, cache->flags_), cache_(cache) {}

ByteStreamCache::CachingByteStream::~CachingByteStream() {}

void ByteStreamCache::CachingByteStream::Orphan() {
  GRPC_ERROR_UNREF(shutdown_error_);
  // Note: We do not actually delete the object here, since
  // CachingByteStream is usually allocated as part of a larger
  // object and has an OrphanablePtr of itself passed down through the
  // filter stack.
}

bool ByteStreamCache::CachingByteStream::Next(size_t max_size_hint,
                                              grpc_closure* on_complete) {
  if (shutdown_error_ != GRPC_ERROR_NONE) return true;
  if (cursor_ < cache_->cache_buffer_.count) return true;
  GPR_ASSERT(cache_->underlying_stream_ != nullptr);
  return cache_->underlying_stream_->Next(max_size_hint, on_complete);
}

grpc_error* ByteStreamCache::CachingByteStream::Pull(grpc_slice* slice) {
  if (shutdown_error_ != GRPC_ERROR_NONE) {
    return GRPC_ERROR_REF(shutdown_error_);
  }
  if (cursor_ < cache_->cache_buffer_.count) {
    *slice = grpc_slice_ref_internal(cache_->cache_buffer_.slices[cursor_]);
    ++cursor_;
    offset_ += GRPC_SLICE_LENGTH(*slice);
    return GRPC_ERROR_NONE;
  }
  GPR_ASSERT(cache_->underlying_stream_ != nullptr);
  grpc_error* error = cache_->underlying_stream_->Pull(slice);
  if (error == GRPC_ERROR_NONE) {
    grpc_slice_buffer_add(&cache_->cache_buffer_,
                          grpc_slice_ref_internal(*slice));
    ++cursor_;
    offset_ += GRPC_SLICE_LENGTH(*slice);
    // Orphan the underlying stream if it's been drained.
    if (offset_ == cache_->underlying_stream_->length()) {
      cache_->underlying_stream_.reset();
    }
  }
  return error;
}

void ByteStreamCache::CachingByteStream::Shutdown(grpc_error* error) {
  GRPC_ERROR_UNREF(shutdown_error_);
  shutdown_error_ = GRPC_ERROR_REF(error);
  if (cache_->underlying_stream_ != nullptr) {
    cache_->underlying_stream_->Shutdown(error);
  }
}

void ByteStreamCache::CachingByteStream::Reset() {
  cursor_ = 0;
  offset_ = 0;
}

}  // namespace grpc_core
