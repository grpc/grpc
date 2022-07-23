/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPRPP_MANUAL_CONSTRUCTOR_H
#define GRPC_CORE_LIB_GPRPP_MANUAL_CONSTRUCTOR_H

// manually construct a region of memory with some type

#include <grpc/support/port_platform.h>

#include <utility>

#include "src/core/lib/gprpp/construct_destruct.h"

namespace grpc_core {

template <typename Type>
class ManualConstructor {
 public:
  ManualConstructor() = default;
  ~ManualConstructor() = default;
  ManualConstructor(const ManualConstructor&) = delete;
  ManualConstructor& operator=(const ManualConstructor&) = delete;

  Type* get() { return reinterpret_cast<Type*>(&space_); }
  const Type* get() const { return reinterpret_cast<const Type*>(&space_); }

  Type* operator->() { return get(); }
  const Type* operator->() const { return get(); }

  Type& operator*() { return *get(); }
  const Type& operator*() const { return *get(); }

  void Init() { Construct(get()); }

  // Init() constructs the Type instance using the given arguments
  // (which are forwarded to Type's constructor).
  //
  // Note that Init() with no arguments performs default-initialization,
  // not zero-initialization (i.e it behaves the same as "new Type;", not
  // "new Type();"), so it will leave non-class types uninitialized.
  template <typename... Ts>
  void Init(Ts&&... args) {
    Construct(get(), std::forward<Ts>(args)...);
  }

  // Init() that is equivalent to copy and move construction.
  // Enables usage like this:
  //   ManualConstructor<std::vector<int>> v;
  //   v.Init({1, 2, 3});
  void Init(const Type& x) { Construct(get(), x); }
  void Init(Type&& x) { Construct(get(), std::forward<Type>(x)); }

  void Destroy() { Destruct(get()); }

 private:
  typename std::aligned_storage<sizeof(Type), alignof(Type)>::type space_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_MANUAL_CONSTRUCTOR_H
