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

#include <stddef.h>

#include <string>

#include "absl/types/optional.h"

namespace grpc_core {

// A type that signals a Promise is still pending and not yet completed.
// Allows writing 'return Pending{}' and with automatic conversions gets
// upgraded to a Poll<> object.
struct Pending {
  constexpr bool operator==(Pending) const { return true; }
};

// A type that contains no value. Useful for simulating 'void' in promises that
// always need to return some kind of value.
struct Empty {
  constexpr bool operator==(Empty) const { return true; }
};

// The result of polling a Promise once.
//
// Can be either pending - the Promise has not yet completed, or ready -
// indicating that the Promise has completed AND should not be polled again.
template <typename T>
class Poll {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  Poll(Pending) : value_() {}
  Poll() : value_() {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  template <typename U>
  Poll(U&& value) : value_(std::move(value)) {}

  bool pending() const { return !value_.has_value(); }
  bool ready() const { return value_.has_value(); }

  T& value() {
    GPR_DEBUG_ASSERT(ready());
    return *value_;
  }

  const T& value() const {
    GPR_DEBUG_ASSERT(ready());
    return *value_;
  }

  T* value_if_ready() {
    if (ready()) return &*value_;
    return nullptr;
  }

  const T* value_if_ready() const {
    if (ready()) return &*value_;
    return nullptr;
  }

 private:
  absl::optional<T> value_;
};

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
