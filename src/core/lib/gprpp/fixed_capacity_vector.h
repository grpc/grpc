/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPRPP_FIXED_CAPACITY_VECTOR_H
#define GRPC_CORE_LIB_GPRPP_FIXED_CAPACITY_VECTOR_H

#include <grpc/support/port_platform.h>

#include <cassert>

#include "src/core/lib/gprpp/memory.h"

namespace grpc_core {

// This is similar to std::vector<>, except that its capacity is fixed at
// construction time.  This allows allocating the entire vector in a
// single memory allocation.

// TODO(roth): This interface does not currently implement all methods
// supported by STL containers (e.g., iterators).  Those can be added as
// needed.

template <typename T>
class FixedCapacityVector {
 public:
  static UniquePtr<FixedCapacityVector> Create(size_t capacity) {
    // Allocate space for both this object and the elements.
    FixedCapacityVector* v = static_cast<FixedCapacityVector*>(
        gpr_malloc(padded_allocation_size() + (capacity * sizeof(T))));
    new (v) FixedCapacityVector(capacity);
    return UniquePtr<FixedCapacityVector>(v);
  }

  ~FixedCapacityVector() { destroy_elements(); }

  // Not copyable.
  FixedCapacityVector(const FixedCapacityVector&) = delete;
  FixedCapacityVector& operator=(const FixedCapacityVector&) = delete;

  T& operator[](size_t offset) {
    assert(offset < size_);
    return data()[offset];
  }

  const T& operator[](size_t offset) const {
    assert(offset < size_);
    return data()[offset];
  }

  template <typename... Args>
  void emplace_back(Args&&... args) {
    assert(size_ < capacity_);
    new (data() + size_) T(std::forward<Args>(args)...);
    ++size_;
  }

  void push_back(const T& value) { emplace_back(value); }

  void push_back(T&& value) { emplace_back(std::move(value)); }

  size_t size() const { return size_; }

  size_t capacity() const { return capacity_; }

  void clear() {
    destroy_elements();
    size_ = 0;
  }

 private:
  explicit FixedCapacityVector(size_t capacity)
      : capacity_(capacity), size_(0) {}

  // Returns the size of the FixedCapacityVector object, padded up to
  // the alignment of T.  This is the offset from the object's address
  // at which we store the elements.
  static size_t padded_allocation_size() {
    return (sizeof(FixedCapacityVector) + alignof(T) - 1u) & ~(alignof(T) - 1u);
  }

  // Returns a pointer to the array of elements.
  // Defined for both const and non-const.
  T* data() {
    return reinterpret_cast<T*>(reinterpret_cast<char*>(this) +
                                padded_allocation_size());
  }
  const T* data() const {
    return reinterpret_cast<const T*>(reinterpret_cast<const char*>(this) +
                                      padded_allocation_size());
  }

  void destroy_elements() {
    for (size_t i = 0; i < size_; ++i) {
      T& value = data()[i];
      value.~T();
    }
  }

  const size_t capacity_;
  size_t size_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_FIXED_CAPACITY_VECTOR_H */
