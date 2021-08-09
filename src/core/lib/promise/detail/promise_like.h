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

#ifndef GRPC_CORE_LIB_PROMISE_DETAIL_PROMISE_LIKE_H
#define GRPC_CORE_LIB_PROMISE_DETAIL_PROMISE_LIKE_H

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/lib/promise/poll.h"

#include <utility>
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

template <typename T, typename Ignored = void>
class PromiseLike;

template <typename F>
class PromiseLike<F, typename absl::enable_if_t<PollTraits<decltype(
                         std::declval<F>()())>::is_poll()>> {
 private:
  GPR_NO_UNIQUE_ADDRESS F f_;

 public:
  explicit PromiseLike(F&& f) : f_(std::forward<F>(f)) {}
  using Result = typename PollTraits<decltype(f_())>::Type;
  Poll<Result> operator()() { return f_(); }
};

template <typename F>
class PromiseLike<F, typename absl::enable_if_t<!PollTraits<decltype(
                         std::declval<F>()())>::is_poll()>> {
 private:
  GPR_NO_UNIQUE_ADDRESS F f_;

 public:
  explicit PromiseLike(F&& f) : f_(std::forward<F>(f)) {}
  using Result = decltype(f_());
  Poll<Result> operator()() { return f_(); }
};

}  // namespace promise_detail
}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_DETAIL_PROMISE_LIKE_H
