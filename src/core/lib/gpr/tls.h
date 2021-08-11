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

   Initializing a thread local (must be done at library initialization time):
     gpr_tls_init(foo);

   Destroying a thread local:
     gpr_tls_destroy(foo);

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

#ifdef GPR_STDCPP_TLS
#define GPR_THREAD_LOCAL(type) \
  thread_local grpc_core::TlsTypeConstrainer<type>::Type
#endif

#ifdef GPR_GCC_TLS
#define GPR_THREAD_LOCAL(type) \
  __thread grpc_core::TlsTypeConstrainer<type>::Type
#endif

#ifdef GPR_MSVC_TLS
#define GPR_THREAD_LOCAL(type) \
  __declspec(thread) grpc_core::TlsTypeConstrainer<type>::Type
#endif

#ifdef GPR_PTHREAD_TLS
#include <grpc/support/log.h> /* for GPR_ASSERT */
#include <pthread.h>

namespace grpc_core {

template <typename T>
class PthreadTlsImpl : TlsTypeConstrainer<T> {
 public:
  PthreadTlsImpl() = default;
  PthreadTlsImpl(const PthreadTlsImpl&) = delete;
  PthreadTlsImpl& operator=(const PthreadTlsImpl&) = delete;

  void Init() { GPR_ASSERT(0 == pthread_key_create(&key_, nullptr)); }

  void Destroy() { GPR_ASSERT(0 == pthread_key_delete(key_)); }

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

  pthread_key_t key_;
};

// This class is never instantiated. It exists to statically ensure that all
// TLS usage is compatible with the most restrictive implementation, allowing
// developers to write correct code regardless of the platform they develop on.
template <typename T>
class TriviallyDestructibleAsserter {
  // This type is often used as a global; since the type itself is hidden by the
  // macros, enforce compliance with the style guide here rather than at the
  // caller. See
  // https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables.
  static_assert(std::is_trivially_destructible<T>::value,
                "TLS wrapper must be trivially destructible");

 public:
  using Type = T;
};

}  // namespace grpc_core

#define GPR_THREAD_LOCAL(type)              \
  grpc_core::TriviallyDestructibleAsserter< \
      grpc_core::PthreadTlsImpl<type>>::Type

#define gpr_tls_init(tls) (tls).Init()
#define gpr_tls_destroy(tls) (tls).Destroy()
#else
#define gpr_tls_init(tls) \
  do {                    \
  } while (0)
#define gpr_tls_destroy(tls) \
  do {                       \
  } while (0)
#endif

#endif /* GRPC_CORE_LIB_GPR_TLS_H */
