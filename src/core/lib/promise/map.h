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

#ifndef GRPC_CORE_LIB_PROMISE_MAP_H
#define GRPC_CORE_LIB_PROMISE_MAP_H

#include <grpc/impl/codegen/port_platform.h>

#include "absl/types/variant.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace map_detail {

template <typename Promise, typename Fn>
class Map {
 public:
  Map(Promise promise, Fn fn)
      : promise_(std::move(promise)), fn_(std::move(fn)) {}

  using PromiseResult =
      typename PollTraits<decltype(std::declval<Promise>()())>::Type;
  using Result = absl::remove_reference_t<decltype(
      std::declval<Fn>()(std::declval<PromiseResult>()))>;

  Poll<Result> operator()() {
    auto r = promise_();
    if (auto* p = absl::get_if<kPollReadyIdx>(&r)) {
      return fn_(std::move(*p));
    }
    return Pending();
  }

 private:
  Promise promise_;
  Fn fn_;
};

}  // namespace map_detail

// Mapping combinator.
// Takes a promise, and a synchronous function to mutate it's result, and
// returns a promise.
template <typename Promise, typename Fn>
map_detail::Map<Promise, Fn> Map(Promise promise, Fn fn) {
  return map_detail::Map<Promise, Fn>(std::move(promise), std::move(fn));
}

// Map functor to take the N-th element of a tuple
template <size_t N>
struct JustElem {
  template <typename... Types>
  auto operator()(std::tuple<Types...>&& t) -> decltype(std::get<N>(t)) {
    return std::get<N>(t);
  }
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_MAP_H
