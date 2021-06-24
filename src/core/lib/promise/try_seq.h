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

#include <tuple>
#include "absl/status/statusor.h"
#include "absl/types/variant.h"
#include "src/core/lib/promise/adaptor.h"
#include "src/core/lib/promise/detail/seq.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace promise_detail {

struct TrySeqTraits {
  template <typename T>
  struct NextArg;
  template <typename T>
  struct NextArg<absl::StatusOr<T>> {
    using Type = T;
    template <typename Next>
    static auto CallFactory(Next* next, absl::StatusOr<T>&& status)
        -> decltype(next->Once(std::move(*status))) {
      return next->Once(std::move(*status));
    }
  };
  template <>
  struct NextArg<absl::Status> {
    using Type = void;
    template <typename Next>
    static auto CallFactory(Next* next, absl::Status&& status)
        -> decltype(next->Once()) {
      return next->Once();
    }
  };

  template <typename Result, typename PriorResult, typename RunNext>
  static Poll<Result> CheckResultAndRunNext(PriorResult prior,
                                            RunNext run_next) {
    if (!prior.ok()) return Result(IntoStatus(&prior));
    return run_next(std::move(prior));
  }
};

template <typename... Fs>
using TrySeq = Seq<TrySeqTraits, Fs...>;

}  // namespace promise_detail

// Try a sequence of operations.
// * Run the first functor as a promise.
// * Feed the it's success result into the second functor to create a promise,
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

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_TRY_SEQ_H
