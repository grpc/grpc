// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_OBSERVABLE_H
#define GRPC_SRC_CORE_LIB_PROMISE_OBSERVABLE_H

#include "absl/container/flat_hash_set.h"

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

template <typename T>
class Observable {
 public:
  explicit Observable(T initial)
      : state_(MakeRefCounted<State>(std::move(initial))) {}

  void Set(T value) { state_->Set(std::move(value)); }

  auto Next(T current) { return Observer(state_, std::move(current)); }

 private:
  class Observer;
  class State : public RefCounted<State> {
   public:
    explicit State(T value) : value_(std::move(value)) {}

    void Set(T value) {
      MutexLock lock(&mu_);
      std::swap(value_, value);
      WakeAll();
    }

    Mutex* mu() ABSL_LOCK_RETURNED(mu_) { return &mu_; }

    const T& current() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
      return value_;
    }

    void Remove(Observer* observer) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
      observers_.erase(observer);
    }

    GRPC_MUST_USE_RESULT Waker Add(Observer* observer)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
      observers_.insert(observer);
      return Activity::current()->MakeNonOwningWaker();
    }

   private:
    void WakeAll() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
      for (auto* observer : observers_) {
        observer->Wakeup();
      }
    }

    Mutex mu_;
    absl::flat_hash_set<Observer*> observers_ ABSL_GUARDED_BY(mu_);
    T value_ ABSL_GUARDED_BY(mu_);
  };

  class Observer {
   public:
    Observer(RefCountedPtr<State> state, T current)
        : state_(std::move(state)), current_(std::move(current)) {}
    ~Observer() {
      MutexLock lock(state_->mu());
      if (!waker_.is_unwakeable()) state_->Remove(this);
    }

    Observer(const Observer&) = delete;
    Observer& operator=(const Observer&) = delete;
    Observer(Observer&& other) noexcept
        : state_(std::move(other.state_)), current_(std::move(other.current_)) {
      GPR_ASSERT(other.waker_.is_unwakeable());
    }
    Observer& operator=(Observer&& other) noexcept = delete;

    void Wakeup() { waker_.Wakeup(); }

    Poll<T> operator()() {
      MutexLock lock(state_->mu());
      if (current_ != state_->current()) {
        if (!waker_.is_unwakeable()) state_->Remove(this);
        return state_->current();
      }
      if (waker_.is_unwakeable()) waker_ = state_->Add(this);
      return Pending{};
    }

   private:
    RefCountedPtr<State> state_;
    T current_;
    Waker waker_;
  };

  RefCountedPtr<State> state_;
};

}  // namespace grpc_core

#endif
