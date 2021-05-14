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
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace seq_detail {

template <typename... Ts>
struct List {
  template <typename T>
  using Append = List<Ts..., T>;
  using Variant = absl::variant<Ts...>;
};

template <typename F, typename... Next>
class State;

template <typename F>
class State<F> {
 public:
  State(F f) : f_(std::move(f)) {}
  using Result = typename decltype(std::declval<F>()())::Type;

  using FinalState = State<F>;
  using StatesList = List<FinalState>;

  Poll<Result> operator()() { return f_(); }

 private:
  F f_;
};

template <typename F, typename Next, typename... Nexts>
class State<F, Next, Nexts...> {
 public:
  State(F f, Next next, Nexts... nexts)
      : f_(std::move(f)), next_(std::move(next)), nexts_(std::move(nexts)...) {}

  using FResult = typename decltype(std::declval<F>()())::Type;
  using NextResult = decltype(std::declval<Next>()(std::declval<FResult>()));
  using NextState = State<NextResult, Nexts...>;
  using FinalState = typename NextState::FinalState;
  using StatesList =
      typename NextState::StatesList::template Append<State<F, Next, Nexts...>>;

  Poll<NextState> operator()() {
    return f_().Map([this](FResult& r) {
      auto next_f = next_(std::move(r));
      return absl::apply(
          [&next_f](Nexts&... nexts) {
            return NextState(std::move(next_f), std::move(nexts)...);
          },
          nexts_);
    });
  }

 private:
  F f_;
  Next next_;
  std::tuple<Nexts...> nexts_;
};

template <typename... Functors>
class Seq {
 private:
  using InitialState = State<Functors...>;
  using FinalState = typename InitialState::FinalState;
  using StatesVariant = typename InitialState::StatesList::Variant;
  using Result = typename FinalState::Result;
  StatesVariant state_;

  template <bool kSetState>
  struct CallPoll {
    Seq* seq;

    // CallPoll on a non-final state moves to the next state if it completes.
    // If so, it immediately polls the next state also (recursively!) and when
    // polling completes stores the current state back into state_.
    // kSetState keeps track of the need to do this (or not) - when we first
    // visit state_ kSetState is false indicating that we're already mutating
    // it. If we gain a new state on the stack, we don't put that into state_
    // immediately to avoid unnecessary potential copies.
    template <typename State>
    Poll<Result> operator()(State& state) {
      auto r = state();
      if (auto* p = r.get_ready()) {
        return CallPoll<true>{seq}(*p);
      } else {
        if (kSetState) {
          seq->state_.template emplace<State>(std::move(state));
        }
        return PENDING;
      }
    }

    // CallPoll on the final state produces the result if it completes.
    Poll<Result> operator()(FinalState& final_state) {
      auto r = final_state();
      if (r.ready()) {
        return ready(r.take());
      } else {
        if (kSetState) {
          seq->state_.template emplace<FinalState>(std::move(final_state));
        }
        return PENDING;
      }
    }
  };

 public:
  Seq(Functors... functors) : state_(InitialState(std::move(functors)...)) {}

  Poll<Result> operator()() {
    return absl::visit(CallPoll<false>{this}, state_);
  }
};

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

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_SEQ_H
