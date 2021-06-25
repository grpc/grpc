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

#ifndef GRPC_CORE_LIB_PROMISE_SEQ_H
#define GRPC_CORE_LIB_PROMISE_SEQ_H

#include "absl/types/variant.h"
#include "src/core/lib/promise/adaptor.h"
#include "src/core/lib/promise/detail/seq.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace promise_detail {

template <typename T>
struct InfallibleSeqTraits {
  using UnwrappedType = T;
  using WrappedType = T;
  template <typename Next>
  static auto CallFactory(Next* next, T&& value)
      -> decltype(next->Once(std::forward<T>(value))) {
    return next->Once(std::forward<T>(value));
  }

  template <typename Result, typename PriorResult, typename RunNext>
  static Poll<Result> CheckResultAndRunNext(PriorResult prior,
                                            RunNext run_next) {
    return run_next(std::move(prior));
  }
};

template <typename... Fs>
using InfallibleSeq = Seq<InfallibleSeqTraits, Fs...>;

}  // namespace promise_detail

// Sequencing combinator.
// Run the first promise.
// Pass it's result to the second, and run the returned promise.
// Pass it's result to the third, and run the returned promise.
// etc
// Return the final value.
template <typename... Functors>
promise_detail::InfallibleSeq<Functors...> Seq(Functors... functors) {
  return promise_detail::InfallibleSeq<Functors...>(std::move(functors)...);
}

template <typename F>
F Seq(F functor) {
  return functor;
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_SEQ_H
