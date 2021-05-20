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

#ifndef GRPC_CORE_LIB_PROMISE_OBSERVABLE_H
#define GRPC_CORE_LIB_PROMISE_OBSERVABLE_H

#include <limits>
#include "absl/types/optional.h"
#include "src/core/lib/promise/activity.h"

namespace grpc_core {

namespace observable_detail {

// Shared state between Observable and Observer.
template <typename T>
class ObservableState {
 public:
  explicit ObservableState(absl::optional<T> value) : value_(value) {}

  void Close() {
    // Publish that we're closed.
    waiters_.mu()->Lock();
    version_ = TOMBSTONE_VERSION;
    value_.reset();
    waiters_.WakeAllAndUnlock();
  }

  void Push(T value) {
    waiters_.mu()->Lock();
    version_++;
    value_ = std::move(value);
    waiters_.WakeAllAndUnlock();
  }

  Poll<absl::optional<T>> PollGet(uint64_t* version_seen) {
    absl::MutexLock lock(waiters_.mu());
    if (!value_.has_value()) {
      if (version_ != TOMBSTONE_VERSION) {
        // We allow initial no-value, which does not indicate closure.
        return waiters_.pending();
      }
    }
    *version_seen = version_;
    return ready(value_);
  }

  Poll<absl::optional<T>> PollNext(uint64_t* version_seen) {
    absl::MutexLock lock(waiters_.mu());
    if (!value_.has_value()) {
      if (version_ != TOMBSTONE_VERSION) {
        // We allow initial no-value, which does not indicate closure.
        return waiters_.pending();
      }
    }
    if (version_ == *version_seen) {
      return waiters_.pending();
    }
    *version_seen = version_;
    return ready(value_);
  }

 private:
  WaitSet waiters_;
  uint64_t version_ GUARDED_BY(waiters_.mu()) = 1;
  static constexpr uint64_t TOMBSTONE_VERSION =
      std::numeric_limits<decltype(version_)>::max();
  absl::optional<T> value_ GUARDED_BY(waiters_.mu());
};

// Promise implementation for Observer::Get.
template <typename T>
class Get {
 public:
  Get(uint64_t* version_seen, ObservableState<T>* state)
      : version_seen_(version_seen), state_(state) {}

  Poll<absl::optional<T>> operator()() {
    return state_->PollGet(version_seen_);
  }

 private:
  uint64_t* version_seen_;
  ObservableState<T>* state_;
};

// Promise implementation for Observer::Next.
template <typename T>
class Next {
 public:
  Next(uint64_t* version_seen, ObservableState<T>* state)
      : version_seen_(version_seen), state_(state) {}

  Poll<absl::optional<T>> operator()() {
    return state_->PollNext(version_seen_);
  }

 private:
  uint64_t* version_seen_;
  ObservableState<T>* state_;
};

}  // namespace observable_detail

template <typename T>
class Observable;

// Observer watches an Observable for updates.
// It can see either the latest value or wait for a new value, but is not
// guaranteed to see every value pushed to the Observable.
template <typename T>
class Observer {
 public:
  Observer(const Observer&) = delete;
  Observer& operator=(const Observer&) = delete;
  Observer(Observer&& other) noexcept
      : version_seen_(other.version_seen_), state_(std::move(other.state_)) {}
  Observer& operator=(Observer&& other) noexcept {
    version_seen_ = other.version_seen_;
    state_ = std::move(other.state_);
    return *this;
  }

  // Return a promise that will produce an optional<T>.
  // If the Observable is still present, this will be a value T, but if the
  // Observable has been closed, this will be nullopt. Borrows data from the
  // Observer, so this value must stay valid until the promise is resolved. Only
  // one Next, Get call is allowed to be outstanding at a time.
  observable_detail::Get<T> Get() {
    return observable_detail::Get<T>{&version_seen_, &*state_};
  }

  // Return a promise that will produce the next unseen value as an optional<T>.
  // If the Observable is still present, this will be a value T, but if the
  // Observable has been closed, this will be nullopt. Borrows data from the
  // Observer, so this value must stay valid until the promise is resolved. Only
  // one Next, Get call is allowed to be outstanding at a time.
  observable_detail::Next<T> Next() {
    return observable_detail::Next<T>{&version_seen_, &*state_};
  }

 private:
  using State = observable_detail::ObservableState<T>;
  friend class Observable<T>;
  explicit Observer(std::shared_ptr<State> state) : state_(state) {}
  uint64_t version_seen_ = 0;
  std::shared_ptr<State> state_;
};

// Observable models a single writer multiple reader broadcast channel.
// Readers can observe the latest value, or await a new latest value, but they
// are not guaranteed to observe every value.
template <typename T>
class Observable {
 public:
  Observable() : state_(std::make_shared<State>(absl::nullopt)) {}
  explicit Observable(T value)
      : state_(std::make_shared<State>(std::move(value))) {}
  ~Observable() { state_->Close(); }
  Observable(const Observable&) = delete;
  Observable& operator=(const Observable&) = delete;

  // Push a new value into the observable.
  void Push(T value) { state_->Push(std::move(value)); }

  // Create a new Observer
  Observer<T> MakeObserver() { return Observer<T>(state_); }

 private:
  using State = observable_detail::ObservableState<T>;
  std::shared_ptr<State> state_;
};

}  // namespace grpc_core

#endif
