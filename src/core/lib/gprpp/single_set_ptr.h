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

#ifndef GRPC_CORE_LIB_GPRPP_SINGLE_SET_PTR_H
#define GRPC_CORE_LIB_GPRPP_SINGLE_SET_PTR_H

#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>

#include <grpc/support/log.h>

namespace grpc_core {

template <class T, class Deleter = std::default_delete<T>>
class SingleSetPtr {
 public:
  SingleSetPtr() = default;
  ~SingleSetPtr() { Delete(p_.load(std::memory_order_relaxed)); }

  SingleSetPtr(const SingleSetPtr&) = delete;
  SingleSetPtr& operator=(const SingleSetPtr&) = delete;
  SingleSetPtr(SingleSetPtr&& other) noexcept
      : p_(other.p_.exchange(sentinel())) {}
  SingleSetPtr& operator=(SingleSetPtr&& other) noexcept {
    Set(other.p_.exchange(sentinel(), std::memory_order_acq_rel));
    return *this;
  }

  // Set the pointer;
  // if already set, return the pre-set value and delete ptr;
  // if deleted, return nullptr and delete ptr.
  T* Set(T* ptr) {
    T* expected = nullptr;
    if (!p_.compare_exchange_strong(expected, ptr, std::memory_order_acq_rel,
                                    std::memory_order_acquire)) {
      Delete(ptr);
      return expected == sentinel() ? nullptr : expected;
    }
    return ptr;
  }

  // Clear the pointer. Cannot be set again.
  void Reset() { Delete(p_.exchange(sentinel(), std::memory_order_acq_rel)); }

  bool is_set() const {
    T* p = p_.load(std::memory_order_acquire);
    return p != nullptr && p != sentinel();
  }

  T* operator->() const {
    T* p = p_.load(std::memory_order_acquire);
    GPR_DEBUG_ASSERT(p != sentinel());
    GPR_DEBUG_ASSERT(p != nullptr);
    return p;
  }

  T& operator*() const { return *operator->(); }

 private:
  static T* sentinel() { return reinterpret_cast<T*>(1); }
  static void Delete(T* p) {
    if (p == sentinel() || p == nullptr) return;
    Deleter()(p);
  }
  std::atomic<T*> p_{nullptr};
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_SINGLE_SET_PTR_H
