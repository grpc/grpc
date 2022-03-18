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

#ifndef GRPC_CORE_LIB_PROMISE_TRY_SEQ_H
#define GRPC_CORE_LIB_PROMISE_TRY_SEQ_H

#include <grpc/support/port_platform.h>

#include <tuple>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/variant.h"

#include "src/core/lib/promise/detail/basic_seq.h"
#include "src/core/lib/promise/detail/status.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace promise_detail {

template <typename T, typename Ignored = void>
struct TrySeqTraitsWithSfinae {
  using UnwrappedType = T;
  using WrappedType = absl::StatusOr<T>;
  template <typename Next>
  static auto CallFactory(Next* next, T&& value)
      -> decltype(next->Once(std::forward<T>(value))) {
    return next->Once(std::forward<T>(value));
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
  static auto CallFactory(Next* next, absl::StatusOr<T>&& status)
      -> decltype(next->Once(std::move(*status))) {
    return next->Once(std::move(*status));
  }
  template <typename F, typename Elem>
  static auto CallSeqFactory(F& f, Elem&& elem, absl::StatusOr<T> value)
      -> decltype(f(std::forward<Elem>(elem), std::move(*value))) {
    return f(std::forward<Elem>(elem), std::move(*value));
  }
  template <typename Result, typename RunNext>
  static Poll<Result> CheckResultAndRunNext(absl::StatusOr<T> prior,
                                            RunNext run_next) {
    if (!prior.ok()) return Result(prior.status());
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
  static auto CallFactory(Next* next, T&&) -> decltype(next->Once()) {
    return next->Once();
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
  static auto CallFactory(Next* next, absl::Status&&)
      -> decltype(next->Once()) {
    return next->Once();
  }
  template <typename Result, typename RunNext>
  static Poll<Result> CheckResultAndRunNext(absl::Status prior,
                                            RunNext run_next) {
    if (!prior.ok()) return Result(std::move(prior));
    return run_next(std::move(prior));
  }
};

template <typename T>
using TrySeqTraits = TrySeqTraitsWithSfinae<T>;

template <typename... Fs>
using TrySeq = BasicSeq<TrySeqTraits, Fs...>;

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
promise_detail::BasicSeqIter<promise_detail::TrySeqTraits, Factory, Argument,
                             Iter>
TrySeqIter(Iter begin, Iter end, Argument argument, Factory factory) {
  return promise_detail::BasicSeqIter<promise_detail::TrySeqTraits, Factory,
                                      Argument, Iter>(
      begin, end, std::move(factory), std::move(argument));
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_TRY_SEQ_H
