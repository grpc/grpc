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
#ifndef GRPC_EVENT_ENGINE_MEMORY_ALLOCATOR_H
#define GRPC_EVENT_ENGINE_MEMORY_ALLOCATOR_H

#include <grpc/impl/codegen/port_platform.h>

#include <stdlib.h>  // for abort()

#include <algorithm>
#include <memory>
#include <type_traits>
#include <vector>

#include <grpc/event_engine/internal/memory_allocator_impl.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>

namespace grpc_core{
class Slice;
}

namespace grpc_event_engine {
namespace experimental {

/// A Wrapper around \a grpc_slice_buffer pointer.
///
/// A slice buffer holds the memory for a collection of slices.
/// The SliceBuffer object itself is meant to only hide the C-style API,
/// and won't hold the data itself. In terms of lifespan, the
/// grpc_slice_buffer ought to be kept somewhere inside the caller's objects,
/// like a transport or an endpoint.
///
/// This lifespan rule is likely to change in the future, as we may
/// collapse the grpc_slice_buffer structure straight into this class.
///
/// The SliceBuffer API is basically a replica of the grpc_slice_buffer's,
/// and its documentation will move here once we remove the C structure,
/// which should happen before the Event Engine's API is no longer
/// an experimental API.
class SliceBuffer {
 public:
  explicit SliceBuffer(grpc_slice_buffer* slice_buffer)
      : slice_buffer_(slice_buffer) {}
  SliceBuffer(const SliceBuffer& other) = delete;
  SliceBuffer(SliceBuffer&& other) noexcept
      : slice_buffer_(other.slice_buffer_) {
    other.slice_buffer_ = nullptr;
  }
  /// Upon destruction, the underlying raw slice buffer is cleaned out and all
  /// slices are unreffed.
  ~SliceBuffer() {
    grpc_slice_buffer_destroy(slice_buffer_);
  }

  /// Adds a new slice into the SliceBuffer and makes an attempt to merge
  /// this slice with the last slice in the SliceBuffer.
  void Add(grpc_core::Slice slice);

  /// Adds a new slice into the SliceBuffer at the next available index.
  /// Returns the index at which the new slice is added.
  size_t AddIndexed(grpc_core::Slice slice);

  /// Removes the first slice in the SliceBuffer.
  void Pop() { grpc_slice_buffer_pop(slice_buffer_); }

  /// Returns the number of slices held by the SliceBuffer.
  size_t Count() { return slice_buffer_->count; }

  /// Removes/deletes the last n bytes in the SliceBuffer.
  void TrimEnd(size_t n) {
    grpc_slice_buffer_trim_end(slice_buffer_, n, nullptr);
  }

  /// Move the first n bytes of the SliceBuffer into a memory pointed to by dst.
  void MoveFirstIntoBuffer(size_t n, void* dst) {
    grpc_slice_buffer_move_first_into_buffer(slice_buffer_, n, dst);
  }

  /// Removes and unrefs all slices in the SliceBuffer.
  void Clear() { grpc_slice_buffer_reset_and_unref(slice_buffer_); }

  /// Removes the first slice in the SliceBuffer and returns it.
  grpc_core::Slice TakeFirst();

  /// Prepends the slice to the the front of the SliceBuffer.
  void UndoTakeFirst(grpc_core::Slice slice);

  /// Increased the ref-count of slice at the specified index and returns the
  /// associated slice.
  grpc_core::Slice RefSlice(size_t index);

  /// The total number of bytes held by the SliceBuffer
  size_t Length() { return slice_buffer_->length; }

  /// Return a pointer to the back raw grpc_slice_buffer
  grpc_slice_buffer* RawSliceBuffer() { return slice_buffer_; }

 private:
  /// The backing raw slice buffer.
  grpc_slice_buffer* slice_buffer_ = nullptr;
};

// Tracks memory allocated by one system.
// Is effectively a thin wrapper/smart pointer for a MemoryAllocatorImpl,
// providing a convenient and stable API.
class MemoryAllocator {
 public:
  /// Construct a MemoryAllocator given an internal::MemoryAllocatorImpl
  /// implementation. The constructed MemoryAllocator will call
  /// MemoryAllocatorImpl::Shutdown() upon destruction.
  explicit MemoryAllocator(
      std::shared_ptr<internal::MemoryAllocatorImpl> allocator)
      : allocator_(std::move(allocator)) {}
  // Construct an invalid MemoryAllocator.
  MemoryAllocator() : allocator_(nullptr) {}
  ~MemoryAllocator() {
    if (allocator_ != nullptr) allocator_->Shutdown();
  }

  MemoryAllocator(const MemoryAllocator&) = delete;
  MemoryAllocator& operator=(const MemoryAllocator&) = delete;

  MemoryAllocator(MemoryAllocator&&) = default;
  MemoryAllocator& operator=(MemoryAllocator&&) = default;

  /// Drop the underlying allocator and make this an empty object.
  /// The object will not be usable after this call unless it's a valid
  /// allocator is moved into it.
  void Reset() {
    if (allocator_ != nullptr) allocator_->Shutdown();
    allocator_.reset();
  }

