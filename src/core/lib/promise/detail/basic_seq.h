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

#ifndef GRPC_CORE_LIB_PROMISE_DETAIL_BASIC_SEQ_H
#define GRPC_CORE_LIB_PROMISE_DETAIL_BASIC_SEQ_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <array>
#include <cassert>
#include <new>
#include <tuple>
#include <utility>

#include "absl/meta/type_traits.h"
#include "absl/types/variant.h"
#include "absl/utility/utility.h"

#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {
namespace promise_detail {

// Given f0, ..., fn, call function idx and return the result.
template <typename R, typename A, R (*... f)(A* arg)>
class JumpTable {
 public:
  JumpTable() = delete;
  JumpTable(const JumpTable&) = delete;

  static R Run(size_t idx, A* arg) { return fs_[idx](arg); }

 private:
  using Fn = R (*)(A* arg);
  static const Fn fs_[sizeof...(f)];
};

template <typename R, typename A, R (*... f)(A* arg)>
const typename JumpTable<R, A, f...>::Fn
    JumpTable<R, A, f...>::fs_[sizeof...(f)] = {f...};

// Helper for SeqState to evaluate some common types to all partial
// specializations.
template <template <typename> class Traits, typename FPromise, typename FNext>
struct SeqStateTypes {
  // Our current promise.
  using Promise = FPromise;
  // The result of our current promise.
  using PromiseResult = typename Promise::Result;
  // Traits around the result of our promise.
  using PromiseResultTraits = Traits<PromiseResult>;
  // Wrap the factory callable in our factory wrapper to deal with common edge
  // cases. We use the 'unwrapped type' from the traits, so for instance, TrySeq
  // can pass back a T from a StatusOr<T>.
  using Next = promise_detail::PromiseFactory<
      typename PromiseResultTraits::UnwrappedType, FNext>;
};

// One state in a sequence.
// A state contains the current promise, and the promise factory to turn the
// result of the current promise into the next state's promise. We play a shell
// game such that the prior state and our current promise are kept in a union,
// and the next promise factory is kept alongside in the state struct.
// Recursively this guarantees that the next functions get initialized once, and
// destroyed once, and don't need to be moved around in between, which avoids a
// potential O(n**2) loop of next factory moves had we used a variant of states
// here. The very first state does not have a prior state, and so that state has
// a partial specialization below. The final state does not have a next state;
// that state is inlined in BasicSeq since that was simpler to type.
template <template <typename> class Traits, char I, typename... Fs>
struct SeqState {
  // The state evaluated before this state.
  using PriorState = SeqState<Traits, I - 1, Fs...>;
  // Initialization from callables.
  explicit SeqState(std::tuple<Fs*...> fs)
      : next_factory(std::move(*std::get<I + 1>(fs))) {
    new (&prior) PriorState(fs);
  }
  // Move constructor - assumes we're in the initial state (move prior) as it's
  // illegal to move a promise after polling it.
  SeqState(SeqState&& other) noexcept
      : next_factory(std::move(other.next_factory)) {
    new (&prior) PriorState(std::move(other.prior));
  }
  // Copy constructor - assumes we're in the initial state (move prior) as it's
  // illegal to move a promise after polling it.
  SeqState(const SeqState& other) : next_factory(other.next_factory) {
    new (&prior) PriorState(std::move(other.prior));
  }
  // Empty destructor - we instead destruct the innards in BasicSeq manually
  // depending on state.
  ~SeqState() {}
  // Evaluate the current promise, next promise factory types for this state.
  // The current promise is the next promise from the prior state.
  // The next factory callable is from the callables passed in:
  // Fs[0] is the initial promise, Fs[1] is the state 0 next factory, Fs[2] is
  // the state 1 next factory, etc...
  using Types = SeqStateTypes<
      Traits, typename PriorState::Types::Next::Promise,
      typename std::tuple_element<I + 1, std::tuple<Fs...>>::type>;
  // Storage for either the current promise or the prior state.
  union {
    // If we're in the prior state.
    GPR_NO_UNIQUE_ADDRESS PriorState prior;
    // The callables representing our promise.
    GPR_NO_UNIQUE_ADDRESS typename Types::Promise current_promise;
  };
  // Storage for the next promise factory.
  GPR_NO_UNIQUE_ADDRESS typename Types::Next next_factory;
};

// Partial specialization of SeqState above for the first state - it has no
// prior state, so we take the first callable from the template arg list and use
// it as a promise.
template <template <typename> class Traits, typename... Fs>
struct SeqState<Traits, 0, Fs...> {
  // Initialization from callables.
  explicit SeqState(std::tuple<Fs*...> args)
      : current_promise(std::move(*std::get<0>(args))),
        next_factory(std::move(*std::get<1>(args))) {}
  // Move constructor - it's assumed we're in this state (see above).
  SeqState(SeqState&& other) noexcept
      : current_promise(std::move(other.current_promise)),
        next_factory(std::move(other.next_factory)) {}
  // Copy constructor - it's assumed we're in this state (see above).
  SeqState(const SeqState& other)
      : current_promise(other.current_promise),
        next_factory(other.next_factory) {}
  // Empty destructor - we instead destruct the innards in BasicSeq manually
  // depending on state.
  ~SeqState() {}
  // Evaluate the current promise, next promise factory types for this state.
  // Our callable is the first element of Fs, wrapped in PromiseLike to handle
  // some common edge cases. The next factory is the second element.
  using Types = SeqStateTypes<
      Traits,
      PromiseLike<typename std::tuple_element<0, std::tuple<Fs...>>::type>,
      typename std::tuple_element<1, std::tuple<Fs...>>::type>;
  GPR_NO_UNIQUE_ADDRESS typename Types::Promise current_promise;
  GPR_NO_UNIQUE_ADDRESS typename Types::Next next_factory;
};

// Helper to get a specific state index.
// Calls the prior state, unless it's this state, in which case it returns
// that.
template <char I, template <typename> class Traits, char J, typename... Fs>
struct GetSeqStateInner {
  static SeqState<Traits, I, Fs...>* f(SeqState<Traits, J, Fs...>* p) {
    return GetSeqStateInner<I, Traits, J - 1, Fs...>::f(&p->prior);
  }
};

template <char I, template <typename> class Traits, typename... Fs>
struct GetSeqStateInner<I, Traits, I, Fs...> {
  static SeqState<Traits, I, Fs...>* f(SeqState<Traits, I, Fs...>* p) {
    return p;
  }
};

template <char I, template <typename> class Traits, char J, typename... Fs>
absl::enable_if_t<I <= J, SeqState<Traits, I, Fs...>*> GetSeqState(
    SeqState<Traits, J, Fs...>* p) {
  return GetSeqStateInner<I, Traits, J, Fs...>::f(p);
}

template <template <typename> class Traits, char I, typename... Fs, typename T>
auto CallNext(SeqState<Traits, I, Fs...>* state, T&& arg)
    -> decltype(SeqState<Traits, I, Fs...>::Types::PromiseResultTraits::
                    CallFactory(&state->next_factory, std::forward<T>(arg))) {
  return SeqState<Traits, I, Fs...>::Types::PromiseResultTraits::CallFactory(
      &state->next_factory, std::forward<T>(arg));
}

// A sequence under stome traits for some set of callables Fs.
// Fs[0] should be a promise-like object that yields a value.
// Fs[1..] should be promise-factory-like objects that take the value from the
// previous step and yield a promise. Note that most of the machinery in
// PromiseFactory exists to make it possible for those promise-factory-like
// objects to be anything that's convenient. Traits defines how we move from one
// step to the next. Traits sets up the wrapping and escape handling for the
// sequence. Promises return wrapped values that the trait can inspect and
// unwrap before passing them to the next element of the sequence. The trait can
// also interpret a wrapped value as an escape value, which terminates
// evaluation of the sequence immediately yielding a result. Traits for type T
// have the members:
//  * type UnwrappedType - the type after removing wrapping from T (i.e. for
//    TrySeq, T=StatusOr<U> yields UnwrappedType=U).
//  * type WrappedType - the type after adding wrapping if it doesn't already
//    exist (i.e. for TrySeq if T is not Status/StatusOr/void, then
//    WrappedType=StatusOr<T>; if T is Status then WrappedType=Status (it's
//    already wrapped!))
//  * template <typename Next> void CallFactory(Next* next_factory, T&& value) -
//    call promise factory next_factory with the result of unwrapping value, and
//    return the resulting promise.
//  * template <typename Result, typename RunNext> Poll<Result>
//    CheckResultAndRunNext(T prior, RunNext run_next) - examine the value of
//    prior, and decide to escape or continue. If escaping, return the final
//    sequence value of type Poll<Result>. If continuing, return the value of
//    run_next(std::move(prior)).
template <template <typename> class Traits, typename... Fs>
class BasicSeq {
 private:
  // Number of states in the sequence - we'll refer to this some!
  static constexpr char N = sizeof...(Fs);

