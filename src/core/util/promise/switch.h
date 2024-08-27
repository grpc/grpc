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

#include <memory>
#include <utility>

#include <grpc/support/port_platform.h>

#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/if.h"

namespace grpc_core {

namespace promise_detail {
template <typename D, typename F>
struct Case {
  D discriminator;
  F factory;
};

template <typename F>
struct Default {
  F factory;
};
}  // namespace promise_detail

template <typename D, typename PromiseFactory>
auto Case(D discriminator, PromiseFactory f) {
  return promise_detail::Case<D, PromiseFactory>{discriminator, std::move(f)};
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
template <typename D, typename F>
auto Switch(D, promise_detail::Default<F> def) {
  return promise_detail::OncePromiseFactory<void, F>(std::move(def.factory))
      .Make();
}

template <typename D, typename F, typename... Others>
auto Switch(D discriminator, promise_detail::Case<D, F> c, Others... others) {
  return If(discriminator == c.discriminator, std::move(c.factory),
            Switch(discriminator, std::move(others)...));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_SWITCH_H