  /// Reserve bytes from the quota.
  /// If we enter overcommit, reclamation will begin concurrently.
  /// Returns the number of bytes reserved.
  size_t Reserve(MemoryRequest request) { return allocator_->Reserve(request); }

  /// Release some bytes that were previously reserved.
  void Release(size_t n) { return allocator_->Release(n); }

  //
  // The remainder of this type are helper functions implemented in terms of
  // Reserve/Release.
  //

  /// An automatic releasing reservation of memory.
  class Reservation {
   public:
    Reservation() = default;
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;
    Reservation(Reservation&&) = default;
    Reservation& operator=(Reservation&&) = default;
    ~Reservation() {
      if (allocator_ != nullptr) allocator_->Release(size_);
    }

   private:
    friend class MemoryAllocator;
    Reservation(std::shared_ptr<internal::MemoryAllocatorImpl> allocator,
                size_t size)
        : allocator_(std::move(allocator)), size_(size) {}

    std::shared_ptr<internal::MemoryAllocatorImpl> allocator_;
    size_t size_ = 0;
  };

  /// Reserve bytes from the quota and automatically release them when
  /// Reservation is destroyed.
  Reservation MakeReservation(MemoryRequest request) {
    return Reservation(allocator_, Reserve(request));
  }

  /// Allocate a new object of type T, with constructor arguments.
  /// The returned type is wrapped, and upon destruction the reserved memory
  /// will be released to the allocator automatically. As such, T must have a
  /// virtual destructor so we can insert the necessary hook.
  template <typename T, typename... Args>
  typename std::enable_if<std::has_virtual_destructor<T>::value, T*>::type New(
      Args&&... args) {
    // Wrap T such that when it's destroyed, we can release memory back to the
    // allocator.
    class Wrapper final : public T {
     public:
      explicit Wrapper(std::shared_ptr<internal::MemoryAllocatorImpl> allocator,
                       Args&&... args)
          : T(std::forward<Args>(args)...), allocator_(std::move(allocator)) {}
      ~Wrapper() override { allocator_->Release(sizeof(*this)); }

     private:
      const std::shared_ptr<internal::MemoryAllocatorImpl> allocator_;
    };
    Reserve(sizeof(Wrapper));
    return new Wrapper(allocator_, std::forward<Args>(args)...);
  }

  /// Construct a unique_ptr immediately.
  template <typename T, typename... Args>
  std::unique_ptr<T> MakeUnique(Args&&... args) {
    return std::unique_ptr<T>(New<T>(std::forward<Args>(args)...));
  }

  /// Allocate a slice, using MemoryRequest to size the number of returned
  /// bytes. For a variable length request, check the returned slice length to
  /// verify how much memory was allocated. Takes care of reserving memory for
  /// any relevant control structures also.
  grpc_slice MakeSlice(MemoryRequest request);

  /// A C++ allocator for containers of T.
  template <typename T>
  class Container {
   public:
    using value_type = T;

    /// Construct the allocator: \a underlying_allocator is borrowed, and must
    /// outlive this object.
    explicit Container(MemoryAllocator* underlying_allocator)
        : underlying_allocator_(underlying_allocator) {}
    template <typename U>
    explicit Container(const Container<U>& other)
        : underlying_allocator_(other.underlying_allocator()) {}

    MemoryAllocator* underlying_allocator() const {
      return underlying_allocator_;
    }

    T* allocate(size_t n) {
      underlying_allocator_->Reserve(n * sizeof(T));
      return static_cast<T*>(::operator new(n * sizeof(T)));
    }
    void deallocate(T* p, size_t n) {
      ::operator delete(p);
      underlying_allocator_->Release(n * sizeof(T));
    }

   private:
    MemoryAllocator* underlying_allocator_;
  };

 protected:
  /// Return a pointer to the underlying implementation.
  /// Note that the interface of said implementation is unstable and likely to
  /// change at any time.
  internal::MemoryAllocatorImpl* get_internal_impl_ptr() {
    return allocator_.get();
  }

  const internal::MemoryAllocatorImpl* get_internal_impl_ptr() const {
    return allocator_.get();
  }

 private:
  std::shared_ptr<internal::MemoryAllocatorImpl> allocator_;
};

// Wrapper type around std::vector to make initialization against a
// MemoryAllocator based container allocator easy.
template <typename T>
class Vector : public std::vector<T, MemoryAllocator::Container<T>> {
 public:
  explicit Vector(MemoryAllocator* allocator)
      : std::vector<T, MemoryAllocator::Container<T>>(
            MemoryAllocator::Container<T>(allocator)) {}
};

class MemoryAllocatorFactory {
 public:
  virtual ~MemoryAllocatorFactory() = default;
  /// On Endpoint creation, call \a CreateMemoryAllocator to create a new
  /// allocator for the endpoint.
  /// \a name is used to label the memory allocator in debug logs.
  /// Typically we'll want to:
  ///    auto allocator = factory->CreateMemoryAllocator(peer_address_string);
  ///    auto* endpoint = allocator->New<MyEndpoint>(std::move(allocator), ...);
  virtual MemoryAllocator CreateMemoryAllocator(absl::string_view name) = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_MEMORY_ALLOCATOR_H
