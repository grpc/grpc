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

#ifndef GRPC_CORE_LIB_PROMISE_DETAIL_PROMISE_FACTORY_H
#define GRPC_CORE_LIB_PROMISE_DETAIL_PROMISE_FACTORY_H

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/lib/promise/detail/promise_like.h"
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

namespace grpc_core {
namespace promise_detail {

// Helper trait: given a T, and T x, is calling x() legal?
template <typename T, typename Ignored = void>
struct IsVoidCallableT {
  static constexpr bool value = false;
};
template <typename F>
struct IsVoidCallableT<F, absl::void_t<decltype(std::declval<F>()())>> {
  static constexpr bool value = true;
};

// Wrap that trait in some nice syntax.
template <typename T>
constexpr bool IsVoidCallable() {
  return IsVoidCallableT<T>::value;
}

// Given F(A,B,C,...), what's the return type?
template <typename T, typename Ignored = void>
struct ResultOfT;
template <typename F, typename... Args>
struct ResultOfT<F(Args...), absl::void_t<decltype(
                                 std::declval<F>()(std::declval<Args>()...))>> {
  using T = decltype(std::declval<F>()(std::declval<Args>()...));
};

template <typename T>
using ResultOf = typename ResultOfT<T>::T;

// Error case - we don't know what the heck to do.
// Also defines arguments:
//  A - the argument type for the PromiseFactory.
//  F - the callable that we're promoting to a PromiseFactory.
//  Ignored - must be void, here to make the enable_if machinery work.
template <typename A, typename F, typename Ignored = void>
class PromiseFactory;

// Promote a callable(A) -> T | Poll<T> to a PromiseFactory(A) -> Promise<T> by
// capturing A.
template <typename A, typename F>
class PromiseFactory<A, F,
                     absl::enable_if_t<!IsVoidCallable<ResultOf<F(A)>>()>> {
 public:
  using Arg = A;

 private:
  // Captures the promise functor and the argument passed.
  // Provides the interface of a promise.
  class Curried {
   public:
    Curried(F f, Arg arg) : f_(std::move(f)), arg_(std::move(arg)) {}
    using Result = decltype(std::declval<F>()(std::declval<Arg>()));
    Result operator()() { return f_(arg_); }

   private:
    GPR_NO_UNIQUE_ADDRESS F f_;
    GPR_NO_UNIQUE_ADDRESS Arg arg_;
  };

 public:
  using Promise = PromiseLike<Curried>;

  Promise Once(Arg arg) {
    return Promise(Curried(std::move(f_), std::move(arg)));
  }
  Promise Repeated(Arg arg) { return Promise(Curried(f_, std::move(arg))); }

  explicit PromiseFactory(F f) : f_(std::move(f)) {}

 private:
  GPR_NO_UNIQUE_ADDRESS F f_;
};

// Promote a callable() -> T|Poll<T> to a PromiseFactory(A) -> Promise<T>
// by dropping the argument passed to the factory.
template <typename A, typename F>
class PromiseFactory<A, F,
                     absl::enable_if_t<!IsVoidCallable<ResultOf<F()>>()>> {
 public:
  using Promise = PromiseLike<F>;
  using Arg = A;
  Promise Once(Arg) { return std::move(f_); }
  Promise Repeated(Arg) { return f_; }
  explicit PromiseFactory(F f) : f_(std::move(f)) {}

 private:
  GPR_NO_UNIQUE_ADDRESS PromiseLike<F> f_;
};

// Promote a callable() -> T|Poll<T> to a PromiseFactory() -> Promise<T>
template <typename F>
class PromiseFactory<void, F,
                     absl::enable_if_t<!IsVoidCallable<ResultOf<F()>>()>> {
 public:
  using Arg = void;
  using Promise = PromiseLike<F>;
  Promise Once() { return std::move(f_); }
  Promise Repeated() { return f_; }
  explicit PromiseFactory(F f) : f_(std::move(f)) {}

 private:
  GPR_NO_UNIQUE_ADDRESS PromiseLike<F> f_;
};

// Given a callable(A) -> Promise<T>, name it a PromiseFactory and use it.
template <typename A, typename F>
class PromiseFactory<A, F,
                     absl::enable_if_t<IsVoidCallable<ResultOf<F(A)>>()>> {
 public:
  using Arg = A;
  using Promise = PromiseLike<decltype(std::declval<F>()(std::declval<Arg>()))>;
  Promise Once(Arg arg) { return Promise(f_(std::move(arg))); }
  Promise Repeated(Arg arg) { return Promise(f_(std::move(arg))); }
  explicit PromiseFactory(F f) : f_(std::move(f)) {}

 private:
  GPR_NO_UNIQUE_ADDRESS F f_;
};

// Given a callable() -> Promise<T>, promote it to a
// PromiseFactory(A) -> Promise<T> by dropping the first argument.
template <typename A, typename F>
class PromiseFactory<A, F, absl::enable_if_t<IsVoidCallable<ResultOf<F()>>()>> {
 public:
  using Arg = A;
  using Promise = PromiseLike<decltype(std::declval<F>()())>;
  Promise Once(Arg) { return Promise(f_()); }
  Promise Repeated(Arg) { return Promise(f_()); }
  explicit PromiseFactory(F f) : f_(std::move(f)) {}

 private:
  GPR_NO_UNIQUE_ADDRESS F f_;
};

// Given a callable() -> Promise<T>, name it a PromiseFactory and use it.
template <typename F>
class PromiseFactory<void, F,
                     absl::enable_if_t<IsVoidCallable<ResultOf<F()>>()>> {
 public:
  using Arg = void;
  using Promise = PromiseLike<decltype(std::declval<F>()())>;
  Promise Once() { return Promise(f_()); }
  Promise Repeated() { return Promise(f_()); }
  explicit PromiseFactory(F f) : f_(std::move(f)) {}

 private:
  GPR_NO_UNIQUE_ADDRESS F f_;
};

}  // namespace promise_detail
}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_DETAIL_PROMISE_FACTORY_H