  // Current state.
  static_assert(N < 128, "Long sequence... please revisit BasicSeq");
  char state_ = 0;
  // The penultimate state contains all the preceding states too.
  using PenultimateState = SeqState<Traits, N - 2, Fs...>;
  // The final state is simply the final promise, which is the next promise from
  // the penultimate state.
  using FinalPromise = typename PenultimateState::Types::Next::Promise;
  union {
    GPR_NO_UNIQUE_ADDRESS PenultimateState penultimate_state_;
    GPR_NO_UNIQUE_ADDRESS FinalPromise final_promise_;
  };
  using FinalPromiseResult = typename FinalPromise::Result;
  using Result = typename Traits<FinalPromiseResult>::WrappedType;

  // Get a state by index.
  template <char I>
      absl::enable_if_t < I<N - 2, SeqState<Traits, I, Fs...>*> state() {
    return GetSeqState<I>(&penultimate_state_);
  }

  template <char I>
  absl::enable_if_t<I == N - 2, PenultimateState*> state() {
    return &penultimate_state_;
  }

  // Get the next state's promise.
  template <char I>
  auto next_promise() -> absl::enable_if_t<
      I != N - 2,
      decltype(&GetSeqState<I + 1>(static_cast<PenultimateState*>(nullptr))
                    ->current_promise)> {
    return &GetSeqState<I + 1>(&penultimate_state_)->current_promise;
  }

