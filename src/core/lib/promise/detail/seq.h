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

#ifndef GRPC_CORE_LIB_PROMISE_DETAIL_SEQ_H
#define GRPC_CORE_LIB_PROMISE_DETAIL_SEQ_H

#include "absl/types/variant.h"
#include "src/core/lib/promise/adaptor.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/detail/switch.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {
namespace promise_detail {

template <template <typename> class Traits, char I, typename... Fs>
struct SeqState {
  using PriorState = SeqState<Traits, I - 1, Fs...>;
  explicit SeqState(std::tuple<Fs*...> fs)
      : next(std::move(*std::get<I + 1>(fs))) {
    new (&prior) PriorState(fs);
  }
  SeqState(SeqState&& other) noexcept
      : prior(std::move(other.prior)), next(std::move(other.next)) {}
  SeqState(const SeqState& other) : prior(other.prior), next(other.next) {}
  ~SeqState() = delete;
  using F = typename PriorState::Next::Promise;
  union {
    [[no_unique_address]] PriorState prior;
    [[no_unique_address]] F f;
  };
  using FResult =
      absl::remove_reference_t<typename PollTraits<decltype(f())>::Type>;
  using FResultTraits = Traits<FResult>;
  using FNext = typename std::tuple_element<I + 1, std::tuple<Fs...>>::type;
  using Next =
      promise_detail::PromiseFactory<typename FResultTraits::UnwrappedType,
                                     FNext>;
  [[no_unique_address]] Next next;

  template <int J>
  struct Get {
    static SeqState<Traits, J, Fs...>* f(SeqState* p) {
      return PriorState::template Get<J>::f(&p->prior);
    }
  };
  template <>
  struct Get<I> {
    static SeqState* f(SeqState* p) { return p; }
  };
};

template <template <typename> class Traits, typename... Fs>
struct SeqState<Traits, 0, Fs...> {
  explicit SeqState(std::tuple<Fs*...> args)
      : f(std::move(*std::get<0>(args))), next(std::move(*std::get<1>(args))) {}
  SeqState(SeqState&& other) noexcept
      : f(std::move(other.f)), next(std::move(other.next)) {}
  SeqState(const SeqState& other) : f(other.f), next(other.next) {}
  ~SeqState() = delete;
  using F =
      PromiseLike<typename std::tuple_element<0, std::tuple<Fs...>>::type>;
  using FNext = typename std::tuple_element<1, std::tuple<Fs...>>::type;
  [[no_unique_address]] F f;
  using FResult = typename F::Result;
  using FResultTraits = Traits<FResult>;
  using Next =
      promise_detail::PromiseFactory<typename FResultTraits::UnwrappedType,
                                     FNext>;
  [[no_unique_address]] Next next;

  template <int I>
  struct Get;
  template <>
  struct Get<0> {
    static SeqState* f(SeqState* p) { return p; }
  };
};

template <template <typename> class Traits, char I, typename... Fs, typename T>
auto CallNext(SeqState<Traits, I, Fs...>* state, T&& arg)
    -> decltype(SeqState<Traits, I, Fs...>::FResultTraits::CallFactory(
        &state->next, std::forward<T>(arg))) {
  return SeqState<Traits, I, Fs...>::FResultTraits::CallFactory(
      &state->next, std::forward<T>(arg));
}

template <template <typename> class Traits, typename... Fs>
class Seq {
 private:
  static constexpr char N = sizeof...(Fs);

  char state_ = 0;
  using PenultimateState = SeqState<Traits, N - 2, Fs...>;
  using FLast = typename PenultimateState::Next::Promise;
  union {
    [[no_unique_address]] PenultimateState p_;
    [[no_unique_address]] FLast f_;
  };
  using FLastResult = typename PollTraits<decltype(f_())>::Type;
  using Result = typename Traits<FLastResult>::WrappedType;

  template <char I>
  struct Get {
    static SeqState<Traits, I, Fs...>* state(Seq* s) {
      return &PenultimateState::template Get<I + 1>::f(&s->p_)->prior;
    }

    static typename SeqState<Traits, I + 1, Fs...>::F* next_f(Seq* s) {
      return &PenultimateState::template Get<I + 1>::f(&s->p_)->f;
    }
  };

  template <>
  struct Get<N - 2> {
    static PenultimateState* state(Seq* s) { return &s->p_; }
    static FLast* next_f(Seq* s) { return &s->f_; }
  };

  template <char I>
  struct RunNext {
    Seq* s;
    template <typename T>
    Poll<Result> operator()(T&& value) {
      auto* prior = Get<I>::state(s);
      Destruct(&prior->f);
      using N = absl::remove_reference_t<decltype(*Get<I>::next_f(s))>;
      N n = CallNext(prior, std::forward<T>(value));
      Destruct(&prior->next);
      Construct(Get<I>::next_f(s), std::move(n));
      s->state_ = I + 1;
      return RunState<I + 1>{s}();
    }
  };

  template <char I>
  struct RunState {
    Seq* s;
    Poll<Result> operator()() {
      auto* state = Get<I>::state(s);
      auto r = state->f();
      auto* p = absl::get_if<kPollReadyIdx>(&r);
      if (p == nullptr) {
        return Pending();
      }
      return Traits<
          typename absl::remove_reference_t<decltype(*state)>::FResult>::
          template CheckResultAndRunNext<Result>(std::move(*p), RunNext<I>{s});
    }
  };

  template <>
  struct RunState<N - 1> {
    Seq* s;
    Poll<Result> operator()() {
      auto r = s->f_();
      if (auto* p = absl::get_if<kPollReadyIdx>(&r)) {
        return Result(std::move(*p));
      } else {
        return Pending();
      }
    }
  };

  template <char I>
  struct DestructF {
    Seq* s;
    void operator()() {
      Destruct(&PenultimateState::template Get<I>::f(&s->p_)->f);
      DestructNext<I>::Run(s);
    }
  };

  template <>
  struct DestructF<N - 1> {
    Seq* s;
    void operator()() { Destruct(&s->f_); }
  };

  template <char I>
  struct DestructNext {
    static void Run(Seq* s) {
      Destruct(&PenultimateState::template Get<I>::f(&s->p_)->next);
      DestructNext<I + 1>::Run(s);
    }
  };

  template <>
  struct DestructNext<N - 1> {
    static void Run(Seq* s) {}
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
  explicit Seq(Fs... fs) : p_(std::make_tuple(&fs...)) {}
  Seq& operator=(const Seq&) = delete;
  Seq(const Seq& other) {
    assert(other.state_ == 0);
    new (&p_) PenultimateState(other.p_);
  }
  Seq(Seq&& other) noexcept {
    assert(other.state_ == 0);
    new (&p_) PenultimateState(std::move(other.p_));
  }
  ~Seq() { RunDestruct(absl::make_integer_sequence<char, N>()); }

  Poll<Result> operator()() {
    return Run(absl::make_integer_sequence<char, N>());
  }
};

}  // namespace promise_detail
}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_SEQ_H
