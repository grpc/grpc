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

// PromiseFactory is an adaptor class.
//
// Where a Promise is a thing that's polled periodically, a PromiseFactory
// creates a Promise. Within this Promise/Activity framework, PromiseFactory's
// then provide the edges for computation -- invoked at state transition
// boundaries to provide the new steady state.
//
// A PromiseFactory formally is either f(A) -> Promise<T> for some types A & T.
// This get a bit awkward and inapproprate to write however, and so the type
// contained herein can adapt various kinds of callable into the correct form.
// Of course a callable of a single argument returning a Promise will see an
// identity translation. One taking no arguments and returning a Promise
// similarly.
//
// A Promise passed to a PromiseFactory will yield a PromiseFactory that
// returns just that Promise.
//
// Generalizing slightly, a callable taking a single argument A and returning a
// Poll<T> will yield a PromiseFactory that captures it's argument A and
// returns a Poll<T>.
//
// Since various consumers of PromiseFactory run either repeatedly through an
// overarching Promises lifetime, or just once, and we can optimize just once
// by moving the contents of the PromiseFactory, two factory methods are
// provided: Once, that can be called just once, and Repeated, that can (wait
// for it) be called Repeatedly.

#include <utility>
#include "src/core/lib/promise/poll.h"

// PromiseLike helps us deal with functors that return immediately.
// PromiseLike<F> if F returns Poll<T> is basically a no-op, where-as if F
// returns anything else, PromiseLike wraps the return of F to return a ready
// value immediately.

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
