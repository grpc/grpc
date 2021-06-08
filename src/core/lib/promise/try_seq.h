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

#include "absl/status/statusor.h"
#include "absl/types/variant.h"
#include "src/core/lib/promise/adaptor.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace try_seq_detail {

template <typename T>
struct NextArg;
template <typename T>
struct NextArg<absl::StatusOr<T>> {
  using Type = T;
};
template <>
struct NextArg<absl::Status> {
  using Type = void;
};

template <typename Next, typename T>
auto CallNext(Next* next, absl::StatusOr<T>* status)
    -> decltype(next->Once(std::move(**status))) {
  return next->Once(std::move(**status));
}

template <typename Next>
auto CallNext(Next* next, absl::Status* status) -> decltype(next->Once()) {
  return next->Once();
}

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
  using Next = adaptor_detail::Factory<typename NextArg<FResult>::Type, F1>;
  [[no_unique_address]] Next next;
};

template <typename PriorState, typename FNext, typename... PriorStateFs>
struct IntermediateState {
  IntermediateState(PriorStateFs&&... prior_fs, FNext&& f)
      : next(std::forward<F>(f)) {
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
  using Next = adaptor_detail::Factory<typename NextArg<FResult>::Type, FNext>;
  [[no_unique_address]] Next next;
};

template <typename PriorState, typename NewStateF, typename Result>
void AdvanceState(PriorState* prior_state, NewStateF* new_state_f, Result* p) {
  Destruct(&prior_state->f);
  auto n = CallNext(&prior_state->next, p);
  Destruct(&prior_state->next);
  Construct(new_state_f, std::move(n));
}

template <typename... Functors>
class TrySeq;

#include "try_seq_switch.h"

/* {
 private:
  using InitialState = State<Functors...>;
  using FinalState = typename InitialState::FinalState;
  using StatesVariant = typename InitialState::StatesList::Variant;
  using Result = typename FinalState::Result;
  StatesVariant state_;

  struct CallPoll {
    TrySeq* seq;
    // CallPoll on a non-final state moves to the next state if it completes.
    // If so, it immediately polls the next state also (recursively!) and when
    // polling completes stores the current state back into state_.
    // kSetState keeps track of the need to do this (or not) - when we first
    // visit state_ kSetState is false indicating that we're already mutating
    // it. If we gain a new state on the stack, we don't put that into state_
    // immediately to avoid unnecessary potential copies.
    template <typename State>
    Poll<Result> operator()(State& state) {
      return state.Run(&seq->state_);
    }
  };

 public:
  explicit TrySeq(Functors... functors)
      : state_(InitialState(std::move(functors)...)) {}

  Poll<Result> operator()() { return absl::visit(CallPoll{this}, state_); }
};*/

}  // namespace try_seq_detail

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
try_seq_detail::TrySeq<Functors...> TrySeq(Functors... functors) {
  return try_seq_detail::TrySeq<Functors...>(std::move(functors)...);
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_TRY_SEQ_H
