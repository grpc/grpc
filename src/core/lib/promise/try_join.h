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

#ifndef GRPC_CORE_LIB_PROMISE_TRY_JOIN_H
#define GRPC_CORE_LIB_PROMISE_TRY_JOIN_H

#include "absl/status/statusor.h"
#include "absl/types/variant.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace try_join_detail {

struct Empty {};

template <typename T>
T IntoResult(absl::StatusOr<T>* status) {
  return std::move(**status);
}

inline Empty IntoResult(absl::Status* status) { return Empty{}; }

// Stores either a promise (if uncompleted) or its result (if completed) so
// that polling can occur after the promise is completed.
template <class Promise>
class Fused {
 public:
  using PromiseResult = typename decltype(std::declval<Promise>()())::Type;
  using Result = decltype(IntoResult(static_cast<PromiseResult*>(nullptr)));

  explicit Fused(Promise promise) : state_(std::move(promise)) {}

  // Poll the underlying promise if we're still executing.
  // Returns error, or Ok(true) if a result is ready, or Ok(false) if pending.
  absl::StatusOr<bool> Poll() { return absl::visit(CallPoll{this}, state_); }

  Result take() { return std::move(absl::get<Result>(state_)); }

 private:
  absl::variant<Promise, Result> state_;

  struct CallPoll {
    Fused* const fused;

    // Poll for the case that we're still executing.
    absl::StatusOr<bool> operator()(Promise& promise) const {
      // Poll the underlying promise.
      auto r = promise();
      // If the promise is complete...
      if (auto* p = r.get_ready()) {
        // ... and it's successful, save the success and return complete.
        if (p->ok()) {
          fused->state_.template emplace<Result>(IntoResult(p));
          return absl::StatusOr<bool>(true);
        }
        // ... otherwise it's failed, and we can return failure immediately.
        return absl::StatusOr<bool>(p->status());
      }
      // If the promise is not complete, return pending.
      return absl::StatusOr<bool>(false);
    }

    // Poll for the case that we've completed -- return that information.
    absl::StatusOr<bool> operator()(Result& result) const {
      return absl::StatusOr<bool>(true);
    }
  };
};

// Recursive helper type to poll a list of children and early out
// appropriately.
template <typename... Promises>
struct PollAll;

template <typename Promise, typename... Promises>
struct PollAll<Promise, Promises...> {
  static absl::StatusOr<bool> Poll(Fused<Promise>& p,
                                   Fused<Promises>&... next) {
    // First poll the top promise.
    auto r = p.Poll();
    // If it failed, then everything will fail and we can early out.
    if (!r.ok()) {
      return r;
    }
    // Note: if it was pending, a later promise might fail, so we need to keep
    // recursing/polling.

    // Poll the remaining children.
    auto n = PollAll<Promises...>::Poll(next...);
    // If any of them failed, then everything has failed.
    if (!n.ok()) {
      return n;
    }
    // If the first or any of the remainder were pending, then the overall
    // promise is so far succeeding but still pending - return pending.
    if (!*r || !*n) {
      return absl::StatusOr<bool>(false);
    }
    // Otherwise return completion.
    return r;
  }
};

template <typename Promise>
struct PollAll<Promise> {
  static absl::StatusOr<bool> Poll(Fused<Promise>& p) { return p.Poll(); }
};

// Implementation of TryJoin combinator.
template <typename... Promises>
class TryJoin {
 public:
  explicit TryJoin(Promises... promises)
      : state_(Fused<Promises>(std::move(promises))...) {}

  using Tuple = std::tuple<typename Fused<Promises>::Result...>;

  Poll<absl::StatusOr<Tuple>> operator()() {
    // Check overall status.
    // Result could be - fail      -> fail
    //                   Ok(true)  -> ready
    //                   Ok(false) -> pending
    auto result = absl::apply(
        [](Fused<Promises>&... promises) {
          return PollAll<Promises...>::Poll(promises...);
        },
        state_);
    // If we failed, return failure.
    if (!result.ok()) {
      return ready(absl::StatusOr<Tuple>(std::move(result.status())));
    }
    // If complete, build and return the tuple.
    if (*result) {
      return absl::apply(
          [](Fused<Promises>&... promises) {
            return ready(
                absl::StatusOr<Tuple>(absl::in_place, promises.take()...));
          },
          state_);
    }
    // Keep pending!
    return PENDING;
  }

 private:
  std::tuple<Fused<Promises>...> state_;
};

}  // namespace try_join_detail

// Run all promises.
// If any fail, cancel the rest and return the failure.
// If all succeed, return Ok(tuple-of-results).
template <typename... Promises>
try_join_detail::TryJoin<Promises...> TryJoin(Promises... promises) {
  return try_join_detail::TryJoin<Promises...>(std::move(promises)...);
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_TRY_JOIN_H
