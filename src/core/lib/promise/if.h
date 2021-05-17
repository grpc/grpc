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

#ifndef GRPC_CORE_LIB_PROMISE_IF_H
#define GRPC_CORE_LIB_PROMISE_IF_H

#include "absl/status/statusor.h"
#include "absl/types/variant.h"
#include "src/core/lib/promise/adaptor.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace if_detail {

template <typename CallPoll, typename T, typename F>
typename CallPoll::PollResult Choose(CallPoll call_poll, bool result,
                                     T* if_true, F* if_false) {
  if (result) {
    auto promise = if_true->Once();
    return call_poll(promise);
  } else {
    auto promise = if_false->Once();
    return call_poll(promise);
  }
}

template <typename CallPoll, typename T, typename F>
typename CallPoll::PollResult Choose(CallPoll call_poll,
                                     absl::StatusOr<bool> result, T* if_true,
                                     F* if_false) {
  if (!result.ok()) {
    return typename CallPoll::PollResult(result.status());
  } else if (*result) {
    auto promise = if_true->Once();
    return call_poll(promise);
  } else {
    auto promise = if_false->Once();
    return call_poll(promise);
  }
}

template <typename C, typename T, typename F>
class If {
 private:
  using ConditionFactory = adaptor_detail::Factory<void, C>;
  using TrueFactory = adaptor_detail::Factory<void, T>;
  using FalseFactory = adaptor_detail::Factory<void, F>;
  using ConditionPromise = typename ConditionFactory::Promise;
  using TruePromise = typename TrueFactory::Promise;
  using FalsePromise = typename FalseFactory::Promise;
  using Result = typename decltype(std::declval<TruePromise>()())::Type;

 public:
  If(C condition, T if_true, F if_false)
      : state_(Initial{ConditionFactory(std::move(condition)),
                       TrueFactory(std::move(if_true)),
                       FalseFactory(std::move(if_false))}) {}

  Poll<Result> operator()() {
    return absl::visit(CallPoll<false>{this}, state_);
  }

 private:
  struct Initial {
    ConditionFactory condition;
    TrueFactory if_true;
    FalseFactory if_false;
  };
  struct Evaluating {
    ConditionPromise condition;
    TrueFactory if_true;
    FalseFactory if_false;
  };
  using State = absl::variant<Initial, Evaluating, TruePromise, FalsePromise>;
  State state_;

  template <bool kSetState>
  struct CallPoll {
    using PollResult = Poll<Result>;

    If* const self;

    PollResult operator()(Initial& initial) const {
      Evaluating evaluating{
          initial.condition.Once(),
          std::move(initial.if_true),
          std::move(initial.if_false),
      };
      return CallPoll<true>{self}(evaluating);
    }

    PollResult operator()(Evaluating& evaluating) const {
      auto r = evaluating.condition();
      if (auto* p = r.get_ready()) {
        return Choose(CallPoll<true>{self}, std::move(*p), &evaluating.if_true,
                      &evaluating.if_false);
      }
      if (kSetState) {
        self->state_.template emplace<Evaluating>(std::move(evaluating));
      }
      return PENDING;
    }

    template <class Promise>
    PollResult operator()(Promise& promise) const {
      auto r = promise();
      if (kSetState && r.pending()) {
        self->state_.template emplace<Promise>(std::move(promise));
      }
      return r;
    }
  };
};

}  // namespace if_detail

// If promise combinator.
// Takes 3 promise factories, and evaluates the first.
// If it returns failure, returns failure for the entire combinator.
// If it returns true, evaluates the second promise.
// If it returns false, evaluates the third promise.
template <typename C, typename T, typename F>
if_detail::If<C, T, F> If(C condition, T if_true, F if_false) {
  return if_detail::If<C, T, F>(std::move(condition), std::move(if_true),
                                std::move(if_false));
}

}  // namespace grpc_core

#endif
