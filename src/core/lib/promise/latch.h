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

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

template <typename T>
class Latch {
 public:
  class WaitPromise {
   public:
    Poll<T*> operator()() const {
      if (latch_->value_.has_value()) {
        return ready(&*latch_->value_);
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
  Latch(Latch&& other) noexcept : value_(std::move(other.value_)) {
    assert(!other.has_had_waiters_);
  }
  Latch& operator=(Latch&& other) noexcept {
    assert(!other.has_had_waiters_);
    value_ = std::move(other.value_);
    return *this;
  }

  WaitPromise Wait() {
    has_had_waiters_ = true;
    return WaitPromise(this);
  }

  void Set(T value) {
    assert(!value_.has_value());
    value_ = std::move(value);
    waiter_.Wake();
  }

 private:
  absl::optional<T> value_;
  bool has_had_waiters_ = false;
  IntraActivityWaiter waiter_;
};

}  // namespace grpc_core

#endif
