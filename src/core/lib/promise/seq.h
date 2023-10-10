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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_SEQ_H
#define GRPC_SRC_CORE_LIB_PROMISE_SEQ_H

#include <grpc/support/port_platform.h>

#include <stdlib.h>

#include <type_traits>
#include <utility>

#include "src/core/lib/promise/detail/basic_seq.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/detail/seq_state.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace promise_detail {

template <typename T>
struct SeqTraits {
  using UnwrappedType = T;
  using WrappedType = T;
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
  static auto CallSeqFactory(F& f, Elem&& elem, T&& value) {
    return f(std::forward<Elem>(elem), std::forward<T>(value));
  }
  template <typename Result, typename PriorResult, typename RunNext>
  static Poll<Result> CheckResultAndRunNext(PriorResult prior,
                                            RunNext run_next) {
    return run_next(std::move(prior));
  }
};

template <typename P, typename... Fs>
class Seq {
 public:
  explicit Seq(P&& promise, Fs&&... factories)
      : state_(std::forward<P>(promise), std::forward<Fs>(factories)...) {}

  auto operator()() { return state_.PollOnce(); }

 private:
  SeqState<SeqTraits, P, Fs...> state_;
};

template <typename I, typename F, typename Arg>
struct SeqIterTraits {
  using Iter = I;
  using Factory = F;
  using Argument = Arg;
  using IterValue = decltype(*std::declval<Iter>());
  using StateCreated = decltype(std::declval<F>()(std::declval<IterValue>(),
                                                  std::declval<Arg>()));
  using State = PromiseLike<StateCreated>;
  using Wrapped = typename State::Result;

  using Traits = SeqTraits<Wrapped>;
};

template <typename Iter, typename Factory, typename Argument>
struct SeqIterResultTraits {
  using IterTraits = SeqIterTraits<Iter, Factory, Argument>;
  using Result = BasicSeqIter<IterTraits>;
};

}  // namespace promise_detail

// Sequencing combinator.
// Run the first promise.
// Pass its result to the second, and run the returned promise.
// Pass its result to the third, and run the returned promise.
// etc
// Return the final value.
template <typename... Functors>
promise_detail::Seq<Functors...> Seq(Functors... functors) {
  return promise_detail::Seq<Functors...>(std::move(functors)...);
}

template <typename F>
F Seq(F functor) {
  return functor;
}

// Execute a sequence of operations of unknown length.
// Asynchronously:
//   for (element in (begin, end)) {
//     argument = wait_for factory(element, argument);
//   }
//   return argument;
template <typename Iter, typename Factory, typename Argument>
typename promise_detail::SeqIterResultTraits<Iter, Factory, Argument>::Result
SeqIter(Iter begin, Iter end, Argument argument, Factory factory) {
  using Result = typename promise_detail::SeqIterResultTraits<Iter, Factory,
                                                              Argument>::Result;
  return Result(begin, end, std::move(factory), std::move(argument));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_SEQ_H
