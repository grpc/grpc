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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_TRY_SEQ_H
#define GRPC_SRC_CORE_LIB_PROMISE_TRY_SEQ_H

#include <grpc/support/port_platform.h>

#include <stdlib.h>

#include <type_traits>
#include <utility>

#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "src/core/lib/promise/detail/basic_seq.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/detail/seq_state.h"
#include "src/core/lib/promise/detail/status.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace promise_detail {

template <typename T, typename Ignored = void>
struct TrySeqTraitsWithSfinae {
  using UnwrappedType = T;
  using WrappedType = absl::StatusOr<T>;
  template <typename Next>
  static auto CallFactory(Next* next, T&& value) {
    return next->Make(std::forward<T>(value));
  }
  static bool IsOk(const T&) { return true; }
  template <typename R>
  static R ReturnValue(T&&) {
    abort();
  }
  template <typename F, typename Elem>
  static auto CallSeqFactory(F& f, Elem&& elem, T&& value)
      -> decltype(f(std::forward<Elem>(elem), std::forward<T>(value))) {
    return f(std::forward<Elem>(elem), std::forward<T>(value));
  }
  template <typename Result, typename RunNext>
  static Poll<Result> CheckResultAndRunNext(T prior, RunNext run_next) {
    return run_next(std::move(prior));
  }
};

template <typename T>
struct TrySeqTraitsWithSfinae<absl::StatusOr<T>> {
  using UnwrappedType = T;
  using WrappedType = absl::StatusOr<T>;
  template <typename Next>
  static auto CallFactory(Next* next, absl::StatusOr<T>&& status) {
    return next->Make(std::move(*status));
  }
  static bool IsOk(const absl::StatusOr<T>& status) { return status.ok(); }
  template <typename R>
  static R ReturnValue(absl::StatusOr<T>&& status) {
    return StatusCast<R>(status.status());
  }
  template <typename F, typename Elem>
  static auto CallSeqFactory(F& f, Elem&& elem, absl::StatusOr<T> value)
      -> decltype(f(std::forward<Elem>(elem), std::move(*value))) {
    return f(std::forward<Elem>(elem), std::move(*value));
  }
  template <typename Result, typename RunNext>
  static Poll<Result> CheckResultAndRunNext(absl::StatusOr<T> prior,
                                            RunNext run_next) {
    if (!prior.ok()) return StatusCast<Result>(prior.status());
    return run_next(std::move(prior));
  }
};
// If there exists a function 'IsStatusOk(const T&) -> bool' then we assume that
// T is a status type for the purposes of promise sequences, and a non-OK T
// should terminate the sequence and return.
template <typename T>
struct TrySeqTraitsWithSfinae<
    T, absl::enable_if_t<
           std::is_same<decltype(IsStatusOk(std::declval<T>())), bool>::value,
           void>> {
  using UnwrappedType = void;
  using WrappedType = T;
  template <typename Next>
  static auto CallFactory(Next* next, T&&) {
    return next->Make();
  }
  static bool IsOk(const T& status) { return IsStatusOk(status); }
  template <typename R>
  static R ReturnValue(T&& status) {
    return R(std::move(status));
  }
  template <typename Result, typename RunNext>
  static Poll<Result> CheckResultAndRunNext(T prior, RunNext run_next) {
    if (!IsStatusOk(prior)) return Result(std::move(prior));
    return run_next(std::move(prior));
  }
};
template <>
struct TrySeqTraitsWithSfinae<absl::Status> {
  using UnwrappedType = void;
  using WrappedType = absl::Status;
  template <typename Next>
  static auto CallFactory(Next* next, absl::Status&&) {
    return next->Make();
  }
  static bool IsOk(const absl::Status& status) { return status.ok(); }
  template <typename R>
  static R ReturnValue(absl::Status&& status) {
    return StatusCast<R>(std::move(status));
  }
  template <typename Result, typename RunNext>
  static Poll<Result> CheckResultAndRunNext(absl::Status prior,
                                            RunNext run_next) {
    if (!prior.ok()) return StatusCast<Result>(std::move(prior));
    return run_next(std::move(prior));
  }
};

template <typename T>
using TrySeqTraits = TrySeqTraitsWithSfinae<T>;

template <typename P, typename... Fs>
class TrySeq {
 public:
  explicit TrySeq(P&& promise, Fs&&... factories)
      : state_(std::forward<P>(promise), std::forward<Fs>(factories)...) {}

  auto operator()() { return state_.PollOnce(); }

 private:
  SeqState<TrySeqTraits, P, Fs...> state_;
};

template <typename I, typename F, typename Arg>
struct TrySeqIterTraits {
  using Iter = I;
  using Factory = F;
  using Argument = Arg;
  using IterValue = decltype(*std::declval<Iter>());
  using StateCreated = decltype(std::declval<F>()(std::declval<IterValue>(),
                                                  std::declval<Arg>()));
  using State = PromiseLike<StateCreated>;
  using Wrapped = typename State::Result;

  using Traits = TrySeqTraits<Wrapped>;
};

template <typename Iter, typename Factory, typename Argument>
struct TrySeqIterResultTraits {
  using IterTraits = TrySeqIterTraits<Iter, Factory, Argument>;
  using Result = BasicSeqIter<IterTraits>;
};

}  // namespace promise_detail

// Try a sequence of operations.
// * Run the first functor as a promise.
// * Feed its success result into the second functor to create a promise,
//   then run that.
// * ...
// * Feed the second-final success result into the final functor to create a
//   promise, then run that, with the overall success result being that
//   promises success result.
// If any step fails, fail everything.
// Functors can return StatusOr<> to signal that a value is fed forward, or
// Status to indicate only success/failure. In the case of returning Status,
// the construction functors take no arguments.
template <typename... Functors>
promise_detail::TrySeq<Functors...> TrySeq(Functors... functors) {
  return promise_detail::TrySeq<Functors...>(std::move(functors)...);
}

// Try a sequence of operations of unknown length.
// Asynchronously:
//   for (element in (begin, end)) {
//     auto r = wait_for factory(element, argument);
//     if (!r.ok()) return r;
//     argument = *r;
//   }
//   return argument;
template <typename Iter, typename Factory, typename Argument>
typename promise_detail::TrySeqIterResultTraits<Iter, Factory, Argument>::Result
TrySeqIter(Iter begin, Iter end, Argument argument, Factory factory) {
  using Result =
      typename promise_detail::TrySeqIterResultTraits<Iter, Factory,
                                                      Argument>::Result;
  return Result(begin, end, std::move(factory), std::move(argument));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_TRY_SEQ_H
