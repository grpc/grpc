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
  explicit State(F f) : f_(std::move(f)) {}
  using Result = typename decltype(std::declval<F>()())::Type;

  using FinalState = State<F>;
  using StatesList = List<FinalState>;

  Poll<Result> operator()() { return f_(); }

 private:
  F f_;
};

template <typename Next, typename T>
struct NextResultTraits;

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

template <typename Result>
Result StatusFrom(absl::Status* status) {
  return Result(std::move(*status));
}

template <typename Result, typename T>
Result StatusFrom(absl::StatusOr<T>* status) {
  return Result(std::move(status->status()));
}

template <typename F, typename Next, typename... Nexts>
class State<F, Next, Nexts...> {
 public:
  State(F f, Next next, Nexts... nexts)
      : f_(std::move(f)), next_(std::move(next)), nexts_(std::move(nexts)...) {}

  using FResult = typename decltype(std::declval<F>()())::Type;
  using NextFactory =
      adaptor_detail::Factory<typename NextArg<FResult>::Type, Next>;
  using NextResult = decltype(CallNext(static_cast<NextFactory*>(nullptr),
                                       static_cast<FResult*>(nullptr)));
  using NextState = State<NextResult, Nexts...>;
  using FinalState = typename NextState::FinalState;
  using StatesList =
      typename NextState::StatesList::template Append<State<F, Next, Nexts...>>;
  using PollResult = absl::StatusOr<NextState>;

  Poll<PollResult> operator()() {
    auto r = f_();
    if (auto result = r.get_ready()) {
      if (result->ok()) {
        auto next = &next_;
        return ready(PollResult(absl::apply(
            [result, next](Nexts&... nexts) {
              return NextState(CallNext(next, result), std::move(nexts)...);
            },
            nexts_)));
      }
      return ready(StatusFrom<PollResult>(result));
    }
    return PENDING;
  }

 private:
  F f_;
  NextFactory next_;
  std::tuple<Nexts...> nexts_;
};

template <typename... Functors>
class TrySeq {
 private:
  using InitialState = State<Functors...>;
  using FinalState = typename InitialState::FinalState;
  using StatesVariant = typename InitialState::StatesList::Variant;
  using Result = typename FinalState::Result;
  StatesVariant state_;

  template <bool kSetState>
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
      auto r = state();
      if (auto* p = r.get_ready()) {
        if (p->ok()) {
          return CallPoll<true>{seq}(**p);
        } else {
          return ready(Result(std::move(p->status())));
        }
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
  explicit TrySeq(Functors... functors)
      : state_(InitialState(std::move(functors)...)) {}

  Poll<Result> operator()() {
    return absl::visit(CallPoll<false>{this}, state_);
  }
};

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
