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
  if (backing_buffer_.count == 0) {
    grpc_slice_buffer_add_indexed(&backing_buffer_, grpc_empty_slice());
    GPR_ASSERT(backing_buffer_.count > 0);
  }
}

SliceBufferByteStream::~SliceBufferByteStream() {}

void SliceBufferByteStream::Orphan() {
  grpc_slice_buffer_destroy_internal(&backing_buffer_);
  // Note: We do not actually delete the object here, since
  // SliceBufferByteStream is usually allocated as part of a larger
  // object and has an OrphanablePtr of itself passed down through the
  // filter stack.
}

Poll<absl::StatusOr<Slice>> SliceBufferByteStream::PollNext(
    size_t /*max_size_hint*/) {
  GPR_DEBUG_ASSERT(backing_buffer_.count > 0);
  return Slice(grpc_slice_buffer_take_first(&backing_buffer_));
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
  // Note: We do not actually delete the object here, since
  // CachingByteStream is usually allocated as part of a larger
  // object and has an OrphanablePtr of itself passed down through the
  // filter stack.
}

Poll<absl::StatusOr<Slice>> ByteStreamCache::CachingByteStream::PollNext(
    size_t max_size_hint) {
  if (!cache_->error_.ok()) return cache_->error_;
  if (cursor_ < cache_->cache_buffer_.count) {
    Slice out(grpc_slice_ref_internal(cache_->cache_buffer_.slices[cursor_]));
    ++cursor_;
    offset_ += out.length();
    return std::move(out);
  }
  GPR_ASSERT(cache_->underlying_stream_ != nullptr);
  auto r = cache_->underlying_stream_->PollNext(max_size_hint);
  if (absl::holds_alternative<Pending>(r)) return r;
  auto& underlying_status = absl::get<absl::StatusOr<Slice>>(r);
  if (!underlying_status.ok()) {
    cache_->error_ = underlying_status.status();
    return cache_->error_;
  }
  auto& underlying_slice = *underlying_status;
  grpc_slice_buffer_add(&cache_->cache_buffer_,
                        underlying_slice.Ref().TakeCSlice());
  ++cursor_;
  offset_ += underlying_slice.length();
  return std::move(underlying_slice);
}

void ByteStreamCache::CachingByteStream::Reset() {
  cursor_ = 0;
  offset_ = 0;
}

}  // namespace grpc_core