  template <char I>
  absl::enable_if_t<I == N - 2, FinalPromise*> next_promise() {
    return &final_promise_;
  }

  // Callable to advance the state to the next one after I given the result from
  // state I.
  template <char I>
  struct RunNext {
    BasicSeq* s;
    template <typename T>
    Poll<Result> operator()(T&& value) {
      auto* prior = s->state<I>();
      using StateType = absl::remove_reference_t<decltype(*prior)>;
      // Destroy the promise that just completed.
      Destruct(&prior->current_promise);
      // Construct the next promise by calling the next promise factory.
      // We need to ask the traits to do this to deal with value
      // wrapping/unwrapping.
      auto n = StateType::Types::PromiseResultTraits::CallFactory(
          &prior->next_factory, std::forward<T>(value));
      // Now we also no longer need the factory, so we can destroy that.
      Destruct(&prior->next_factory);
      // Constructing the promise for the new state will use the memory
      // previously occupied by the promise & next factory of the old state.
      Construct(s->next_promise<I>(), std::move(n));
      // Store the state counter.
      s->state_ = I + 1;
      // Recursively poll the new current state.
      return s->RunState<I + 1>();
    }
  };

  // Poll the current state, advance it if necessary.
  template <char I>
  absl::enable_if_t<I != N - 1, Poll<Result>> RunState() {
    // Get a pointer to the state object.
    auto* s = state<I>();
    // Poll the current promise in this state.
    auto r = s->current_promise();
    // If we are still pending, say so by returning.
    if (absl::holds_alternative<Pending>(r)) {
      return Pending();
    }
    // Current promise is ready, as the traits to do the next thing.
    // That may be returning - eg if TrySeq sees an error.
    // Or it may be by calling the callable we hand down - RunNext - which
    // will advance the state and call the next promise.
    return Traits<
        typename absl::remove_reference_t<decltype(*s)>::Types::PromiseResult>::
        template CheckResultAndRunNext<Result>(
            std::move(absl::get<kPollReadyIdx>(std::move(r))),
            RunNext<I>{this});
  }

