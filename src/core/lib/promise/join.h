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
#include "src/core/lib/promise/adaptor.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace join_detail {

// The combined promise.
template <typename... Promise>
class Join;

template <typename F>
union Fused {
  Fused() {}
  ~Fused() {}

  using Result = typename decltype(std::declval<F>()())::Type;
  [[no_unique_address]] F pending;
  [[no_unique_address]] Result ready;

  void CallDestruct(bool ready) {
    if (ready) {
      Destruct(&ready);
    } else {
      Destruct(&pending);
    }
  }

  template <typename T>
  void Poll(T* state, T control_bit) {
    if ((*state & control_bit) == 0) {
      auto r = pending();
      if (auto* p = r.get_ready()) {
        *state |= control_bit;
        Destruct(&pending);
        Construct(&ready, std::move(*p));
      }
    }
  }
};

#include "join_switch.h"

}  // namespace join_detail

/// Combinator to run all promises to completion, and return a tuple
/// of their results.
template <typename... Promise>
join_detail::Join<Promise...> Join(Promise... promises) {
  return join_detail::Join<Promise...>(std::move(promises)...);
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_JOIN_H
