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
#ifndef GRPC_EVENT_ENGINE_SLICE_ALLOCATOR_H
#define GRPC_EVENT_ENGINE_SLICE_ALLOCATOR_H

#include <grpc/impl/codegen/port_platform.h>

#include <memory>
#include <type_traits>
#include <vector>

#include <grpc/slice.h>

// forward-declaring an internal struct, not used publicly.
struct grpc_slice_buffer;

namespace grpc_event_engine {
namespace experimental {

// TODO(nnoble): needs implementation
class SliceBuffer {
 public:
  SliceBuffer() { abort(); }
  explicit SliceBuffer(grpc_slice_buffer*) { abort(); }

  grpc_slice_buffer* RawSliceBuffer() { return slice_buffer_; }

 private:
  grpc_slice_buffer* slice_buffer_;
};

// Reservation request - how much memory do we want to allocate?
class MemoryRequest {
 public:
  // Request a fixed amount of memory.
  // NOLINTNEXTLINE(google-explicit-constructor)
  MemoryRequest(size_t n) : min_(n), max_(n) {}
  // Request a range of memory.
  MemoryRequest(size_t min, size_t max) : min_(std::min(min, max)), max_(max) {}

  // Increase the size by amount
  MemoryRequest Increase(size_t amount) const {
    return MemoryRequest(min_ + amount, max_ + amount);
  }

  size_t min() const { return min_; }
  size_t max() const { return max_; }

 private:
  size_t min_;
  size_t max_;
};

class BasicMemoryAllocator
    : public std::enable_shared_from_this<BasicMemoryAllocator> {
 public:
  BasicMemoryAllocator() {}
  virtual ~BasicMemoryAllocator() {}

  BasicMemoryAllocator(const BasicMemoryAllocator&) = delete;
  BasicMemoryAllocator& operator=(const BasicMemoryAllocator&) = delete;

  // Reserve bytes from the quota.
  // If we enter overcommit, reclamation will begin concurrently.
  // Returns the number of bytes reserved.
  virtual size_t Reserve(MemoryRequest request) = 0;

  // Release some bytes that were previously reserved.
  virtual void Release(size_t n) = 0;

  // Shutdown this allocator.
  // Further usage of Reserve() is undefined behavior.
  virtual void Shutdown() = 0;
};

class MemoryAllocator {
 public:
  // Construct a MemoryAllocator given a BasicMemoryAllocator implementation.
  // The constructed MemoryAllocator will call BasicMemoryAllocator::Shutdown()
  // upon destruction.
  explicit MemoryAllocator(std::shared_ptr<BasicMemoryAllocator> allocator)
      : allocator_(allocator) {}
  ~MemoryAllocator() {
    if (allocator_ != nullptr) allocator_->Shutdown();
  }

  MemoryAllocator(const MemoryAllocator&) = delete;
  MemoryAllocator& operator=(const MemoryAllocator&) = delete;

  MemoryAllocator(MemoryAllocator&&) = default;
  MemoryAllocator& operator=(MemoryAllocator&&) = default;

  // Reserve bytes from the quota.
  // If we enter overcommit, reclamation will begin concurrently.
  // Returns the number of bytes reserved.
  size_t Reserve(MemoryRequest request) { return allocator_->Reserve(request); }

  // Release some bytes that were previously reserved.
  void Release(size_t n) { return allocator_->Release(n); }

  //
  // The remainder of this type are helper functions implemented in terms of
  // Reserve/Release.
  //

  // An automatic releasing reservation of memory.
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
    Reservation(std::shared_ptr<BasicMemoryAllocator> allocator, size_t size)
        : allocator_(allocator), size_(size) {}

    std::shared_ptr<BasicMemoryAllocator> allocator_ = nullptr;
    size_t size_ = 0;
  };

  // Reserve bytes from the quota and automatically release them when
  // Reservation is destroyed.
  Reservation MakeReservation(MemoryRequest request) {
    return Reservation(allocator_, Reserve(request));
  }

  // Allocate a new object of type T, with constructor arguments.
  // The returned type is wrapped, and upon destruction the reserved memory
  // will be released to the allocator automatically. As such, T must have a
  // virtual destructor so we can insert the necessary hook.
  template <typename T, typename... Args>
  typename std::enable_if<std::has_virtual_destructor<T>::value, T*>::type New(
      Args&&... args) {
    // Wrap T such that when it's destroyed, we can release memory back to the
    // allocator.
    class Wrapper final : public T {
     public:
      explicit Wrapper(std::shared_ptr<BasicMemoryAllocator> allocator,
                       Args&&... args)
          : T(std::forward<Args>(args)...), allocator_(std::move(allocator)) {}
      ~Wrapper() override { allocator_->Release(sizeof(*this)); }

     private:
      const std::shared_ptr<BasicMemoryAllocator> allocator_;
    };
    Reserve(sizeof(Wrapper));
    return new Wrapper(allocator_, std::forward<Args>(args)...);
  }

  // Construct a unique ptr immediately.
  template <typename T, typename... Args>
  std::unique_ptr<T> MakeUnique(Args&&... args) {
    return std::unique_ptr<T>(New<T>(std::forward<Args>(args)...));
  }

  // Allocate a slice, using MemoryRequest to size the number of returned bytes.
  // For a variable length request, check the returned slice length to verify
  // how much memory was allocated.
  // Takes care of reserving memory for any relevant control structures also.
  grpc_slice MakeSlice(MemoryRequest request);

  // A C++ allocator for containers of T.
  template <typename T>
  class Container {
   public:
    using value_type = T;

    // Construct the allocator: \a underlying_allocator is borrowed, and must
    // outlive this object.
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
  const std::shared_ptr<BasicMemoryAllocator> allocator() { return allocator_; }

 private:
  std::shared_ptr<BasicMemoryAllocator> allocator_;
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
  /// Typically we'll want to:
  ///    auto allocator = factory->CreateMemoryAllocator();
  ///    auto* endpoint = allocator->New<MyEndpoint>(std::move(allocator), ...);
  virtual MemoryAllocator CreateMemoryAllocator() = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_SLICE_ALLOCATOR_H
