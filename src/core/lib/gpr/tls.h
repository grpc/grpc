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
template <typename T, typename = typename std::enable_if<(
                          std::is_trivially_destructible<T>::value &&
                          sizeof(T) <= sizeof(void*) &&
                          alignof(void*) % alignof(T) == 0)>::type>
class TlsTypeConstrainer {
  static_assert(std::is_trivially_destructible<T>::value &&
                    sizeof(T) <= sizeof(void*) &&
                    alignof(void*) % alignof(T) == 0,
                "unsupported type for TLS");

 public:
  using Type = T;
};

}  // namespace grpc_core

#if defined(GPR_PTHREAD_TLS)

#include <grpc/support/log.h> /* for GPR_ASSERT */
#include <pthread.h>

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
      : key_([]() {
          pthread_key_t key;
          GPR_ASSERT(0 == pthread_key_create(&key, nullptr));
          return key;
        }()) {}
  PthreadTlsImpl(T t) : PthreadTlsImpl() { *this = t; }
  ~PthreadTlsImpl() { GPR_ASSERT(0 == pthread_key_delete(key_)); }

  operator T() const { return Cast<T>(pthread_getspecific(key_)); }

  T operator=(T t) {
    GPR_ASSERT(0 == pthread_setspecific(key_, Cast<T>(t)));
    return t;
  }

 private:
  // TODO(C++17): Replace these helpers with constexpr if statements inline.
  template <typename V>
  static typename std::enable_if<std::is_pointer<V>::value, V>::type Cast(
      void* object) {
    return static_cast<V>(object);
  }

  template <typename V>
  static typename std::enable_if<
      !std::is_pointer<V>::value && std::is_integral<V>::value, V>::type
  Cast(void* object) {
    return reinterpret_cast<uintptr_t>(object);
  }

  template <typename V>
  static void* Cast(
      typename std::enable_if<std::is_pointer<V>::value, V>::type t) {
    return t;
  }

  template <typename V>
  static void* Cast(typename std::enable_if<!std::is_pointer<V>::value &&
                                                std::is_integral<V>::value,
                                            V>::type t) {
    return reinterpret_cast<void*>(uintptr_t(t));
  }

  const pthread_key_t key_;
};

}  // namespace grpc_core

#define GPR_THREAD_LOCAL(type) grpc_core::PthreadTlsImpl<type>

#else

#define GPR_THREAD_LOCAL(type) \
  thread_local grpc_core::TlsTypeConstrainer<type>::Type

#endif

#endif /* GRPC_CORE_LIB_GPR_TLS_H */
