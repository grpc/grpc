/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPRPP_MEMORY_H
#define GRPC_CORE_LIB_GPRPP_MEMORY_H

#include <grpc/support/port_platform.h>

#include <grpc/support/alloc.h>

#include <limits>
#include <memory>
#include <utility>

// Add this to a class that want to use Delete(), but has a private or
// protected destructor.
#define GRPC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE \
  template <typename T>                           \
  friend void grpc_core::Delete(T*);
// Add this to a class that want to use New(), but has a private or
// protected constructor.
#define GRPC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW \
  template <typename T, typename... Args>      \
  friend T* grpc_core::New(Args&&...);

namespace grpc_core {

// Alternative to new, since we cannot use it (for fear of libstdc++)
template <typename T, typename... Args>
inline T* New(Args&&... args) {
  void* p = gpr_malloc(sizeof(T));
  return new (p) T(std::forward<Args>(args)...);
}

// Alternative to delete, since we cannot use it (for fear of libstdc++)
template <typename T>
inline void Delete(T* p) {
  if (p == nullptr) return;
  p->~T();
  gpr_free(p);
}

template <typename T>
class DefaultDelete {
 public:
  void operator()(T* p) { Delete(p); }
};

template <typename T, typename Deleter = DefaultDelete<T>>
using UniquePtr = std::unique_ptr<T, Deleter>;

template <typename T, typename... Args>
inline UniquePtr<T> MakeUnique(Args&&... args) {
  return UniquePtr<T>(New<T>(std::forward<Args>(args)...));
}

// an allocator that uses gpr_malloc/gpr_free
template <class T>
class Allocator {
 public:
  typedef T value_type;
  typedef T* pointer;
  typedef const T* const_pointer;
  typedef T& reference;
  typedef const T& const_reference;
  typedef std::size_t size_type;
  typedef std::ptrdiff_t difference_type;
  typedef std::false_type propagate_on_container_move_assignment;
  template <class U>
  struct rebind {
    typedef Allocator<U> other;
  };
  typedef std::true_type is_always_equal;

  pointer address(reference x) const { return &x; }
  const_pointer address(const_reference x) const { return &x; }
  pointer allocate(std::size_t n,
                   std::allocator<void>::const_pointer hint = nullptr) {
    return static_cast<pointer>(gpr_malloc(n * sizeof(T)));
  }
  void deallocate(T* p, std::size_t n) { gpr_free(p); }
  size_t max_size() const {
    return std::numeric_limits<size_type>::max() / sizeof(value_type);
  }
  void construct(pointer p, const_reference val) { new ((void*)p) T(val); }
  template <class U, class... Args>
  void construct(U* p, Args&&... args) {
    ::new ((void*)p) U(std::forward<Args>(args)...);
  }
  void destroy(pointer p) { p->~T(); }
  template <class U>
  void destroy(U* p) {
    p->~U();
  }
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_MEMORY_H */
