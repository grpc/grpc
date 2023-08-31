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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_POLL_H
#define GRPC_SRC_CORE_LIB_PROMISE_POLL_H

#include <grpc/support/port_platform.h>

#include <string>
#include <utility>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/construct_destruct.h"

namespace grpc_core {

// A type that signals a Promise is still pending and not yet completed.
// Allows writing 'return Pending{}' and with automatic conversions gets
// upgraded to a Poll<> object.
struct Pending {};
inline bool operator==(const Pending&, const Pending&) { return true; }

// A type that contains no value. Useful for simulating 'void' in promises that
// always need to return some kind of value.
struct Empty {};
inline bool operator==(const Empty&, const Empty&) { return true; }

// The result of polling a Promise once.
//
// Can be either pending - the Promise has not yet completed, or ready -
// indicating that the Promise has completed AND should not be polled again.
template <typename T>
class Poll {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  Poll(Pending) : ready_(false) {}
  Poll() : ready_(false) {}
  Poll(const Poll& other) : ready_(other.ready_) {
    if (ready_) Construct(&value_, other.value_);
  }
  Poll(Poll&& other) noexcept : ready_(other.ready_) {
    if (ready_) Construct(&value_, std::move(other.value_));
  }
  Poll& operator=(const Poll& other) {
    if (ready_) {
      if (other.ready_) {
        value_ = other.value_;
      } else {
        Destruct(&value_);
        ready_ = false;
      }
    } else if (other.ready_) {
      Construct(&value_, other.value_);
      ready_ = true;
    }
    return *this;
  }
  Poll& operator=(Poll&& other) noexcept {
    if (ready_) {
      if (other.ready_) {
        value_ = std::move(other.value_);
      } else {
        Destruct(&value_);
        ready_ = false;
      }
    } else if (other.ready_) {
      Construct(&value_, std::move(other.value_));
      ready_ = true;
    }
    return *this;
  }
  template <typename U>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Poll(U value) : ready_(true) {
    Construct(&value_, std::move(value));
  }
  // NOLINTNEXTLINE(google-explicit-constructor)
  Poll(T&& value) : ready_(true) { Construct(&value_, std::forward<T>(value)); }
  ~Poll() {
    if (ready_) Destruct(&value_);
  }

  bool pending() const { return !ready_; }
  bool ready() const { return ready_; }

  T& value() {
    GPR_DEBUG_ASSERT(ready());
    return value_;
  }

  const T& value() const {
    GPR_DEBUG_ASSERT(ready());
    return value_;
  }

  T* value_if_ready() {
    if (ready()) return &value_;
    return nullptr;
  }

  const T* value_if_ready() const {
    if (ready()) return &value_;
    return nullptr;
  }

 private:
  // Flag indicating readiness, followed by an optional value.
  //
  // Why not optional<T>?
  //
  // We have cases where we want to return absl::nullopt{} from a promise, and
  // have that upgraded to a Poll<absl::nullopt_t> prior to a cast to some
  // Poll<optional<T>>.
  //
  // Since optional<nullopt_t> is not allowed, we'd not be allowed to make
  // Poll<nullopt_t> and so we'd need to pollute all poll handling code with
  // some edge case handling template magic - the complexity would explode and
  // grow over time - versus hand coding the pieces we need here and containing
  // that quirk to one place.
  bool ready_;
  // We do a single element union so we can choose when to construct/destruct
  // this value.
  union {
    T value_;
  };
};

template <>
class Poll<Empty> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  Poll(Pending) : ready_(false) {}
  Poll() : ready_(false) {}
  Poll(const Poll& other) = default;
  Poll(Poll&& other) noexcept = default;
  Poll& operator=(const Poll& other) = default;
  Poll& operator=(Poll&& other) = default;
  // NOLINTNEXTLINE(google-explicit-constructor)
  Poll(Empty) : ready_(true) {}
  ~Poll() = default;

  bool pending() const { return !ready_; }
  bool ready() const { return ready_; }

  Empty value() const {
    GPR_DEBUG_ASSERT(ready());
    return Empty{};
  }

  Empty* value_if_ready() {
    static Empty value;
    if (ready()) return &value;
    return nullptr;
  }

  const Empty* value_if_ready() const {
    static Empty value;
    if (ready()) return &value;
    return nullptr;
  }

 private:
  // Flag indicating readiness.
  bool ready_;
};

// Ensure degenerate cases are not defined:

// Can't poll for a Pending
template <>
class Poll<Pending>;

// Can't poll for a poll
template <class T>
class Poll<Poll<T>>;

template <typename T>
bool operator==(const Poll<T>& a, const Poll<T>& b) {
  if (a.pending() && b.pending()) return true;
  if (a.ready() && b.ready()) return a.value() == b.value();
  return false;
}

template <typename T, typename U>
Poll<T> poll_cast(Poll<U> poll) {
  if (poll.pending()) return Pending{};
  return static_cast<T>(std::move(poll.value()));
}

// PollTraits tells us whether a type is Poll<> or some other type, and is
// leveraged in the PromiseLike/PromiseFactory machinery to select the
// appropriate implementation of those concepts based upon the return type of a
// lambda, for example (via enable_if).
template <typename T>
struct PollTraits {
  static constexpr bool is_poll() { return false; }
};

template <typename T>
struct PollTraits<Poll<T>> {
  using Type = T;
  static constexpr bool is_poll() { return true; }
};

// Convert a poll to a string
template <typename T, typename F>
std::string PollToString(
    const Poll<T>& poll,
    F t_to_string = [](const T& t) { return t.ToString(); }) {
  if (poll.pending()) {
    return "<<pending>>";
  }
  return t_to_string(poll.value());
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_POLL_H
