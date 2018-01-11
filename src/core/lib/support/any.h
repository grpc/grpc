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

#ifndef GRPC_CORE_LIB_SUPPORT_ANY_H
#define GRPC_CORE_LIB_SUPPORT_ANY_H

#include <cstddef>
#include <type_traits>

#include "src/core/lib/support/memory.h"

namespace grpc_core {

namespace any_impl {

struct VTable {
  void (*copy_init)(const void* from, void* to);
  void (*move_init)(void* from, void* to);
  void (*copy)(const void* from, void* to);
  void (*move)(void* from, void* to);
  void (*destroy)(void* value);
};

template <class T, bool kIsBig>
struct TypeInfo;

template <class T>
struct TypeInfo<T, false> {
  static const VTable vtable;

  static void Init(const T& value, void* storage) { new (storage) T(value); }

  static void MoveInit(T&& value, void* storage) {
    new (storage) T(std::move(value));
  }

  static T* Get(void* storage) { return static_cast<T*>(storage); }

  static const T* Get(const void* storage) {
    return static_cast<const T*>(storage);
  }

  static void CopyT(const void* from, void* to) {
    *static_cast<T*>(to) = *static_cast<const T*>(from);
  }

  static void MoveT(void* from, void* to) {
    *static_cast<T*>(to) = std::move(*static_cast<T*>(from));
  }

  static void Copy(const void* from, void* to) {
    Init(*static_cast<const T*>(from), to);
  }

  static void Move(void* from, void* to) {
    MoveInit(std::move(*static_cast<T*>(from)), to);
  }

  static void Destroy(void* value) { static_cast<T*>(value)->~T(); }
};

template <class T>
const VTable TypeInfo<T, false>::vtable = {
    Copy, Move, CopyT, MoveT, Destroy,
};

template <class T>
struct TypeInfo<T, true> {
  static const VTable vtable;
  typedef UniquePtr<T> TP;

  static void Init(const T& value, void* storage) {
    new (storage) TP(New<T>(value));
  }

  static void MoveInit(T&& value, void* storage) {
    new (storage) TP(New<T>(std::move(value)));
  }

  static T* Get(void* storage) { return static_cast<TP*>(storage)->get(); }

  static const T* Get(const void* storage) {
    return static_cast<const TP*>(storage)->get();
  }

  static void Copy(const void* from, void* to) {
    Init(**static_cast<const TP*>(from), to);
  }

  static void Move(void* from, void* to) {
    MoveInit(std::move(**static_cast<TP*>(from)), to);
  }

  static void CopyT(const void* from, void* to) {
    **static_cast<TP*>(to) = **static_cast<const TP*>(from);
  }

  static void MoveT(void* from, void* to) {
    *static_cast<TP*>(to) = std::move(*static_cast<TP*>(from));
  }

  static void Destroy(void* value) { static_cast<TP*>(value)->~TP(); }
};

template <class T>
const VTable TypeInfo<T, true>::vtable = {
    Copy, Move, CopyT, MoveT, Destroy,
};

template <class T = void>
struct Null {
  static const VTable vtable;

  static void Copy(const void* from, void* to) {}
  static void Move(void* from, void* to) {}
  static void Destroy(void* value) {}
};

template <class T>
const VTable Null<T>::vtable = {
    Copy, Move, Copy, Move, Destroy,
};

}  // namespace any_impl

template <size_t kInlineSize = sizeof(void*)>
class Any {
  template <class T>
  using TI = any_impl::TypeInfo<T, (sizeof(T) > kInlineSize)>;

 public:
  Any() : vtable_(&any_impl::Null<>::vtable) {}
  ~Any() {
    static_assert(kInlineSize >= sizeof(void*),
                  "Inlined data must be at least sizeof(void*)");
    vtable_->destroy(&storage_);
  }

  Any(const Any& other) : vtable_(other.vtable_) {
    vtable_->copy_init(&other.storage_, &storage_);
  }

  Any(Any& other) : vtable_(other.vtable_) {
    vtable_->copy_init(&other.storage_, &storage_);
  }

  Any(Any&& other) : vtable_(other.vtable_) {
    vtable_->move_init(&other.storage_, &storage_);
  }

  template <class T>
  Any(const T& value) : vtable_(&TI<T>::vtable) {
    TI<T>::Init(value, &storage_);
  }

  template <class T>
  Any(T&& value) : vtable_(&TI<T>::vtable) {
    TI<T>::MoveInit(std::move(value), &storage_);
  }

  Any& operator=(const Any& other) {
    if (other.vtable_ == vtable_) {
      vtable_->copy(&other.storage_, &storage_);
    } else {
      vtable_->destroy(&storage_);
      vtable_ = other.vtable_;
      vtable_->copy_init(&other.storage_, &storage_);
    }
    return *this;
  }

  Any& operator=(Any&& other) {
    if (other.vtable_ == vtable_) {
      vtable_->move(&other.storage_, &storage_);
    } else {
      vtable_->destroy(&storage_);
      vtable_ = other.vtable_;
      vtable_->move_init(&other.storage_, &storage_);
    }
    return *this;
  }

  template <class T>
  T* as() {
    return vtable_ == &TI<T>::vtable ? TI<T>::Get(&storage_) : nullptr;
  }

  template <class T>
  const T* as() const {
    return vtable_ == &TI<T>::vtable ? TI<T>::Get(&storage_) : nullptr;
  }

 private:
  const any_impl::VTable* vtable_;
  typename std::aligned_storage<kInlineSize>::type storage_;
};

}  // namespace grpc_core

#endif