  // Specialization of RunState to run the final state.
  template <char I>
  absl::enable_if_t<I == N - 1, Poll<Result>> RunState() {
    // Poll the final promise.
    auto r = final_promise_();
    // If we are still pending, say so by returning.
    if (absl::holds_alternative<Pending>(r)) {
      return Pending();
    }
    // We are complete, return the (wrapped) result.
    return Result(std::move(absl::get<kPollReadyIdx>(std::move(r))));
  }

  // For state numbered I, destruct the current promise and the next promise
  // factory, and recursively destruct the next promise factories for future
  // states (since they also still exist).
  template <char I>
  absl::enable_if_t<I != N - 1, void>
  DestructCurrentPromiseAndSubsequentFactories() {
    Destruct(&GetSeqState<I>(&penultimate_state_)->current_promise);
    DestructSubsequentFactories<I>();
  }

  template <char I>
  absl::enable_if_t<I == N - 1, void>
  DestructCurrentPromiseAndSubsequentFactories() {
    Destruct(&final_promise_);
  }

  // For state I, destruct the next promise factory, and recursively the next
  // promise factories after.
  template <char I>
  absl::enable_if_t<I != N - 1, void> DestructSubsequentFactories() {
    Destruct(&GetSeqState<I>(&penultimate_state_)->next_factory);
    DestructSubsequentFactories<I + 1>();
  }

  template <char I>
  absl::enable_if_t<I == N - 1, void> DestructSubsequentFactories() {}

  // Placate older compilers by wrapping RunState in a struct so that their
  // parameter unpacking can work.
  template <char I>
  struct RunStateStruct {
    static Poll<Result> Run(BasicSeq* s) { return s->RunState<I>(); }
  };

  // Similarly placate those compilers for
  // DestructCurrentPromiseAndSubsequentFactories
  template <char I>
  struct DestructCurrentPromiseAndSubsequentFactoriesStruct {
    static void Run(BasicSeq* s) {
      return s->DestructCurrentPromiseAndSubsequentFactories<I>();
    }
  };

  // Run the current state (and possibly later states if that one finishes).
  // Single argument is a type that encodes the integer sequence 0, 1, 2, ...,
  // N-1 as a type, but which uses no memory. This is used to expand out
  // RunState instances using a template unpack to pass to Switch, which encodes
  // a switch statement over the various cases. This ultimately gives us a
  // Duff's device like mechanic for evaluating sequences.
  template <char... I>
  Poll<Result> Run(absl::integer_sequence<char, I...>) {
    return JumpTable<Poll<Result>, BasicSeq, RunStateStruct<I>::Run...>::Run(
        state_, this);
  }

  // Run the appropriate destructors for a given state.
  // Single argument is a type that encodes the integer sequence 0, 1, 2, ...,
  // N-1 as a type, but which uses no memory. This is used to expand out
  // DestructCurrentPromiseAndSubsequentFactories instances to pass to Switch,
  // which can choose the correct instance at runtime to destroy everything.
  template <char... I>
  void RunDestruct(absl::integer_sequence<char, I...>) {
    JumpTable<void, BasicSeq,
              DestructCurrentPromiseAndSubsequentFactoriesStruct<I>::Run...>::
        Run(state_, this);
  }

