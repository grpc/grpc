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
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace seq_detail {

template <typename F0, typename F1>
struct InitialState {
  InitialState(F0&& f0, F1&& f1)
      : f(std::forward<F0>(f0)), next(std::forward<F1>(f1)) {}
  InitialState(InitialState&& other)
      : f(std::move(other.f)), next(std::move(other.next)) {}
  InitialState(const InitialState& other) : f(other.f), next(other.next) {}
  ~InitialState() = delete;
  using F = F0;
  [[no_unique_address]] F0 f;
  using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
  using Next = adaptor_detail::Factory<FResult, F1>;
  [[no_unique_address]] Next next;
};

template <typename PriorState, typename FNext, typename... PriorStateFs>
struct IntermediateState {
  IntermediateState(PriorStateFs&&... prior_fs, FNext&& f)
      : next(std::forward<FNext>(f)) {
    new (&prior) PriorState(std::forward<PriorStateFs>(prior_fs)...);
  }
  IntermediateState(IntermediateState&& other)
      : prior(std::move(other.prior)), next(std::move(other.next)) {}
  IntermediateState(const IntermediateState& other)
      : prior(other.prior), next(other.next) {}
  ~IntermediateState() = delete;
  using F = typename PriorState::Next::Promise;
  union {
    [[no_unique_address]] PriorState prior;
    [[no_unique_address]] F f;
  };
  using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
  using Next = adaptor_detail::Factory<FResult, FNext>;
  [[no_unique_address]] Next next;
};

template <typename State, typename NextF>
bool StepSeq(State* state, NextF* next_f) {
  auto r = state->f();
  auto* p = r.get_ready();
  if (p == nullptr) return true;
  Destruct(&state->f);
  auto n = state->next.Once(std::move(*p));
  Destruct(&state->next);
  Construct(next_f, std::move(n));
  return false;
}

template <typename... Fs>
class Seq;

#include "seq_switch.h"

}  // namespace seq_detail

// Sequencing combinator.
// Run the first promise.
// Pass it's result to the second, and run the returned promise.
// Pass it's result to the third, and run the returned promise.
// etc
// Return the final value.
template <typename... Functors>
seq_detail::Seq<Functors...> Seq(Functors... functors) {
  return seq_detail::Seq<Functors...>(std::move(functors)...);
}

template <typename F>
F Seq(F functor) {
  return functor;
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_SEQ_H
