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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_MATCH_PROMISE_H
#define GRPC_SRC_CORE_LIB_PROMISE_MATCH_PROMISE_H

#include "absl/types/variant.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/detail/promise_variant.h"
#include "src/core/util/overload.h"

namespace grpc_core {

namespace promise_detail {

// This types job is to visit a supplied variant, and apply a mapping
// Constructor from input types to promises, returning a variant full of
// promises.
template <typename Constructor, typename... Ts>
struct ConstructPromiseVariantVisitor {
  // Factory functions supplied to the top level `Match` object, wrapped by
  // OverloadType to become overloaded members.
  Constructor constructor;

  // Helper function... only callable once.
  // Given a value, construct a Promise Factory that accepts that value type,
  // and uses the constructor type above to map from that type to a promise
  // returned by the factory.
  // We use the Promise Factory infrastructure to deal with all the common
  // variants of factory signatures that we've found to be convenient.
  template <typename T>
  auto CallConstructorThenFactory(T x) {
    OncePromiseFactory<T, Constructor> factory(std::move(constructor));
    return factory.Make(std::move(x));
  }

  // Polling operator.
  // Given a visited type T, construct a Promise Factory, use it, and then cast
  // the result into a variant type that covers ALL of the possible return types
  // given the input types listed in Ts...
  template <typename T>
  auto operator()(T x) -> absl::variant<promise_detail::PromiseLike<
      decltype(CallConstructorThenFactory(std::declval<Ts>()))>...> {
    return CallConstructorThenFactory(std::move(x));
  }
};

}  // namespace promise_detail

// Match for promises
// Like the Match function takes a variant of some set of types,
// and a set of functions - one per variant type.
// We use these functions as Promise Factories, and return a Promise that can be
// polled selected by the type that was in the variant.
template <typename... Fs, typename... Ts>
auto MatchPromise(absl::variant<Ts...> value, Fs... fs) {
  // Construct a variant of promises using the factory functions fs, selected by
  // the type held by value.
  auto body = absl::visit(
      promise_detail::ConstructPromiseVariantVisitor<OverloadType<Fs...>,
                                                     Ts...>{
          OverloadType<Fs...>(std::move(fs)...)},
      std::move(value));
  // Wrap that in a PromiseVariant that provides the promise API on the wrapped
  // variant.
  return promise_detail::PromiseVariant<decltype(body)>(std::move(body));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_MATCH_PROMISE_H
