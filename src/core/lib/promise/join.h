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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_JOIN_H
#define GRPC_SRC_CORE_LIB_PROMISE_JOIN_H

#include <grpc/support/port_platform.h>

#include <stdlib.h>

#include <tuple>

#include "absl/meta/type_traits.h"

#include "src/core/lib/promise/detail/join_state.h"
#include "src/core/lib/promise/map.h"

namespace grpc_core {
namespace promise_detail {

struct JoinTraits {
  template <typename T>
  using ResultType = absl::remove_reference_t<T>;
  template <typename T>
  static bool IsOk(const T&) {
    return true;
  }
  template <typename T>
  static T Unwrapped(T x) {
    return x;
  }
  template <typename R, typename T>
  static R EarlyReturn(T x) {
    abort();
  }
};

template <typename... Promises>
class Join {
 public:
  explicit Join(Promises... promises) : state_(std::move(promises)...) {}
  auto operator()() { return state_.PollOnce(); }

 private:
  JoinState<JoinTraits, Promises...> state_;
};

struct WrapInTuple {
  template <typename T>
  std::tuple<T> operator()(T x) {
    return std::make_tuple(std::move(x));
  }
};

}  // namespace promise_detail

/// Combinator to run all promises to completion, and return a tuple
/// of their results.
template <typename... Promise>
promise_detail::Join<Promise...> Join(Promise... promises) {
  return promise_detail::Join<Promise...>(std::move(promises)...);
}

template <typename F>
auto Join(F promise) {
  return Map(std::move(promise), promise_detail::WrapInTuple{});
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_JOIN_H
