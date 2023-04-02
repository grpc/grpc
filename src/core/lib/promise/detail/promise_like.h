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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_DETAIL_PROMISE_LIKE_H
#define GRPC_SRC_CORE_LIB_PROMISE_DETAIL_PROMISE_LIKE_H

#include <grpc/support/port_platform.h>

#include <utility>

#include "absl/meta/type_traits.h"

#include "src/core/lib/promise/poll.h"

// A Promise is a callable object that returns Poll<T> for some T.
// Often when we're writing code that uses promises, we end up wanting to also
// deal with code that completes instantaneously - that is, it returns some T
// where T is not Poll.
// PromiseLike wraps any callable that takes no parameters and implements the
// Promise interface. For things that already return Poll, this wrapping does
// nothing. For things that do not return Poll, we wrap the return type in Poll.
// This allows us to write things like:
//   Seq(
//     [] { return 42; },
//     ...)
// in preference to things like:
//   Seq(
//     [] { return Poll<int>(42); },
//     ...)
// or:
//   Seq(
//     [] -> Poll<int> { return 42; },
//     ...)
// leading to slightly more concise code and eliminating some rules that in
// practice people find hard to deal with.

namespace grpc_core {
namespace promise_detail {

template <typename T>
struct PollWrapper {
  static Poll<T> Wrap(T&& x) { return Poll<T>(std::forward<T>(x)); }
};

template <typename T>
struct PollWrapper<Poll<T>> {
  static Poll<T> Wrap(Poll<T>&& x) { return std::forward<Poll<T>>(x); }
};

template <typename T>
auto WrapInPoll(T&& x) -> decltype(PollWrapper<T>::Wrap(std::forward<T>(x))) {
  return PollWrapper<T>::Wrap(std::forward<T>(x));
}

template <typename F>
class PromiseLike {
 private:
  GPR_NO_UNIQUE_ADDRESS F f_;

 public:
  // NOLINTNEXTLINE - internal detail that drastically simplifies calling code.
  PromiseLike(F&& f) : f_(std::forward<F>(f)) {}
  auto operator()() -> decltype(WrapInPoll(f_())) { return WrapInPoll(f_()); }
  using Result = typename PollTraits<decltype(WrapInPoll(f_()))>::Type;
};

// T -> T, const T& -> T
template <typename T>
using RemoveCVRef = absl::remove_cv_t<absl::remove_reference_t<T>>;

}  // namespace promise_detail
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_DETAIL_PROMISE_LIKE_H
