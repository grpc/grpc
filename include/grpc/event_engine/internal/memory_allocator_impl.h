// Copyright 2021 The gRPC Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef GRPC_EVENT_ENGINE_INTERNAL_MEMORY_ALLOCATOR_IMPL_H
#define GRPC_EVENT_ENGINE_INTERNAL_MEMORY_ALLOCATOR_IMPL_H

#include <grpc/impl/codegen/port_platform.h>

#include <algorithm>
#include <memory>
#include <type_traits>
#include <vector>

#include <grpc/slice.h>

// forward-declaring an internal struct, not used publicly.
struct grpc_slice_buffer;

namespace grpc_event_engine {
namespace experimental {

/// Reservation request - how much memory do we want to allocate?
class MemoryRequest {
 public:
  /// Request a fixed amount of memory.
  // NOLINTNEXTLINE(google-explicit-constructor)
  MemoryRequest(size_t n) : min_(n), max_(n) {}
  /// Request a range of memory.
  /// Requires: \a min <= \a max.
  /// Requires: \a max <= max_size()
  MemoryRequest(size_t min, size_t max) : min_(min), max_(max) {}

  /// Maximum allowable request size - hard coded to 1GB.
  static constexpr size_t max_allowed_size() { return 1024 * 1024 * 1024; }

  /// Increase the size by \a amount.
  /// Undefined behavior if min() + amount or max() + amount overflows.
  MemoryRequest Increase(size_t amount) const {
    return MemoryRequest(min_ + amount, max_ + amount);
  }

  size_t min() const { return min_; }
  size_t max() const { return max_; }

 private:
  size_t min_;
  size_t max_;
};

namespace internal {

/// Underlying memory allocation interface.
/// This is an internal interface, not intended to be used by users.
/// Its interface is subject to change at any time.
class MemoryAllocatorImpl
    : public std::enable_shared_from_this<MemoryAllocatorImpl> {
 public:
  MemoryAllocatorImpl() {}
  virtual ~MemoryAllocatorImpl() {}

  MemoryAllocatorImpl(const MemoryAllocatorImpl&) = delete;
  MemoryAllocatorImpl& operator=(const MemoryAllocatorImpl&) = delete;

  /// Reserve bytes from the quota.
  /// If we enter overcommit, reclamation will begin concurrently.
  /// Returns the number of bytes reserved.
  /// If MemoryRequest is invalid, this function will abort.
  /// If MemoryRequest is valid, this function is infallible, and will always
  /// succeed at reserving the some number of bytes between request.min() and
  /// request.max() inclusively.
  virtual size_t Reserve(MemoryRequest request) = 0;

  /// Release some bytes that were previously reserved.
  /// If more bytes are released than were reserved, we will have undefined
  /// behavior.
  virtual void Release(size_t n) = 0;

  /// Shutdown this allocator.
  /// Further usage of Reserve() is undefined behavior.
  virtual void Shutdown() = 0;
};

}  // namespace internal

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_INTERNAL_MEMORY_ALLOCATOR_IMPL_H
