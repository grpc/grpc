// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_SWITCH_H
#define GRPC_SRC_CORE_LIB_PROMISE_SWITCH_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <utility>

#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/detail/promise_variant.h"
#include "src/core/lib/promise/if.h"
#include "src/core/util/crash.h"

namespace grpc_core {

namespace promise_detail {
template <typename D, D discriminator, typename F>
struct Case {
  using Factory = OncePromiseFactory<void, F>;
  explicit Case(F f) : factory(std::move(f)) {}
  Factory factory;
  static bool Matches(D value) { return value == discriminator; }
};

template <typename F>
struct Default {
  using Factory = OncePromiseFactory<void, F>;
  explicit Default(F f) : factory(std::move(f)) {}
  Factory factory;
};

template <typename Promise, typename D, typename F>
Promise ConstructSwitchPromise(D, Default<F>& def) {
  return def.factory.Make();
}

template <typename Promise, typename D, typename Case, typename... OtherCases>
Promise ConstructSwitchPromise(D discriminator, Case& c, OtherCases&... cs) {
  if (Case::Matches(discriminator)) return c.factory.Make();
  return ConstructSwitchPromise<Promise>(discriminator, cs...);
}

template <typename D, typename... Cases>
auto SwitchImpl(D discriminator, Cases&... cases) {
  using Promise = absl::variant<typename Cases::Factory::Promise...>;
  return PromiseVariant<Promise>(
      ConstructSwitchPromise<Promise>(discriminator, cases...));
}

}  // namespace promise_detail

// TODO(ctiller): when we have C++17, make this
// template <auto D, typename PromiseFactory>.
// (this way we don't need to list the type on /every/ case)
template <typename D, D discriminator, typename PromiseFactory>
auto Case(PromiseFactory f) {
  return promise_detail::Case<D, discriminator, PromiseFactory>{std::move(f)};
}

template <typename PromiseFactory>
auto Default(PromiseFactory f) {
  return promise_detail::Default<PromiseFactory>{std::move(f)};
}

// Given a list of cases that result in promise factories, return a single
// promise chosen by the discriminator (the first argument of this function).
// e.g.:
// Switch(1, Case<1>([] { return 43; }), Case<2>([] { return 44; }))
// resolves to 43.
// TODO(ctiller): consider writing a code-generator like we do for seq/join
// so that this lowers into a C switch statement.
template <typename D, typename... C>
auto Switch(D discriminator, C... cases) {
  return promise_detail::SwitchImpl(discriminator, cases...);
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_SWITCH_H
