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

#include <grpc/support/port_platform.h>

#include <atomic>

#include <grpc/support/log.h>

namespace grpc_core {

template <class T>
class SingleSetPtr {
 public:
  SingleSetPtr() = default;
  ~SingleSetPtr() {
    T* p = p_.load(std::memory_order_relaxed);
    if (p != sentinel()) delete p;
  }

  SingleSetPtr(const SingleSetPtr&) = delete;
  SingleSetPtr& operator=(const SingleSetPtr&) = delete;
  SingleSetPtr(SingleSetPtr&& other) noexcept
      : p_(other.p_.exchange(sentinel())) {}
  SingleSetPtr& operator=(SingleSetPtr&& other) noexcept {
    T* p = p_.exchange(other.p_.exchange(sentinel(), std::memory_order_acq_rel),
                       std::memory_order_acq_rel);
    return *this;
  }

  // Set the pointer;
  // if already set, return the pre-set value and delete ptr;
  // if deleted, return nullptr and delete ptr.
  T* Set(T* ptr) {
    T* expected = nullptr;
    if (!p_.compare_exchange_strong(expected, ptr, std::memory_order_acq_rel,
                                    std::memory_order_acquire)) {
      delete ptr;
      return expected == sentinel() ? nullptr : expected;
    }
    return ptr;
  }

  // Clear the pointer. Cannot be set again.
  void Reset() {
    T* p = p_.exchange(sentinel(), std::memory_order_acq_rel);
    if (p != sentinel()) delete p;
  }

  bool is_set() const {
    T* p = p_.load(std::memory_order_acquire);
    if (p == sentinel()) return false;
    if (p == nullptr) return false;
    return true;
  }

  T* operator->() const {
    T* p = p_.load(std::memory_order_acquire);
    GPR_DEBUG_ASSERT(p != sentinel() && p != nullptr);
    return p;
  }

  T& operator*() const { return *operator->(); }

 private:
  static T* sentinel() { return reinterpret_cast<T*>(1); }
  std::atomic<T*> p_{nullptr};
};

}  // namespace grpc_core
