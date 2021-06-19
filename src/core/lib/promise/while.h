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

#ifndef GRPC_CORE_LIB_PROMISE_WHILE_H
#define GRPC_CORE_LIB_PROMISE_WHILE_H

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace while_detail {

struct Empty {};

// Step functions.
// Each of these takes the result of a While Promise, and returns an
// optional<R>. If the optional has a value, then that value is returned from
// the promise. If the optional is empty, then the while loop continues on its
// next iteration. In this way Step is used to control iteration AND to
// determine the result of the While combinator.

inline absl::optional<Poll<Empty>> Step(bool cont) {
  if (!cont) {
    return ready(Empty{});
  } else {
    return {};
  }
}

template <typename T>
absl::optional<T> Step(absl::optional<T> result) {
  if (result.has_value()) {
    return std::move(*result);
  } else {
    return {};
  }
}

inline absl::optional<absl::Status> Step(absl::StatusOr<bool> result) {
  if (!result.ok()) {
    return absl::Status(result.status());
  } else if (*result) {
    return {};
  } else {
    return absl::OkStatus();
  }
}

template <typename T>
absl::optional<absl::StatusOr<T>> Step(
    absl::StatusOr<absl::optional<T>> result) {
  if (!result.ok()) {
    return absl::StatusOr<T>(result.status());
  } else if (result->has_value()) {
    return absl::StatusOr<T>(std::move(**result));
  } else {
    return {};
  }
}

template <typename F>
class While {
 private:
  using Factory = promise_detail::PromiseFactory<void, F>;
  using Promise = decltype(std::declval<Factory>().Repeated());
  using PromiseResult = typename decltype(std::declval<Promise>()())::Type;

 public:
  using Result =
      typename decltype(Step(std::declval<PromiseResult>()))::value_type;

  explicit While(F f) : factory_(std::move(f)) {}

  Poll<Result> operator()() {
    while (true) {
      if (!promise_.has_value()) {
        promise_.emplace(factory_.Repeated());
      }
      auto promise_result = (*promise_)();
      if (auto* p = promise_result.get_ready()) {
        promise_.reset();
        auto step_result = Step(std::move(*p));
        if (step_result.has_value()) {
          return ready(std::move(*step_result));
        }
        // Continue iteration.
      } else {
        return kPending;
      }
    }
  }

 private:
  Factory factory_;
  absl::optional<Promise> promise_;
};

}  // namespace while_detail

// Execute F while it returns a true value.
// F can be a promise factory or a promise.
// If F is a promise, it's turned into a promise factory via copy construction
// - i.e. a new promise is constructed per iteration always.
// F's result can be:
// - bool
//   true indicates continue to next iteration.
//   false indicates stop.
//   The overall result of the While promise will be an unnamable type.
// - optional<T>
//   A value indicates completion.
//   No value indicates continue to next iteration.
//   The overall result of the While promise will be T.
// - StatusOr<bool>
//   Ok(true) indicates continue to next iteration.
//   Ok(false) indicates stop.
//   Non-ok means stop and propagate error.
//   The overall result of the While promise will be Status.
// - StatusOr<optional<T>>
//   Ok(x) with x non-empty indicates completion, with value StatusOr<T>(x).
//         with x empty indicates continue to next iteration.
//   Non-ok means stop and propagate error.
template <typename F>
while_detail::While<F> While(F f) {
  return while_detail::While<F>(std::move(f));
}

}  // namespace grpc_core

#endif
