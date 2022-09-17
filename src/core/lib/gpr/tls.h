/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPR_TLS_H
#define GRPC_CORE_LIB_GPR_TLS_H

#include <grpc/support/port_platform.h>

#include <type_traits>

/** Thread local storage.

   Usage is the same as C++ thread_local. Declaring a thread local:
     static GPR_THREAD_LOCAL(uint32_t) foo;

   ALL functions here may be implemented as macros. */

namespace grpc_core {

// This class is never instantiated. It exists to statically ensure that all
// TLS usage is compatible with the most restrictive implementation, allowing
// developers to write correct code regardless of the platform they develop on.
template <typename T>
class TlsTypeConstrainer {
  static_assert(std::is_trivial<T>::value,
                "TLS support is limited to trivial types");

 public:
  using Type = T;
};

}  // namespace grpc_core

#if defined(GPR_PTHREAD_TLS)

#include <pthread.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace grpc_core {

template <typename T>
class PthreadTlsImpl : TlsTypeConstrainer<T> {
 public:
  PthreadTlsImpl(const PthreadTlsImpl&) = delete;
  PthreadTlsImpl& operator=(const PthreadTlsImpl&) = delete;

  // To properly ensure these invariants are upheld the `pthread_key_t` must
  // be `const`, which means it can only be released in the destructor. This
  // is a a violation of the style guide, since these objects are always
  // static (see footnote) but this code is used in sufficiently narrow
  // circumstances to justify the deviation.
  //
  // https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables
  PthreadTlsImpl(T t)
      : default_value_(t), key_([]() {
          pthread_key_t key;
          if (0 != pthread_key_create(
                       &key, [](void* p) { delete static_cast<T*>(p); })) {
            abort();
          }
          return key;
        }()) {}
  PthreadTlsImpl() : PthreadTlsImpl({}) {}
  ~PthreadTlsImpl() {
    if (0 != pthread_key_delete(key_)) abort();
  }

  operator T() const {
    auto* p = static_cast<T*>(pthread_getspecific(key_));
    if (p == nullptr) return default_value_;
    return *p;
  }

  T operator->() const {
    static_assert(std::is_pointer<T>::value,
                  "operator-> only usable on pointers");
    return this->operator T();
  }

  T operator=(T t) {
    auto* p = static_cast<T*>(pthread_getspecific(key_));
    if (p == nullptr) {
      p = new T(t);
      if (0 != pthread_setspecific(key_, p)) abort();
    } else {
      *p = t;
    }
    return t;
  }

 private:
  const T default_value_;
  const pthread_key_t key_;
};

}  // namespace grpc_core

#define GPR_THREAD_LOCAL(type) grpc_core::PthreadTlsImpl<type>

#else

#define GPR_THREAD_LOCAL(type) \
  thread_local typename grpc_core::TlsTypeConstrainer<type>::Type

#endif

#endif /* GRPC_CORE_LIB_GPR_TLS_H */
