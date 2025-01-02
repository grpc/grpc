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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_TRY_JOIN_H
#define GRPC_SRC_CORE_LIB_PROMISE_TRY_JOIN_H

#include <grpc/support/port_platform.h>

#include <tuple>
#include <variant>

#include "absl/log/check.h"
#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/core/lib/promise/detail/join_state.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/status_flag.h"

namespace grpc_core {

namespace promise_detail {

// Extract the T from a StatusOr<T>
template <typename T>
T IntoResult(absl::StatusOr<T>* status) {
  return std::move(**status);
}

// TryJoin returns a StatusOr<tuple<A,B,C>> for f()->Poll<StatusOr<A>>,
// g()->Poll<StatusOr<B>>, h()->Poll<StatusOr<C>>. If one of those should be a
// Status instead, we need a placeholder type to return, and this is it.
inline Empty IntoResult(absl::Status*) { return Empty{}; }

// Traits object to pass to BasicJoin
template <template <typename> class Result>
struct TryJoinTraits {
  template <typename T>
  using ResultType = Result<absl::remove_reference_t<T>>;
  template <typename T>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static bool IsOk(
      const absl::StatusOr<T>& x) {
    return x.ok();
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static bool IsOk(const absl::Status& x) {
    return x.ok();
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static bool IsOk(StatusFlag x) {
    return x.ok();
  }
  template <typename T>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static bool IsOk(
      const ValueOrFailure<T>& x) {
    return x.ok();
  }
  template <typename T>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static T Unwrapped(absl::StatusOr<T> x) {
    return std::move(*x);
  }
  template <typename T>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static T Unwrapped(ValueOrFailure<T> x) {
    return std::move(*x);
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static Empty Unwrapped(absl::Status) {
    return Empty{};
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static Empty Unwrapped(StatusFlag) {
    return Empty{};
  }
  template <typename R, typename T>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static R EarlyReturn(
      absl::StatusOr<T> x) {
    return x.status();
  }
  template <typename R>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static R EarlyReturn(absl::Status x) {
    return FailureStatusCast<R>(std::move(x));
  }
  template <typename R>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static R EarlyReturn(StatusFlag x) {
    return FailureStatusCast<R>(x);
  }
  template <typename R, typename T>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static R EarlyReturn(
      const ValueOrFailure<T>& x) {
    CHECK(!x.ok());
    return FailureStatusCast<R>(Failure{});
  }
  template <typename... A>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static auto FinalReturn(A&&... a) {
    return Result<std::tuple<A...>>(std::make_tuple(std::forward<A>(a)...));
  }
};

// Implementation of TryJoin combinator.
template <template <typename> class R, typename... Promises>
class TryJoin {
 public:
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION explicit TryJoin(Promises... promises)
      : state_(std::move(promises)...) {}
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto operator()() {
    return state_.PollOnce();
  }

 private:
  JoinState<TryJoinTraits<R>, Promises...> state_;
};

template <template <typename> class R>
struct WrapInStatusOrTuple {
  template <typename T>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION R<std::tuple<T>> operator()(R<T> x) {
    if (!x.ok()) return x.status();
    return std::make_tuple(std::move(*x));
  }
};

}  // namespace promise_detail

// Run all promises.
// If any fail, cancel the rest and return the failure.
// If all succeed, return Ok(tuple-of-results).
template <template <typename> class R, typename... Promises>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline promise_detail::TryJoin<R,
                                                                    Promises...>
TryJoin(Promises... promises) {
  return promise_detail::TryJoin<R, Promises...>(std::move(promises)...);
}

template <template <typename> class R, typename F>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline auto TryJoin(F promise) {
  return Map(promise, promise_detail::WrapInStatusOrTuple<R>{});
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_TRY_JOIN_H
