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

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include "absl/types/variant.h"

namespace grpc_core {

// A type that signals a Promise is still pending and not yet completed.
// Allows writing 'return Pending{}' and with automatic conversions gets
// upgraded to a Poll<> object.
struct Pending {
  constexpr bool operator==(Pending) const { return true; }
};

// The result of polling a Promise once.
//
// Can be either pending - the Promise has not yet completed, or ready -
// indicating that the Promise has completed AND should not be polled again.
template <typename T>
using Poll = absl::variant<Pending, T>;

// Variant of Poll that serves as a ready value
static constexpr size_t kPollReadyIdx = 1;

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

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_POLL_H
