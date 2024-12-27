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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_JOIN_H
#define GRPC_SRC_CORE_LIB_PROMISE_JOIN_H

#include <grpc/support/port_platform.h>
#include <stdlib.h>

#include <tuple>

#include "absl/meta/type_traits.h"
#include "src/core/lib/promise/detail/join_state.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/map.h"

namespace grpc_core {
namespace promise_detail {

// Join Promise Combinator
//
// The Join promise combinator takes as inputs multiple promises.
// When the Join promise is polled, these input promises will be executed
// serially on the same thread.
// Each promise being executed either returns a value or Pending{}.
// Each subsequent execution of the Join will only execute the input promises
// which returned Pending{} in any of the previous executions. This mechanism
// ensures that no promise is executed after it resolves, which is an essential
// requirement.
//
// Suppose you have three promises
// 1.  First promise returning type Poll<int>
// 2.  Second promise returning type Poll<bool>
// 3.  Third promise returning type Poll<double>
// Then you poll the Join of theses three promises, the result will have the
// type Poll<std::tuple<int, bool, double>>
//
// Polling this join promise will
// 1.  Return Pending{} if even one promise in the input list of promises
// returns Pending{}
// 2.  Return the tuple if all promises are resolved.
//
// Polling this Join combinator promise will make the pending promises run
// serially, in order and on the same thread.
//
// All promises in the input list will be executed irrespective of failure
// status. If you want the promise execution to stop when there is a failure in
// any one promise, consider using TryJoin promise combinator instead of the
// Join combinator.
//
// Example of Join : Refer to join_test.cc

struct JoinTraits {
  template <typename T>
  using ResultType = absl::remove_reference_t<T>;
  template <typename T>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static bool IsOk(const T&) {
    return true;
  }
  template <typename T>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static T Unwrapped(T x) {
    return x;
  }
  template <typename R, typename T>
  static R EarlyReturn(T) {
    abort();
  }
  template <typename... A>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static std::tuple<A...> FinalReturn(
      A... a) {
    return std::make_tuple(std::move(a)...);
  }
};

template <typename... Promises>
class Join {
 public:
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION explicit Join(Promises... promises)
      : state_(std::move(promises)...) {}
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto operator()() {
    return state_.PollOnce();
  }

 private:
  JoinState<JoinTraits, Promises...> state_;
};

struct WrapInTuple {
  template <typename T>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION std::tuple<T> operator()(T x) {
    return std::make_tuple(std::move(x));
  }
};

}  // namespace promise_detail

template <typename... Promise>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline promise_detail::Join<Promise...>
Join(Promise... promises) {
  return promise_detail::Join<Promise...>(std::move(promises)...);
}

template <typename F>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline auto Join(F promise) {
  return Map(std::move(promise), promise_detail::WrapInTuple{});
}

template <typename Iter, typename FactoryFn>
inline auto JoinIter(Iter begin, Iter end, FactoryFn factory_fn) {
  using Factory =
      promise_detail::RepeatedPromiseFactory<decltype(*begin), FactoryFn>;
  Factory factory(std::move(factory_fn));
  using Promise = typename Factory::Promise;
  using Result = typename Promise::Result;
  using State = absl::variant<Promise, Result>;
  std::vector<State> state;
  for (Iter it = begin; it != end; ++it) {
    state.emplace_back(factory.Make(*it));
  }
  return [state = std::move(state)]() mutable -> Poll<std::vector<Result>> {
    bool still_working = false;
    for (auto& s : state) {
      if (auto* promise = absl::get_if<Promise>(&s)) {
        auto p = (*promise)();
        if (auto* r = p.value_if_ready()) {
          s.template emplace<Result>(std::move(*r));
        } else {
          still_working = true;
        }
      }
    }
    if (!still_working) {
      std::vector<Result> output;
      for (auto& s : state) {
        output.emplace_back(std::move(absl::get<Result>(s)));
      }
      return output;
    }
    return Pending{};
  };
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_JOIN_H
