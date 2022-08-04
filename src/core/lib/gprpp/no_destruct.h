// Copyright 2022 gRPC authors.
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

#ifndef NO_DESTRUCT_H
#define NO_DESTRUCT_H

#include <type_traits>
#include <utility>

#include "src/core/lib/gprpp/construct_destruct.h"

namespace grpc_core {

template <typename T>
class NoDestruct {
 public:
  template <typename... Args>
  explicit NoDestruct(Args&&... args) {
    static_assert(std::is_trivially_destructible<NoDestruct<T>>::value,
                  "NoDestruct must be trivially destructible");
    Construct(reinterpret_cast<T*>(&space_), std::forward<Args>(args)...);
  }
  NoDestruct(const NoDestruct&) = delete;
  NoDestruct& operator=(const NoDestruct&) = delete;
  ~NoDestruct() = default;

  T* operator->() { return reinterpret_cast<T*>(&space_); }
  const T* operator->() const { return *reinterpret_cast<const T*>(&space_); }
  T& operator*() { return *reinterpret_cast<T*>(&space_); }
  const T& operator*() const { return *reinterpret_cast<const T*>(&space_); }

 private:
  typename std::aligned_storage<sizeof(T), alignof(T)>::type space_;
};

template <typename T>
class NoDestructSingleton {
 public:
  static T* Get() { return &*value_; }

 private:
  static NoDestruct<T> value_;
};

template <typename T>
NoDestruct<T> NoDestructSingleton<T>::value_;

}  // namespace grpc_core

#endif
