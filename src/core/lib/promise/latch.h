// Copyright 2021 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_LATCH_H
#define GRPC_SRC_CORE_LIB_PROMISE_LATCH_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <atomic>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/trace.h"

namespace grpc_core {

// Latch provides a single set waitable object.
// Initially the Latch is unset.
// It can be waited upon by the Wait method, which produces a Promise that
// resolves when the Latch is Set to a value of type T.
template <typename T>
class Latch {
 public:
  Latch() = default;
  Latch(const Latch&) = delete;
  explicit Latch(T value) : value_(std::move(value)), has_value_(true) {}
  Latch& operator=(const Latch&) = delete;
  Latch(Latch&& other) noexcept
      : value_(std::move(other.value_)), has_value_(other.has_value_) {
#ifndef NDEBUG
    GPR_DEBUG_ASSERT(!other.has_had_waiters_);
#endif
  }
  Latch& operator=(Latch&& other) noexcept {
#ifndef NDEBUG
    GPR_DEBUG_ASSERT(!other.has_had_waiters_);
#endif
    value_ = std::move(other.value_);
    has_value_ = other.has_value_;
    return *this;
  }

  // Produce a promise to wait for a value from this latch.
  // Moves the result out of the latch.
  auto Wait() {
#ifndef NDEBUG
    has_had_waiters_ = true;
#endif
    return [this]() -> Poll<T> {
      if (grpc_trace_promise_primitives.enabled()) {
        gpr_log(GPR_INFO, "%sWait %s", DebugTag().c_str(),
                StateString().c_str());
      }
      if (has_value_) {
        return std::move(value_);
      } else {
        return waiter_.pending();
      }
    };
  }

  // Produce a promise to wait for a value from this latch.
  // Copies the result out of the latch.
  auto WaitAndCopy() {
#ifndef NDEBUG
    has_had_waiters_ = true;
#endif
    return [this]() -> Poll<T> {
      if (grpc_trace_promise_primitives.enabled()) {
        gpr_log(GPR_INFO, "%sWaitAndCopy %s", DebugTag().c_str(),
                StateString().c_str());
      }
      if (has_value_) {
        return value_;
      } else {
        return waiter_.pending();
      }
    };
  }

  // Set the value of the latch. Can only be called once.
  void Set(T value) {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%sSet %s", DebugTag().c_str(), StateString().c_str());
    }
    GPR_DEBUG_ASSERT(!has_value_);
    value_ = std::move(value);
    has_value_ = true;
    waiter_.Wake();
  }

  bool is_set() const { return has_value_; }

 private:
  std::string DebugTag() {
    return absl::StrCat(Activity::current()->DebugTag(), " LATCH[0x",
                        reinterpret_cast<uintptr_t>(this), "]: ");
  }

  std::string StateString() {
    return absl::StrCat("has_value:", has_value_ ? "true" : "false",
                        " waiter:", waiter_.DebugString());
  }

  // The value stored (if has_value_ is true), otherwise some random value, we
  // don't care.
  // Why not absl::optional<>? Writing things this way lets us compress
  // has_value_ with waiter_ and leads to some significant memory savings for
  // some scenarios.
  GPR_NO_UNIQUE_ADDRESS T value_;
  // True if we have a value set, false otherwise.
  bool has_value_ = false;
#ifndef NDEBUG
  // Has this latch ever had waiters.
  bool has_had_waiters_ = false;
#endif
  IntraActivityWaiter waiter_;
};

// Specialization for void.
template <>
class Latch<void> {
 public:
  Latch() = default;
  Latch(const Latch&) = delete;
  Latch& operator=(const Latch&) = delete;
  Latch(Latch&& other) noexcept : is_set_(other.is_set_) {
#ifndef NDEBUG
    GPR_DEBUG_ASSERT(!other.has_had_waiters_);
#endif
  }
  Latch& operator=(Latch&& other) noexcept {
#ifndef NDEBUG
    GPR_DEBUG_ASSERT(!other.has_had_waiters_);
#endif
    is_set_ = other.is_set_;
    return *this;
  }

  // Produce a promise to wait for this latch.
  auto Wait() {
#ifndef NDEBUG
    has_had_waiters_ = true;
#endif
    return [this]() -> Poll<Empty> {
      if (grpc_trace_promise_primitives.enabled()) {
        gpr_log(GPR_INFO, "%sPollWait %s", DebugTag().c_str(),
                StateString().c_str());
      }
      if (is_set_) {
        return Empty{};
      } else {
        return waiter_.pending();
      }
    };
  }

  // Set the latch. Can only be called once.
  void Set() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%sSet %s", DebugTag().c_str(), StateString().c_str());
    }
    GPR_DEBUG_ASSERT(!is_set_);
    is_set_ = true;
    waiter_.Wake();
  }

 private:
  std::string DebugTag() {
    return absl::StrCat(Activity::current()->DebugTag(), " LATCH(void)[0x",
                        reinterpret_cast<uintptr_t>(this), "]: ");
  }

  std::string StateString() {
    return absl::StrCat("is_set:", is_set_ ? "true" : "false",
                        " waiter:", waiter_.DebugString());
  }

  // True if we have a value set, false otherwise.
  bool is_set_ = false;
#ifndef NDEBUG
  // Has this latch ever had waiters.
  bool has_had_waiters_ = false;
#endif
  IntraActivityWaiter waiter_;
};

// A Latch that can have its value observed by outside threads, but only waited
// upon from inside a single activity.
template <typename T>
class ExternallyObservableLatch;

template <>
class ExternallyObservableLatch<void> {
 public:
  ExternallyObservableLatch() = default;
  ExternallyObservableLatch(const ExternallyObservableLatch&) = delete;
  ExternallyObservableLatch& operator=(const ExternallyObservableLatch&) =
      delete;

  // Produce a promise to wait for this latch.
  auto Wait() {
    return [this]() -> Poll<Empty> {
      if (grpc_trace_promise_primitives.enabled()) {
        gpr_log(GPR_INFO, "%sPollWait %s", DebugTag().c_str(),
                StateString().c_str());
      }
      if (IsSet()) {
        return Empty{};
      } else {
        return waiter_.pending();
      }
    };
  }

  // Set the latch.
  void Set() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%sSet %s", DebugTag().c_str(), StateString().c_str());
    }
    is_set_.store(true, std::memory_order_relaxed);
    waiter_.Wake();
  }

  bool IsSet() const { return is_set_.load(std::memory_order_relaxed); }

  void Reset() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%sReset %s", DebugTag().c_str(),
              StateString().c_str());
    }
    is_set_.store(false, std::memory_order_relaxed);
  }

 private:
  std::string DebugTag() {
    return absl::StrCat(Activity::current()->DebugTag(), " LATCH(void)[0x",
                        reinterpret_cast<uintptr_t>(this), "]: ");
  }

  std::string StateString() {
    return absl::StrCat(
        "is_set:", is_set_.load(std::memory_order_relaxed) ? "true" : "false",
        " waiter:", waiter_.DebugString());
  }

  // True if we have a value set, false otherwise.
  std::atomic<bool> is_set_{false};
  IntraActivityWaiter waiter_;
};

template <typename T>
using LatchWaitPromise = decltype(std::declval<Latch<T>>().Wait());

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_LATCH_H
