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

// Add this to a class that want to use Delete(), but has a private or
// protected destructor.
// Should not be used in new code.
// TODO(juanlishen): Remove this macro, and instead comment that the public dtor
// should not be used directly.
#define GRPC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE         \
  template <typename _Delete_T, bool _Delete_can_be_null> \
  friend void ::grpc_core::Delete(_Delete_T*);            \
  template <typename _Delete_T>                           \
  friend void ::grpc_core::Delete(_Delete_T*);

// Add this to a class that want to use New(), but has a private or
// protected constructor.
// Should not be used in new code.
// TODO(juanlishen): Remove this macro, and instead comment that the public dtor
// should not be used directly.
#define GRPC_ALLOW_CLASS_TO_USE_NON_PUBLIC_NEW      \
  template <typename _New_T, typename... _New_Args> \
  friend _New_T* grpc_core::New(_New_Args&&...);

namespace grpc_core {

// Alternative to new, since we cannot use it (for fear of libstdc++)
template <typename T, typename... Args>
inline T* New(Args&&... args) {
  void* p = gpr_malloc(sizeof(T));
  return new (p) T(std::forward<Args>(args)...);
}

// Alternative to delete, since we cannot use it (for fear of libstdc++)
// We cannot add a default value for can_be_null, because they are used as
// as friend template methods where we cannot define a default value.
// Instead we simply define two variants, one with and one without the boolean
// argument.
template <typename T, bool can_be_null>
inline void Delete(T* p) {
  GPR_DEBUG_ASSERT(can_be_null || p != nullptr);
  if (can_be_null && p == nullptr) return;
  p->~T();
  gpr_free(p);
}
template <typename T>
inline void Delete(T* p) {
  Delete<T, /*can_be_null=*/true>(p);
}

template <typename T>
class DefaultDelete {
 public:
  void operator()(T* p) {
    // std::unique_ptr is gauranteed not to call the deleter
    // if the pointer is nullptr.
    Delete<T, /*can_be_null=*/false>(p);
  }
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
