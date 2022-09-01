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

#ifndef GRPC_CORE_LIB_GPRPP_REF_COUNTED_PTR_H
#define GRPC_CORE_LIB_GPRPP_REF_COUNTED_PTR_H

#include <grpc/support/port_platform.h>

#include <iosfwd>
#include <utility>

#include "src/core/lib/gprpp/debug_location.h"

namespace grpc_core {

// A smart pointer class for objects that provide IncrementRefCount() and
// Unref() methods, such as those provided by the RefCounted base class.
template <typename T>
class RefCountedPtr {
 public:
  RefCountedPtr() {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  RefCountedPtr(std::nullptr_t) {}

  // If value is non-null, we take ownership of a ref to it.
  template <typename Y>
  explicit RefCountedPtr(Y* value) : value_(value) {}

  // Move ctors.
  RefCountedPtr(RefCountedPtr&& other) noexcept {
    value_ = other.value_;
    other.value_ = nullptr;
  }
  template <typename Y>
  // NOLINTNEXTLINE(google-explicit-constructor)
  RefCountedPtr(RefCountedPtr<Y>&& other) noexcept {
    value_ = static_cast<T*>(other.value_);
    other.value_ = nullptr;
  }

  // Move assignment.
  RefCountedPtr& operator=(RefCountedPtr&& other) noexcept {
    reset(std::exchange(other.value_, nullptr));
    return *this;
  }
  template <typename Y>
  RefCountedPtr& operator=(RefCountedPtr<Y>&& other) noexcept {
    reset(std::exchange(other.value_, nullptr));
    return *this;
  }

  // Copy ctors.
  RefCountedPtr(const RefCountedPtr& other) {
    if (other.value_ != nullptr) other.value_->IncrementRefCount();
    value_ = other.value_;
  }
  template <typename Y>
  // NOLINTNEXTLINE(google-explicit-constructor)
  RefCountedPtr(const RefCountedPtr<Y>& other) {
    static_assert(std::has_virtual_destructor<T>::value,
                  "T does not have a virtual dtor");
    if (other.value_ != nullptr) other.value_->IncrementRefCount();
    value_ = static_cast<T*>(other.value_);
  }

  // Copy assignment.
  // NOLINTNEXTLINE(bugprone-unhandled-self-assignment)
  RefCountedPtr& operator=(const RefCountedPtr& other) {
    // Note: Order of reffing and unreffing is important here in case value_
    // and other.value_ are the same object.
    if (other.value_ != nullptr) other.value_->IncrementRefCount();
    reset(other.value_);
    return *this;
  }
  template <typename Y>
  RefCountedPtr& operator=(const RefCountedPtr<Y>& other) {
    static_assert(std::has_virtual_destructor<T>::value,
                  "T does not have a virtual dtor");
    // Note: Order of reffing and unreffing is important here in case value_
    // and other.value_ are the same object.
    if (other.value_ != nullptr) other.value_->IncrementRefCount();
    reset(other.value_);
    return *this;
  }

  ~RefCountedPtr() {
    if (value_ != nullptr) value_->Unref();
  }

  void swap(RefCountedPtr& other) { std::swap(value_, other.value_); }

  // If value is non-null, we take ownership of a ref to it.
  void reset(T* value = nullptr) {
    T* old_value = std::exchange(value_, value);
    if (old_value != nullptr) old_value->Unref();
  }
  void reset(const DebugLocation& location, const char* reason,
             T* value = nullptr) {
    T* old_value = std::exchange(value_, value);
    if (old_value != nullptr) old_value->Unref(location, reason);
  }
  template <typename Y>
  void reset(Y* value = nullptr) {
    static_assert(std::has_virtual_destructor<T>::value,
                  "T does not have a virtual dtor");
    reset(static_cast<T*>(value));
  }
  template <typename Y>
  void reset(const DebugLocation& location, const char* reason,
             Y* value = nullptr) {
    static_assert(std::has_virtual_destructor<T>::value,
                  "T does not have a virtual dtor");
    reset(location, reason, static_cast<T*>(value));
  }

  // This method is mostly useful for interoperating with C code.
  // Eventually use within core should be banned, except at the surface API
  // boundaries.
  T* release() { return std::exchange(value_, nullptr); }

  T* get() const { return value_; }

  T& operator*() const { return *value_; }
  T* operator->() const { return value_; }

  template <typename Y>
  bool operator==(const RefCountedPtr<Y>& other) const {
    return value_ == other.value_;
  }

  template <typename Y>
  bool operator==(const Y* other) const {
    return value_ == other;
  }

  bool operator==(std::nullptr_t) const { return value_ == nullptr; }

  template <typename Y>
  bool operator!=(const RefCountedPtr<Y>& other) const {
    return value_ != other.value_;
  }

  template <typename Y>
  bool operator!=(const Y* other) const {
    return value_ != other;
  }

  bool operator!=(std::nullptr_t) const { return value_ != nullptr; }

 private:
  template <typename Y>
  friend class RefCountedPtr;

  T* value_ = nullptr;
};

// A smart pointer class for objects that provide IncrementWeakRefCount() and
// WeakUnref() methods, such as those provided by the DualRefCounted base class.
template <typename T>
class WeakRefCountedPtr {
 public:
  WeakRefCountedPtr() {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  WeakRefCountedPtr(std::nullptr_t) {}

  // If value is non-null, we take ownership of a ref to it.
  template <typename Y>
  explicit WeakRefCountedPtr(Y* value) {
    value_ = value;
  }

  // Move ctors.
  WeakRefCountedPtr(WeakRefCountedPtr&& other) noexcept {
    value_ = other.value_;
    other.value_ = nullptr;
  }
  template <typename Y>
  // NOLINTNEXTLINE(google-explicit-constructor)
  WeakRefCountedPtr(WeakRefCountedPtr<Y>&& other) noexcept {
    value_ = static_cast<T*>(other.value_);
    other.value_ = nullptr;
  }

  // Move assignment.
  WeakRefCountedPtr& operator=(WeakRefCountedPtr&& other) noexcept {
    reset(std::exchange(other.value_, nullptr));
    return *this;
  }
  template <typename Y>
  WeakRefCountedPtr& operator=(WeakRefCountedPtr<Y>&& other) noexcept {
    reset(std::exchange(other.value_, nullptr));
    return *this;
  }

  // Copy ctors.
  WeakRefCountedPtr(const WeakRefCountedPtr& other) {
    if (other.value_ != nullptr) other.value_->IncrementWeakRefCount();
    value_ = other.value_;
  }
  template <typename Y>
  // NOLINTNEXTLINE(google-explicit-constructor)
  WeakRefCountedPtr(const WeakRefCountedPtr<Y>& other) {
    static_assert(std::has_virtual_destructor<T>::value,
                  "T does not have a virtual dtor");
    if (other.value_ != nullptr) other.value_->IncrementWeakRefCount();
    value_ = static_cast<T*>(other.value_);
  }

  // Copy assignment.
  // NOLINTNEXTLINE(bugprone-unhandled-self-assignment)
  WeakRefCountedPtr& operator=(const WeakRefCountedPtr& other) {
    // Note: Order of reffing and unreffing is important here in case value_
    // and other.value_ are the same object.
    if (other.value_ != nullptr) other.value_->IncrementWeakRefCount();
    reset(other.value_);
    return *this;
  }
  template <typename Y>
  WeakRefCountedPtr& operator=(const WeakRefCountedPtr<Y>& other) {
    static_assert(std::has_virtual_destructor<T>::value,
                  "T does not have a virtual dtor");
    // Note: Order of reffing and unreffing is important here in case value_
    // and other.value_ are the same object.
    if (other.value_ != nullptr) other.value_->IncrementWeakRefCount();
    reset(other.value_);
    return *this;
  }

  ~WeakRefCountedPtr() {
    if (value_ != nullptr) value_->WeakUnref();
  }

  void swap(WeakRefCountedPtr& other) { std::swap(value_, other.value_); }

  // If value is non-null, we take ownership of a ref to it.
  void reset(T* value = nullptr) {
    T* old_value = std::exchange(value_, value);
    if (old_value != nullptr) old_value->WeakUnref();
  }
  void reset(const DebugLocation& location, const char* reason,
             T* value = nullptr) {
    T* old_value = std::exchange(value_, value);
    if (old_value != nullptr) old_value->WeakUnref(location, reason);
  }
  template <typename Y>
  void reset(Y* value = nullptr) {
    static_assert(std::has_virtual_destructor<T>::value,
                  "T does not have a virtual dtor");
    reset(static_cast<T*>(value));
  }
  template <typename Y>
  void reset(const DebugLocation& location, const char* reason,
             Y* value = nullptr) {
    static_assert(std::has_virtual_destructor<T>::value,
                  "T does not have a virtual dtor");
    reset(location, reason, static_cast<T*>(value));
  }

  // TODO(roth): This method exists solely as a transition mechanism to allow
  // us to pass a ref to idiomatic C code that does not use WeakRefCountedPtr<>.
  // Once all of our code has been converted to idiomatic C++, this
  // method should go away.
  T* release() {
    T* value = value_;
    value_ = nullptr;
    return value;
  }

  T* get() const { return value_; }

  T& operator*() const { return *value_; }
  T* operator->() const { return value_; }

  template <typename Y>
  bool operator==(const WeakRefCountedPtr<Y>& other) const {
    return value_ == other.value_;
  }

  template <typename Y>
  bool operator==(const Y* other) const {
    return value_ == other;
  }

  bool operator==(std::nullptr_t) const { return value_ == nullptr; }

  template <typename Y>
  bool operator!=(const WeakRefCountedPtr<Y>& other) const {
    return value_ != other.value_;
  }

  template <typename Y>
  bool operator!=(const Y* other) const {
    return value_ != other;
  }

  bool operator!=(std::nullptr_t) const { return value_ != nullptr; }

 private:
  template <typename Y>
  friend class WeakRefCountedPtr;

  T* value_ = nullptr;
};

template <typename T, typename... Args>
inline RefCountedPtr<T> MakeRefCounted(Args&&... args) {
  return RefCountedPtr<T>(new T(std::forward<Args>(args)...));
}

template <typename T>
bool operator<(const RefCountedPtr<T>& p1, const RefCountedPtr<T>& p2) {
  return p1.get() < p2.get();
}

template <typename T>
bool operator<(const WeakRefCountedPtr<T>& p1, const WeakRefCountedPtr<T>& p2) {
  return p1.get() < p2.get();
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_REF_COUNTED_PTR_H */
