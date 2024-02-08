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

#include <utility>

#include "src/core/lib/gprpp/debug_location.h"
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
  static const char* ErrorString(const T&) { abort(); }
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
  explicit Seq(P&& promise, Fs&&... factories, DebugLocation whence)
      : state_(std::forward<P>(promise), std::forward<Fs>(factories)...,
               whence) {}

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
template <typename F>
F Seq(F functor) {
  return functor;
}

template <typename F0, typename F1>
promise_detail::Seq<F0, F1> Seq(F0 f0, F1 f1, DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1>(std::move(f0), std::move(f1), whence);
}

template <typename F0, typename F1, typename F2>
promise_detail::Seq<F0, F1, F2> Seq(F0 f0, F1 f1, F2 f2,
                                    DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2>(std::move(f0), std::move(f1),
                                         std::move(f2), whence);
}

template <typename F0, typename F1, typename F2, typename F3>
promise_detail::Seq<F0, F1, F2, F3> Seq(F0 f0, F1 f1, F2 f2, F3 f3,
                                        DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4>
promise_detail::Seq<F0, F1, F2, F3, F4> Seq(F0 f0, F1 f1, F2 f2, F3 f3, F4 f4,
                                            DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4>(std::move(f0), std::move(f1),
                                                 std::move(f2), std::move(f3),
                                                 std::move(f4), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5>
promise_detail::Seq<F0, F1, F2, F3, F4, F5> Seq(F0 f0, F1 f1, F2 f2, F3 f3,
                                                F4 f4, F5 f5,
                                                DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5, typename F6>
promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6> Seq(F0 f0, F1 f1, F2 f2, F3 f3,
                                                    F4 f4, F5 f5, F6 f6,
                                                    DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), std::move(f6), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5, typename F6, typename F7>
promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7> Seq(
    F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7,
    DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), std::move(f6), std::move(f7), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5, typename F6, typename F7, typename F8>
promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8> Seq(
    F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8,
    DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), std::move(f6), std::move(f7), std::move(f8), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5, typename F6, typename F7, typename F8, typename F9>
promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9> Seq(
    F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9,
    DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), std::move(f6), std::move(f7), std::move(f8), std::move(f9),
      whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5, typename F6, typename F7, typename F8, typename F9,
          typename F10>
promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10> Seq(
    F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9,
    F10 f10, DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), std::move(f6), std::move(f7), std::move(f8), std::move(f9),
      std::move(f10), whence);
}

template <typename F0, typename F1, typename F2, typename F3, typename F4,
          typename F5, typename F6, typename F7, typename F8, typename F9,
          typename F10, typename F11>
promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11> Seq(
    F0 f0, F1 f1, F2 f2, F3 f3, F4 f4, F5 f5, F6 f6, F7 f7, F8 f8, F9 f9,
    F10 f10, F11 f11, DebugLocation whence = {}) {
  return promise_detail::Seq<F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11>(
      std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4),
      std::move(f5), std::move(f6), std::move(f7), std::move(f8), std::move(f9),
      std::move(f10), std::move(f11), whence);
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
