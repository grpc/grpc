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

#ifndef GRPC_CORE_LIB_SUPPORT_MEMORY_H
#define GRPC_CORE_LIB_SUPPORT_MEMORY_H

#include <grpc/support/alloc.h>

#include <memory>
#include <utility>

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

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SUPPORT_MEMORY_H */