 public:
  // Construct a sequence given the callables that will control it.
  explicit BasicSeq(Fs... fs) : penultimate_state_(std::make_tuple(&fs...)) {}
  // No assignment... we don't need it (but if we ever find a good case, then
  // it's ok to implement).
  BasicSeq& operator=(const BasicSeq&) = delete;
  // Copy construction - only for state 0.
  // It's illegal to copy a Promise after polling it - if we are in state>0 we
  // *must* have been polled.
  BasicSeq(const BasicSeq& other) {
    assert(other.state_ == 0);
    new (&penultimate_state_) PenultimateState(other.penultimate_state_);
  }
  // Move construction - only for state 0.
  // It's illegal to copy a Promise after polling it - if we are in state>0 we
  // *must* have been polled.
  BasicSeq(BasicSeq&& other) noexcept {
    assert(other.state_ == 0);
    new (&penultimate_state_)
        PenultimateState(std::move(other.penultimate_state_));
  }
  // Destruct based on current state.
  ~BasicSeq() { RunDestruct(absl::make_integer_sequence<char, N>()); }

  // Poll the sequence once.
  Poll<Result> operator()() {
    return Run(absl::make_integer_sequence<char, N>());
  }
};

// As above, but models a sequence of unknown size
// At each element, the accumulator A and the current value V is passed to some
// function of type IterTraits::Factory as f(V, IterTraits::Argument); f is
// expected to return a promise that resolves to IterTraits::Wrapped.
template <class IterTraits>
class BasicSeqIter {
 private:
  using Traits = typename IterTraits::Traits;
  using Iter = typename IterTraits::Iter;
  using Factory = typename IterTraits::Factory;
  using Argument = typename IterTraits::Argument;
  using IterValue = typename IterTraits::IterValue;
  using StateCreated = typename IterTraits::StateCreated;
  using State = typename IterTraits::State;
  using Wrapped = typename IterTraits::Wrapped;

 public:
  BasicSeqIter(Iter begin, Iter end, Factory f, Argument arg)
      : cur_(begin), end_(end), f_(std::move(f)) {
    if (cur_ == end_) {
      Construct(&result_, std::move(arg));
    } else {
      Construct(&state_, f_(*cur_, std::move(arg)));
    }
  }

  ~BasicSeqIter() {
    if (cur_ == end_) {
      Destruct(&result_);
    } else {
      Destruct(&state_);
    }
  }

  BasicSeqIter(const BasicSeqIter& other) = delete;
  BasicSeqIter& operator=(const BasicSeqIter&) = delete;

  BasicSeqIter(BasicSeqIter&& other) noexcept
      : cur_(other.cur_), end_(other.end_), f_(std::move(other.f_)) {
    if (cur_ == end_) {
      Construct(&result_, std::move(other.result_));
    } else {
      Construct(&state_, std::move(other.state_));
    }
  }
  BasicSeqIter& operator=(BasicSeqIter&& other) noexcept {
    cur_ = other.cur_;
    end_ = other.end_;
    if (cur_ == end_) {
      Construct(&result_, std::move(other.result_));
    } else {
      Construct(&state_, std::move(other.state_));
    }
    return *this;
  }

  Poll<Wrapped> operator()() {
    if (cur_ == end_) {
      return std::move(result_);
    }
    return PollNonEmpty();
  }

 private:
  Poll<Wrapped> PollNonEmpty() {
    Poll<Wrapped> r = state_();
    if (absl::holds_alternative<Pending>(r)) return r;
    return Traits::template CheckResultAndRunNext<Wrapped>(
        std::move(absl::get<Wrapped>(r)), [this](Wrapped arg) -> Poll<Wrapped> {
          auto next = cur_;
          ++next;
          if (next == end_) {
            return std::move(arg);
          }
          cur_ = next;
          state_.~State();
          Construct(&state_,
                    Traits::template CallSeqFactory(f_, *cur_, std::move(arg)));
          return PollNonEmpty();
        });
  }

  Iter cur_;
  const Iter end_;
  GPR_NO_UNIQUE_ADDRESS Factory f_;
  union {
    GPR_NO_UNIQUE_ADDRESS State state_;
    GPR_NO_UNIQUE_ADDRESS Argument result_;
  };
};

}  // namespace promise_detail
}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_DETAIL_BASIC_SEQ_H
