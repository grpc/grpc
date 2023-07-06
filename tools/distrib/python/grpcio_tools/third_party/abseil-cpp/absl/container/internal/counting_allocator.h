// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_CONTAINER_INTERNAL_COUNTING_ALLOCATOR_H_
#define ABSL_CONTAINER_INTERNAL_COUNTING_ALLOCATOR_H_

#include <cstdint>
#include <memory>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

// This is a stateful allocator, but the state lives outside of the
// allocator (in whatever test is using the allocator). This is odd
// but helps in tests where the allocator is propagated into nested
// containers - that chain of allocators uses the same state and is
// thus easier to query for aggregate allocation information.
template <typename T>
class CountingAllocator {
 public:
  using Allocator = std::allocator<T>;
  using AllocatorTraits = std::allocator_traits<Allocator>;
  using value_type = typename AllocatorTraits::value_type;
  using pointer = typename AllocatorTraits::pointer;
  using const_pointer = typename AllocatorTraits::const_pointer;
  using size_type = typename AllocatorTraits::size_type;
  using difference_type = typename AllocatorTraits::difference_type;

  CountingAllocator() = default;
  explicit CountingAllocator(int64_t* bytes_used) : bytes_used_(bytes_used) {}
  CountingAllocator(int64_t* bytes_used, int64_t* instance_count)
      : bytes_used_(bytes_used), instance_count_(instance_count) {}

  template <typename U>
  CountingAllocator(const CountingAllocator<U>& x)
      : bytes_used_(x.bytes_used_), instance_count_(x.instance_count_) {}

  pointer allocate(
      size_type n,
      typename AllocatorTraits::const_void_pointer hint = nullptr) {
    Allocator allocator;
    pointer ptr = AllocatorTraits::allocate(allocator, n, hint);
    if (bytes_used_ != nullptr) {
      *bytes_used_ += n * sizeof(T);
    }
    return ptr;
  }

  void deallocate(pointer p, size_type n) {
    Allocator allocator;
    AllocatorTraits::deallocate(allocator, p, n);
    if (bytes_used_ != nullptr) {
      *bytes_used_ -= n * sizeof(T);
    }
  }

  template <typename U, typename... Args>
  void construct(U* p, Args&&... args) {
    Allocator allocator;
    AllocatorTraits::construct(allocator, p, std::forward<Args>(args)...);
    if (instance_count_ != nullptr) {
      *instance_count_ += 1;
    }
  }

  template <typename U>
  void destroy(U* p) {
    Allocator allocator;
    // Ignore GCC warning bug.
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
#endif
    AllocatorTraits::destroy(allocator, p);
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic pop
#endif
    if (instance_count_ != nullptr) {
      *instance_count_ -= 1;
    }
  }

  template <typename U>
  class rebind {
   public:
    using other = CountingAllocator<U>;
  };

  friend bool operator==(const CountingAllocator& a,
                         const CountingAllocator& b) {
    return a.bytes_used_ == b.bytes_used_ &&
           a.instance_count_ == b.instance_count_;
  }

  friend bool operator!=(const CountingAllocator& a,
                         const CountingAllocator& b) {
    return !(a == b);
  }

  int64_t* bytes_used_ = nullptr;
  int64_t* instance_count_ = nullptr;
};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_COUNTING_ALLOCATOR_H_
