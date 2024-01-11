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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_ALL_OK_H
#define GRPC_SRC_CORE_LIB_PROMISE_ALL_OK_H

#include <grpc/support/port_platform.h>

#include <tuple>
#include <variant>

#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "src/core/lib/promise/detail/join_state.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/status_flag.h"

namespace grpc_core {

namespace promise_detail {

// Traits object to pass to JoinState
template <typename Result>
struct AllOkTraits {
  template <typename T>
  using ResultType = Result;
  template <typename T>
  static bool IsOk(const T& x) {
    return IsStatusOk(x);
  }
  static Empty Unwrapped(StatusFlag) { return Empty{}; }
  static Empty Unwrapped(absl::Status) { return Empty{}; }
  template <typename R, typename T>
  static R EarlyReturn(T&& x) {
    return StatusCast<R>(std::forward<T>(x));
  }
  template <typename... A>
  static Result FinalReturn(A&&...) {
    return Result{};
  }
};

// Implementation of AllOk combinator.
template <typename Result, typename... Promises>
class AllOk {
 public:
  explicit AllOk(Promises... promises) : state_(std::move(promises)...) {}
  auto operator()() { return state_.PollOnce(); }

 private:
  JoinState<AllOkTraits<Result>, Promises...> state_;
};

}  // namespace promise_detail

// Run all promises.
// If any fail, cancel the rest and return the failure.
// If all succeed, return Ok.
template <typename Result, typename... Promises>
auto AllOk(Promises... promises) {
  return promise_detail::AllOk<Result, Promises...>(std::move(promises)...);
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_ALL_OK_H
