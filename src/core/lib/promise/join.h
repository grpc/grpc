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

#ifndef GRPC_CORE_LIB_PROMISE_JOIN_H
#define GRPC_CORE_LIB_PROMISE_JOIN_H

#include "absl/types/variant.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace join_detail {

// Track either the functor providing a promise, or the result of the promise.
// Can be polled after the promise finishes, since it keeps track of that.
template <typename Functor>
class Fused {
 public:
  using Result = typename decltype(std::declval<Functor>()())::Type;

  explicit Fused(Functor f) : f_(std::move(f)) {}

  // Returns true if the promise is completed.
  bool Poll() { return absl::visit(CallPoll{this}, f_); }

  // Move the result out of storage.
  Result take() { return std::move(absl::get<Result>(f_)); }

 private:
  // Combined state - either we are a promise or the result.
  // TODO(ctiller): Is it worth doing a bool/union split to avoid excessive
  // generality from variant<> here?
  absl::variant<Functor, Result> f_;

  struct CallPoll {
    Fused* const fused;

    bool operator()(Functor& f) {
      auto r = f();
      if (auto* p = r.get_ready()) {
        fused->f_.template emplace<Result>(std::move(*p));
        return true;
      }
      return false;
    }

    bool operator()(Result&) { return true; }
  };
};

// Helper to recursively poll through a set of Promises, and early out if
// any are pending.
template <typename... Promises>
struct PollAll;

template <typename Promise, typename... Promises>
struct PollAll<Promise, Promises...> {
  static bool Poll(Fused<Promise>& p, Fused<Promises>&... next) {
    const bool a = p.Poll();
    const bool b = PollAll<Promises...>::Poll(next...);
    return a && b;
  }
};

template <>
struct PollAll<> {
  static bool Poll() { return true; }
};

// The combined promise.
template <typename... Promise>
class Join {
 public:
  explicit Join(Promise... promises)
      : state_(Fused<Promise>(std::move(promises))...) {}

  Poll<std::tuple<typename Fused<Promise>::Result...>> operator()() {
    // Check if everything is ready
    if (absl::apply(
            [](Fused<Promise>&... promise) {
              return PollAll<Promise...>::Poll(promise...);
            },
            state_)) {
      // If it is, return the tuple of results.
      return absl::apply(
          [](Fused<Promise>&... fused) {
            return std::make_tuple(fused.take()...);
          },
          state_);
    } else {
      return kPending;
    }
  }

 private:
  std::tuple<Fused<Promise>...> state_;
};

}  // namespace join_detail

/// Combinator to run all promises to completion, and return a tuple
/// of their results.
template <typename... Promise>
join_detail::Join<Promise...> Join(Promise... promises) {
  return join_detail::Join<Promise...>(std::move(promises)...);
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_JOIN_H
