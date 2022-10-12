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

#ifndef GRPC_CORE_LIB_PROMISE_LATCH_H
#define GRPC_CORE_LIB_PROMISE_LATCH_H

#include <grpc/support/port_platform.h>

#include <grpc/support/log.h>

#include "src/core/lib/promise/intra_activity_waiter.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

// Latch provides a single set waitable object.
// Initially the Latch is unset.
// It can be waited upon by the Wait method, which produces a Promise that
// resolves when the Latch is Set to a value of type T.
template <typename T>
class Latch {
 public:
  // This is the type of the promise returned by Wait.
  class WaitPromise {
   public:
    Poll<T*> operator()() const {
      if (latch_->has_value_) {
        return &latch_->value_;
      } else {
        return latch_->waiter_.pending();
      }
    }

   private:
    friend class Latch;
    explicit WaitPromise(Latch* latch) : latch_(latch) {}
    Latch* latch_;
  };

  Latch() = default;
  Latch(const Latch&) = delete;
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
  WaitPromise Wait() {
#ifndef NDEBUG
    has_had_waiters_ = true;
#endif
    return WaitPromise(this);
  }

  // Set the value of the latch. Can only be called once.
  void Set(T value) {
    GPR_DEBUG_ASSERT(!has_value_);
    value_ = std::move(value);
    has_value_ = true;
    waiter_.Wake();
  }

 private:
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

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_LATCH_H
