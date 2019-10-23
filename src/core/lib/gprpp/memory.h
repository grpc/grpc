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
#include <grpc/support/log.h>

#include <limits>
#include <memory>
#include <utility>

namespace grpc_core {

// Alternative to new, to ensure memory allocation being wrapped to gpr_malloc
template <typename T, typename... Args>
inline T* New(Args&&... args) {
  void* p = gpr_malloc(sizeof(T));
  return new (p) T(std::forward<Args>(args)...);
}

// Gets the base pointer of any class, in case of multiple inheritance.
// Used by Delete and friends.
template <typename T, bool isPolymorphic>
struct BasePointerGetter {
  static void* get(T* p) { return p; }
};

template <typename T>
struct BasePointerGetter<T, true> {
  static void* get(T* p) { return dynamic_cast<void*>(p); }
};

// Alternative to delete, to ensure memory allocation being wrapped to gpr_free
template <typename T>
inline void Delete(T* p) {
  if (p == nullptr) return;
  void* basePtr = BasePointerGetter<T, std::is_polymorphic<T>::value>::get(p);
  p->~T();
  gpr_free(basePtr);
}

class DefaultDelete {
 public:
  template <typename T>
  void operator()(T* p) {
    // Delete() checks whether the value is null, but std::unique_ptr<> is
    // guaranteed not to call the deleter if the pointer is nullptr
    // (i.e., it already does this check for us), and we don't want to
    // do the check twice.  So, instead of calling Delete() here, we
    // manually call the object's dtor and free it.
    void* basePtr = BasePointerGetter<T, std::is_polymorphic<T>::value>::get(p);
    p->~T();
    gpr_free(basePtr);
  }
};

template <typename T, typename Deleter = DefaultDelete>
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

  Allocator() = default;

  template <class U>
  Allocator(const Allocator<U>&) {}

  pointer address(reference x) const { return &x; }
  const_pointer address(const_reference x) const { return &x; }
  pointer allocate(std::size_t n,
                   std::allocator<void>::const_pointer /*hint*/ = nullptr) {
    return static_cast<pointer>(gpr_malloc(n * sizeof(T)));
  }
  void deallocate(T* p, std::size_t /* n */) { gpr_free(p); }
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

template <class T, class U>
bool operator==(Allocator<T> const&, Allocator<U> const&) noexcept {
  return true;
}

template <class T, class U>
bool operator!=(Allocator<T> const& /*x*/, Allocator<U> const& /*y*/) noexcept {
  return false;
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_MEMORY_H */
