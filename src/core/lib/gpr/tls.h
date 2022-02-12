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

#include <array>
#include <cstring>

#include <grpc/support/log.h> /* for GPR_ASSERT */

namespace grpc_core {

template <typename T>
class PthreadTlsImpl : TlsTypeConstrainer<T> {
 public:
  PthreadTlsImpl(const PthreadTlsImpl&) = delete;
  PthreadTlsImpl& operator=(const PthreadTlsImpl&) = delete;

  // Achtung! This class emulates C++ `thread_local` using pthread keys. Each
  // instance of this class is a stand in for a C++ `thread_local`. Think of
  // each `thread_local` as a *global* pthread_key_t and a type tag. An
  // important consequence of this is that the lifetime of a `pthread_key_t`
  // is precisely the lifetime of an instance of this class. To understand why
  // this is, consider the following scenario given a fictional implementation
  // of this class which creates and destroys its `pthread_key_t` each time
  // a given block of code runs (all actions take place on a single thread):
  //
  // - instance 1 (type tag = T*) is initialized, is assigned `pthread_key_t` 1
  // - instance 2 (type tag = int) is initialized, is assigned `pthread_key_t` 2
  // - instances 1 and 2 store and retrieve values; all is well
  // - instances 1 and 2 are de-initialized; their keys are released to the pool
  //
  // - another run commences
  // - instance 1 receives key 2
  // - a value is read from instance 1, it observes a value of type int, but
  //   interprets it as T*; undefined behavior, kaboom
  //
  // To properly ensure these invariants are upheld the `pthread_key_t` must be
  // `const`, which means it can only be released in the destructor. This is a
  // a violation of the style guide, since these objects are always static (see
  // footnote) but this code is used in sufficiently narrow circumstances to
  // justify the deviation.
  //
  // https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables
  PthreadTlsImpl()
      : keys_([]() {
          typename std::remove_const<decltype(PthreadTlsImpl::keys_)>::type
              keys;
          for (pthread_key_t& key : keys) {
            GPR_ASSERT(0 == pthread_key_create(&key, nullptr));
          }
          return keys;
        }()) {}
  PthreadTlsImpl(T t) : PthreadTlsImpl() { *this = t; }
  ~PthreadTlsImpl() {
    for (pthread_key_t key : keys_) {
      GPR_ASSERT(0 == pthread_key_delete(key));
    }
  }

  operator T() const {
    T t;
    char* dst = reinterpret_cast<char*>(&t);
    for (pthread_key_t key : keys_) {
      uintptr_t src = uintptr_t(pthread_getspecific(key));
      size_t remaining = reinterpret_cast<char*>(&t + 1) - dst;
      size_t step = std::min(sizeof(src), remaining);
      memcpy(dst, &src, step);
      dst += step;
    }
    return t;
  }

  T operator->() const {
    static_assert(std::is_pointer<T>::value,
                  "operator-> only usable on pointers");
    return this->operator T();
  }

  T operator=(T t) {
    char* src = reinterpret_cast<char*>(&t);
    for (pthread_key_t key : keys_) {
      uintptr_t dst;
      size_t remaining = reinterpret_cast<char*>(&t + 1) - src;
      size_t step = std::min(sizeof(dst), remaining);
      memcpy(&dst, src, step);
      GPR_ASSERT(0 == pthread_setspecific(key, reinterpret_cast<void*>(dst)));
      src += step;
    }
    return t;
  }

 private:
  const std::array<pthread_key_t,
                   (sizeof(T) + sizeof(void*) - 1) / sizeof(void*)>
      keys_;
};

}  // namespace grpc_core

#define GPR_THREAD_LOCAL(type) grpc_core::PthreadTlsImpl<type>

#else

#define GPR_THREAD_LOCAL(type) \
  thread_local typename grpc_core::TlsTypeConstrainer<type>::Type

#endif

#endif /* GRPC_CORE_LIB_GPR_TLS_H */
