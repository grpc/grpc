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

#ifndef GRPC_CORE_LIB_SUPPORT_MANUAL_CONSTRUCTOR_H
#define GRPC_CORE_LIB_SUPPORT_MANUAL_CONSTRUCTOR_H

// manually construct a region of memory with some type

#include <stddef.h>
#include <stdlib.h>
#include <new>
#include <type_traits>
#include <utility>

namespace grpc_core {

#define ASSERT(x) \
  if ((x))        \
    ;             \
  else            \
  abort()

template <class Needle, class... Haystack>
class is_one_of;

template <class Needle, class... Haystack>
class is_one_of<Needle, Needle, Haystack...> {
 public:
  static constexpr const bool value = true;
};

template <class Needle, class A, class... Haystack>
class is_one_of<Needle, A, Haystack...> {
 public:
  static constexpr const bool value = is_one_of<Needle, Haystack...>::value;
};

template <class Needle>
class is_one_of<Needle> {
 public:
  static constexpr const bool value = false;
};

template <class... Types>
class max_size_of;

template <class A>
class max_size_of<A> {
 public:
  static constexpr const size_t value = sizeof(A);
};

template <class A, class... B>
class max_size_of<A, B...> {
 public:
  static constexpr const size_t value = sizeof(A) > max_size_of<B...>::value
                                            ? sizeof(A)
                                            : max_size_of<B...>::value;
};

template <class... Types>
class max_align_of;

template <class A>
class max_align_of<A> {
 public:
  static constexpr const size_t value = alignof(A);
};

template <class A, class... B>
class max_align_of<A, B...> {
 public:
  static constexpr const size_t value = alignof(A) > max_align_of<B...>::value
                                            ? alignof(A)
                                            : max_align_of<B...>::value;
};

template <class BaseType, class... DerivedTypes>
class PolymorphicManualConstructor {
 public:
  // No constructor or destructor because one of the most useful uses of
  // this class is as part of a union, and members of a union could not have
  // constructors or destructors till C++11.  And, anyway, the whole point of
  // this class is to bypass constructor and destructor.

  BaseType* get() { return reinterpret_cast<BaseType*>(&space_); }
  const BaseType* get() const {
    return reinterpret_cast<const BaseType*>(&space_);
  }

  BaseType* operator->() { return get(); }
  const BaseType* operator->() const { return get(); }

  BaseType& operator*() { return *get(); }
  const BaseType& operator*() const { return *get(); }

  template <class DerivedType>
  void Init() {
    FinishInit(new (&space_) DerivedType);
  }

  // Init() constructs the Type instance using the given arguments
  // (which are forwarded to Type's constructor).
  //
  // Note that Init() with no arguments performs default-initialization,
  // not zero-initialization (i.e it behaves the same as "new Type;", not
  // "new Type();"), so it will leave non-class types uninitialized.
  template <class DerivedType, typename... Ts>
  void Init(Ts&&... args) {
    FinishInit(new (&space_) DerivedType(std::forward<Ts>(args)...));
  }

  // Init() that is equivalent to copy and move construction.
  // Enables usage like this:
  //   ManualConstructor<std::vector<int>> v;
  //   v.Init({1, 2, 3});
  template <class DerivedType>
  void Init(const DerivedType& x) {
    FinishInit(new (&space_) DerivedType(x));
  }
  template <class DerivedType>
  void Init(DerivedType&& x) {
    FinishInit(new (&space_) DerivedType(std::move(x)));
  }

  void Destroy() { get()->~BaseType(); }

 private:
  template <class DerivedType>
  void FinishInit(DerivedType* p) {
    static_assert(is_one_of<DerivedType, DerivedTypes...>::value,
                  "DerivedType must be one of the predeclared DerivedTypes");
    ASSERT(reinterpret_cast<BaseType*>(static_cast<DerivedType*>(p)) == p);
  }

  typename std::aligned_storage<
      grpc_core::max_size_of<DerivedTypes...>::value,
      grpc_core::max_align_of<DerivedTypes...>::value>::type space_;
};

template <typename Type>
class ManualConstructor {
 public:
  // No constructor or destructor because one of the most useful uses of
  // this class is as part of a union, and members of a union could not have
  // constructors or destructors till C++11.  And, anyway, the whole point of
  // this class is to bypass constructor and destructor.

  Type* get() { return reinterpret_cast<Type*>(&space_); }
  const Type* get() const { return reinterpret_cast<const Type*>(&space_); }

  Type* operator->() { return get(); }
  const Type* operator->() const { return get(); }

  Type& operator*() { return *get(); }
  const Type& operator*() const { return *get(); }

  void Init() { new (&space_) Type; }

  // Init() constructs the Type instance using the given arguments
  // (which are forwarded to Type's constructor).
  //
  // Note that Init() with no arguments performs default-initialization,
  // not zero-initialization (i.e it behaves the same as "new Type;", not
  // "new Type();"), so it will leave non-class types uninitialized.
  template <typename... Ts>
  void Init(Ts&&... args) {
    new (&space_) Type(std::forward<Ts>(args)...);
  }

  // Init() that is equivalent to copy and move construction.
  // Enables usage like this:
  //   ManualConstructor<std::vector<int>> v;
  //   v.Init({1, 2, 3});
  void Init(const Type& x) { new (&space_) Type(x); }
  void Init(Type&& x) { new (&space_) Type(std::move(x)); }

  void Destroy() { get()->~Type(); }

 private:
  typename std::aligned_storage<sizeof(Type), alignof(Type)>::type space_;
};

}  // namespace grpc_core

#endif
