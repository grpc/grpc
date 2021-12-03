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

#ifndef GRPC_CORE_LIB_PROMISE_LOOP_H
#define GRPC_CORE_LIB_PROMISE_LOOP_H

#include <grpc/support/port_platform.h>

#include <new>
#include <type_traits>

#include "absl/types/variant.h"

#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

// Special type - signals to loop to take another iteration, instead of
// finishing
struct Continue {};

// Result of polling a loop promise - either Continue looping, or return a value
// T
template <typename T>
using LoopCtl = absl::variant<Continue, T>;

namespace promise_detail {

template <typename T>
struct LoopTraits;

template <typename T>
struct LoopTraits<LoopCtl<T>> {
  using Result = T;
};

template <typename F>
class Loop {
 private:
  using Factory = promise_detail::PromiseFactory<void, F>;
  using Promise = decltype(std::declval<Factory>().Repeated());
  using PromiseResult = typename Promise::Result;

 public:
  using Result = typename LoopTraits<PromiseResult>::Result;

  explicit Loop(F f) : factory_(std::move(f)), promise_(factory_.Repeated()) {}
  ~Loop() { promise_.~Promise(); }

  Loop(Loop&& loop) noexcept
      : factory_(std::move(loop.factory_)),
        promise_(std::move(loop.promise_)) {}

  Loop(const Loop& loop) = delete;
  Loop& operator=(const Loop& loop) = delete;

  Poll<Result> operator()() {
    while (true) {
      // Poll the inner promise.
      auto promise_result = promise_();
      // If it returns a value:
      if (auto* p = absl::get_if<kPollReadyIdx>(&promise_result)) {
        //  - then if it's Continue, destroy the promise and recreate a new one
        //  from our factory.
        if (absl::holds_alternative<Continue>(*p)) {
          promise_.~Promise();
          new (&promise_) Promise(factory_.Repeated());
          continue;
        }
        //  - otherwise there's our result... return it out.
        return absl::get<Result>(*p);
      } else {
        // Otherwise the inner promise was pending, so we are pending.
        return Pending();
      }
    }
  }

 private:
  GPR_NO_UNIQUE_ADDRESS Factory factory_;
  GPR_NO_UNIQUE_ADDRESS union { GPR_NO_UNIQUE_ADDRESS Promise promise_; };
};

}  // namespace promise_detail

// Looping combinator.
// Expects F returns LoopCtl<T> - if it's Continue, then run the loop again -
// otherwise yield the returned value as the result of the loop.
template <typename F>
promise_detail::Loop<F> Loop(F f) {
  return promise_detail::Loop<F>(std::move(f));
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_LOOP_H
