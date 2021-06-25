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

namespace grpc_core {

struct Continue {};

template <typename T>
using LoopCtl = absl::variant<Continue, T>;

namespace promise_detail {

template <typename F>
class Loop {
 private:
  using Factory = promise_detail::PromiseFactory<void, F>;
  using Promise = decltype(std::declval<Factory>().Repeated());
  using PromiseResult =
      typename PollTraits<decltype(std::declval<Promise>()())>::Type;

 public:
  using Result =
      typename decltype(Step(std::declval<PromiseResult>()))::value_type;

  explicit Loop(F f) : factory_(std::move(f)), promise_(factory_.Repeated()) {}

  Poll<Result> operator()() {
    while (true) {
      auto promise_result = (*promise_)();
      if (auto* p = absl::get_if<kPollReadyIdx>(&promise_result)) {
        if (absl::holds_alternative<Continue>(*p)) {
            promise_ = factory_.Repeated();
            continue;
        }
        return std::move(*absl::get<Result>())
      } else {
        return Pending();
      }
    }
  }

 private:
  [[no_unique_address]] Factory factory_;
  [[no_unique_address]] Promise promise_;
};

}  // namespace promise_detail

}  // namespace grpc_core

#endif
