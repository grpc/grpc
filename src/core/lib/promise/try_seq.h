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
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/switch.h"

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

template <typename Result, typename PriorState, typename NextStateF,
          typename RunNextPriorState>
Poll<Result> RunPriorState(PriorState* prior, NextStateF* next_f,
                      RunNextPriorState run_next_prior_state) {
  {
    auto r = prior->f();
    auto* p = r.get_ready();
    if (p == nullptr) return kPending;
    if (!p->ok()) return ready(Result(IntoStatus(p)));
    Destruct(&prior->f);
    auto n = CallNext(&prior->next, p);
    Destruct(&prior->next);
    Construct(next_f, std::move(n));
  }
  return run_next_prior_state();
}

template <char I, typename... Fs>
struct State {
  using PriorState = State<I - 1, Fs...>;
  using FNext = typename std::tuple_element<I + 1, std::tuple<Fs...>>::type;
  State(std::tuple<Fs*...> fs) : next(std::move(*std::get<I + 1>(fs))) {
    new (&prior) PriorState(fs);
  }
  State(State&& other)
      : prior(std::move(other.prior)), next(std::move(other.next)) {}
  State(const State& other) : prior(other.prior), next(other.next) {}
  ~State() = delete;
  using F = typename PriorState::Next::Promise;
  union {
    [[no_unique_address]] PriorState prior;
    [[no_unique_address]] F f;
  };
  using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
  using Next = adaptor_detail::Factory<typename NextArg<FResult>::Type, FNext>;
  [[no_unique_address]] Next next;

  template <int J>
  struct Get {
    static State<J, Fs...>* f(State* p) { return PriorState::template Get<J>::f(&p->prior); }
  };
  template <>
  struct Get<I> {
    static State* f(State* p) { return p; }
  };
};

template <typename... Fs>
struct State<0, Fs...> {
  State(std::tuple<Fs*...> args)
      : f(std::move(*std::get<0>(args))), next(std::move(*std::get<1>(args))) {}
  State(State&& other) : f(std::move(other.f)), next(std::move(other.next)) {}
  State(const State& other) : f(other.f), next(other.next) {}
  ~State() = delete;
  using F = typename std::tuple_element<0, std::tuple<Fs...>>::type;
  using FNext = typename std::tuple_element<1, std::tuple<Fs...>>::type;
  [[no_unique_address]] F f;
  using FResult = absl::remove_reference_t<decltype(*f().get_ready())>;
  using Next = adaptor_detail::Factory<typename NextArg<FResult>::Type, FNext>;
  [[no_unique_address]] Next next;

  template <int I>
  struct Get;
  template <>
  struct Get<0> {
    static State* f(State* p) { return p; }
  };
};

template <typename... Fs>
class TrySeq {
 private:
  static constexpr char N = sizeof...(Fs);

  char state_ = 0;
  using PenultimateState = State<N - 2, Fs...>;
  using FLast = typename PenultimateState::Next::Promise;
  union {
    [[no_unique_address]] PenultimateState p_;
    [[no_unique_address]] FLast f_;
  };
  using Result = typename decltype(f_())::Type;

  template <char I>
  struct RunState {
    TrySeq* s;
    Poll<Result> operator()() {
      auto* state = PenultimateState::template Get<I + 1>::f(&s->p_);
      return RunPriorState<Result>(&state->prior, &state->f, [this]() {
        s->state_ = I + 1;
        return RunState<I + 1>{s}();
      });
    }
  };

  template <>
  struct RunState<N - 2> {
    TrySeq* s;
    Poll<Result> operator()() {
      return RunPriorState<Result>(&s->p_, &s->f_, [this]() {
        s->state_ = N - 1;
        return RunState<N - 1>{s}();
      });
    }
  };

  template <>
  struct RunState<N - 1> {
    TrySeq* s;
    Poll<Result> operator()() { return s->f_(); }
  };

  template <char I>
  struct DestructF {
    TrySeq* s;
    void operator()() {
      Destruct(&PenultimateState::template Get<I>::f(&s->p_)->f);
      DestructNext<I>::Run(s);
    }
  };

  template <>
  struct DestructF<N - 1> {
    TrySeq* s;
    void operator()() { Destruct(&s->f_); }
  };

  template <char I>
  struct DestructNext {
    static void Run(TrySeq* s) {
      Destruct(&PenultimateState::template Get<I>::f(&s->p_)->next);
      DestructNext<I + 1>::Run(s);
    }
  };

  template <>
  struct DestructNext<N - 1> {
    static void Run(TrySeq* s) {}
  };

  template <char... I>
  Poll<Result> Run(absl::integer_sequence<char, I...>) {
    return Switch<Poll<Result>>(state_, RunState<I>{this}...);
  }

  template <char... I>
  void RunDestruct(absl::integer_sequence<char, I...>) {
    Switch<void>(state_, DestructF<I>{this}...);
  }

 public:
  TrySeq(Fs... fs) : p_(std::make_tuple(&fs...)) {}
  TrySeq& operator=(const TrySeq&) = delete;
  TrySeq(const TrySeq& other) {
    assert(other.state_ == 0);
    new (&p_) PenultimateState(other.p_);
  }
  TrySeq(TrySeq&& other) {
    assert(other.state_ == 0);
    new (&p_) PenultimateState(std::move(other.p_));
  }
  ~TrySeq() { RunDestruct(absl::make_integer_sequence<char, N>()); }

  Poll<Result> operator()() {
    return Run(absl::make_integer_sequence<char, N>());
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
