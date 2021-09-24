/*
 *
 * Copyright 2021 gRPC authors.
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

#ifndef GRPC_EVENT_ENGINE_SLICE_H
#define GRPC_EVENT_ENGINE_SLICE_H

#include <string>
#include <utility>

#include <grpc/slice.h>

namespace grpc_event_engine {
namespace experimental {

class SliceBuffer;

/// A wrapper around \a grpc_slice.
///
/// A slice represents a contiguous reference counted array of bytes.
/// It is cheap to take references to a slice, and it is cheap to create a
/// slice pointing to a subset of another slice.
class Slice final {
  friend class SliceBuffer;

 public:
  /// Construct an empty slice.
  Slice() : slice_(grpc_empty_slice()) {}
  /// Destructor - drops one reference.
  ~Slice() { grpc_slice_unref(slice_); }

  enum AddRef { ADD_REF };
  /// Construct a slice from \a slice, adding a reference.
  Slice(grpc_slice slice, AddRef) : slice_(grpc_slice_ref(slice)) {}

  enum StealRef { STEAL_REF };
  /// Construct a slice from \a slice, stealing a reference.
  Slice(grpc_slice slice, StealRef) : slice_(slice) {}

  /// Allocate a slice of specified size
  explicit Slice(size_t len) : slice_(grpc_slice_malloc(len)) {}

  /// Construct a slice from a copied buffer
  Slice(const void* buf, size_t len)
      : slice_(grpc_slice_from_copied_buffer(reinterpret_cast<const char*>(buf),
                                             len)) {}

  /// Construct a slice from a copied string
  /* NOLINTNEXTLINE(google-explicit-constructor) */
  Slice(const std::string& str)
      : slice_(grpc_slice_from_copied_buffer(str.c_str(), str.length())) {}

  enum StaticSlice { STATIC_SLICE };

  /// Construct a slice from a static buffer
  Slice(const void* buf, size_t len, StaticSlice)
      : slice_(grpc_slice_from_static_buffer(reinterpret_cast<const char*>(buf),
                                             len)) {}

  /// Copy constructor, adds a reference.
  Slice(const Slice& other) : slice_(grpc_slice_ref(other.slice_)) {}

  /// Assignment, reference count is unchanged.
  Slice& operator=(Slice other) {
    std::swap(slice_, other.slice_);
    return *this;
  }

  /// Create a slice pointing at some data. Calls malloc to allocate a refcount
  /// for the object, and arranges that destroy will be called with the
  /// user data pointer passed in at destruction. Can be the same as buf or
  /// different (e.g., if data is part of a larger structure that must be
  /// destroyed when the data is no longer needed)
  Slice(void* buf, size_t len, void (*destroy)(void*), void* user_data)
      : slice_(grpc_slice_new_with_user_data(buf, len, destroy, user_data)) {}

  /// Specialization of above for common case where buf == user_data
  Slice(void* buf, size_t len, void (*destroy)(void*))
      : Slice(buf, len, destroy, buf) {}

  /// Similar to the above but has a destroy that also takes slice length
  Slice(void* buf, size_t len, void (*destroy)(void*, size_t))
      : slice_(grpc_slice_new_with_len(buf, len, destroy)) {}

  /// Byte size.
  size_t size() const { return GRPC_SLICE_LENGTH(slice_); }

  /// Raw pointer to the beginning (first element) of the slice.
  const uint8_t* begin() const { return GRPC_SLICE_START_PTR(slice_); }

  /// Raw pointer to the end (one byte \em past the last element) of the slice.
  const uint8_t* end() const { return GRPC_SLICE_END_PTR(slice_); }

  /// Raw C slice. Caller needs to call grpc_slice_unref when done.
  grpc_slice c_slice() const { return grpc_slice_ref(slice_); }

 private:
  grpc_slice slice_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_SLICE_H
