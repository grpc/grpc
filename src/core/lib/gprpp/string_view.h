/*
 *
 * Copyright 2019 gRPC authors.
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
#ifndef GRPC_CORE_LIB_GPRPP_STRING_VIEW_H
#define GRPC_CORE_LIB_GPRPP_STRING_VIEW_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"

namespace grpc_core {

// Provides a light-weight view over a char array or a slice, similar but not
// identical to absl::string_view.
//
// StringView does not own the buffers that back the view. Callers must ensure
// the buffer stays around while the StringView is accessible.
//
// The interface used here is not identical to absl::string_view. Notably, we
// need to support slices while we cannot support std::string, and gpr string
// style functions such as strdup() and cmp(). Once we switch to
// absl::string_view this class will inherit from absl::string_view and add the
// gRPC-specific APIs.
class StringView final {
 public:
  static constexpr size_t npos = std::numeric_limits<size_t>::max();

  constexpr string_view(const char* ptr, size_t size)
      : ptr_(ptr), size_(size) {}
  constexpr string_view(const char* ptr)
      : string_view(ptr, ptr == nullptr ? 0 : strlen(ptr)) {}
  string_view(const grpc_slice& slice)
      : string_view(reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(slice)),
                    GRPC_SLICE_LENGTH(slice)) {}
  constexpr string_view() : string_view(nullptr, 0) {}

  constexpr const char* data() const { return ptr_; }
  constexpr size_t size() const { return size_; }
  constexpr bool empty() const { return size_ == 0; }

  string_view substr(size_t start, size_t size = npos) {
    GPR_DEBUG_ASSERT(start + size <= size_);
    return string_view(ptr_ + start, std::min(size, size_ - start));
  }

  constexpr const char& operator[](size_t i) const { return ptr_[i]; }

  const char& front() const { return ptr_[0]; }
  const char& back() const { return ptr_[size_ - 1]; }

  void remove_prefix(size_t n) {
    GPR_DEBUG_ASSERT(n <= size_);
    ptr_ += n;
    size_ -= n;
  }

  void remove_suffix(size_t n) {
    GPR_DEBUG_ASSERT(n <= size_);
    size_ -= n;
  }

  size_t find(char c, size_t pos = 0) const {
    if (empty() || pos >= size_) return npos;
    const char* result =
        static_cast<const char*>(memchr(ptr_ + pos, c, size_ - pos));
    return result != nullptr ? result - ptr_ : npos;
  }

  void clear() {
    ptr_ = nullptr;
    size_ = 0;
  }

  // Creates a dup of the string viewed by this class.
  // Return value is null-terminated and never nullptr.
  grpc_core::UniquePtr<char> dup() const {
    char* str = static_cast<char*>(gpr_malloc(size_ + 1));
    if (size_ > 0) memcpy(str, ptr_, size_);
    str[size_] = '\0';
    return grpc_core::UniquePtr<char>(str);
  }

  int cmp(string_view other) const {
    return strncmp(data(), other.data(), GPR_MIN(size(), other.size()));
  }

 private:
  const char* ptr_;
  size_t size_;
};

inline bool operator==(string_view lhs, string_view rhs) {
  return lhs.size() == rhs.size() &&
         strncmp(lhs.data(), rhs.data(), lhs.size()) == 0;
}

inline bool operator!=(string_view lhs, string_view rhs) {
  return !(lhs == rhs);
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_STRING_VIEW_H */
