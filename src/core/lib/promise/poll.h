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

#ifndef GRPC_CORE_LIB_PROMISE_POLL_H
#define GRPC_CORE_LIB_PROMISE_POLL_H

#include "absl/types/optional.h"

namespace grpc_core {

// A type that signals a Promise is still pending and not yet completed.
// Allows writing 'return kPending' and with automatic conversions gets upgraded
// to a Poll<> object.
enum Pending { kPending };

// The result of polling a Promise once.
//
// Can be either pending - the Promise has not yet completed, or ready -
// indicating that the Promise has completed AND should not be polled again.
template <typename T>
class Poll {
 public:
  using Type = T;

  // Disable clang-tidy lint for explicit construction here: the pending type is
  // intended to make it easy to construct an arbitrarily typed Poll instance
  // based on context and having this be an implicit constructor greatly
  // increases the ergonomics of code using this library.
  // NOLINTNEXTLINE(google-explicit-constructor)
  Poll(Pending) : value_() {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  Poll(T value) : value_(std::move(value)) {}

  // Was the poll pending?
  bool pending() const { return !ready(); }
  // Was the poll complete?
  bool ready() const { return value_.has_value(); }

  // If the poll was ready, moves out of this instance and applies f,
  //   returning a ready with the result of f.
  // If the poll was pending, returns pending.
  template <class F>
  auto Map(F f) -> Poll<decltype(f(std::declval<T&>()))> {
    using Result = decltype(f(value_.value()));
    if (auto* p = get_ready()) {
      return Poll<Result>(std::move(f(*p)));
    } else {
      return Poll<Result>(kPending);
    }
  }

  // Move the ready result out of this object.
  T take() {
    auto out = std::move(*get_ready());
    value_.reset();
    return out;
  }

  // Return nullptr if pending, or a pointer to the result if not.
  T* get_ready() {
    if (value_.has_value()) {
      return &*value_;
    } else {
      return nullptr;
    }
  }

 private:
  // Empty if pending, result if not.
  absl::optional<T> value_;
};

// Add a Poll<Pending> instance to make the constructors for Poll safer.
template <>
class Poll<Pending>;

// Helper to return a Poll<T> of some value when ready.
template <typename T>
Poll<absl::remove_reference_t<T>> ready(T value) {
  return Poll<absl::remove_reference_t<T>>(std::move(value));
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_POLL_H
