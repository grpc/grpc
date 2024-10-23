// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_MATCH_H
#define GRPC_SRC_CORE_LIB_PROMISE_MATCH_H

#include "absl/types/variant.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/util/overload.h"

namespace grpc_core {

namespace promise_detail {

template <typename Constructor, typename... Ts>
struct ConstructPromiseVariantVisitor {
  Constructor constructor;

  template <typename T>
  auto operator()(T x)
      -> absl::variant<promise_detail::PromiseLike<
          decltype(std::declval<Constructor>()(std::declval<Ts>()))...>> {
    return constructor(std::move(x));
  }
};

class PollVisitor {
 public:
  template <typename T>
  auto operator()(T& x) {
    return x();
  }
};

template <typename V>
class PromiseVariant {
 public:
  explicit PromiseVariant(V variant) : variant_(std::move(variant)) {}
  auto operator()() { return absl::visit(PollVisitor(), variant_); }

 private:
  V variant_;
};

}  // namespace promise_detail

template <typename... Fs, typename... Ts>
auto MatchPromise(absl::variant<Ts...> value, Fs... fs) {
  auto body = absl::visit(
      promise_detail::ConstructPromiseVariantVisitor<OverloadType<Fs...>,
                                                     Ts...>(
          OverloadType<Fs...>(std::move(fs)...)),
      std::move(value));
  return promise_detail::PromiseVariant<decltype(body)>(std::move(body));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_MATCH_H
