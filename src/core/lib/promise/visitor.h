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

#ifndef GRPC_CORE_LIB_PROMISE_VISITOR_H
#define GRPC_CORE_LIB_PROMISE_VISITOR_H

#include <grpc/impl/codegen/port_platform.h>

#include "absl/types/variant.h"
#include "src/core/lib/gprpp/overload.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace visitor_detail {

struct PollPromise {
  template <typename P>
  auto operator()(P& p) -> decltype(p()) {
    return p();
  }
};

template <typename... Promises>
struct ResultOf;

template <typename Promise, typename... Promises>
struct ResultOf<Promise, Promises...> {
  using Type = typename PollTraits<decltype(std::declval<Promise>()())>::Type;
};

template <typename... Promises>
class Promise {
 public:
  template <typename P>
  explicit Promise(P p) : promise_(std::move(p)) {}

  using Result = typename ResultOf<Promises...>::Type;

  Poll<Result> operator()() { return absl::visit(PollPromise{}, promise_); }

 private:
  absl::variant<Promises...> promise_;
};

template <typename... Cases>
class OverloadFactory {
 public:
  template <typename T>
  using AdaptorFactory =
      promise_detail::PromiseFactory<T, OverloadType<Cases...>>;

  explicit OverloadFactory(Cases... cases) : overload_(std::move(cases)...) {}

  template <typename T>
  auto operator()(T value)
      -> decltype(std::declval<AdaptorFactory<T>>().Once(std::move(value))) {
    return AdaptorFactory<T>(std::move(overload_)).Once(std::move(value));
  }

 private:
  OverloadType<Cases...> overload_;
};

template <typename Factory, typename Arg>
using FactoryResultForArg =
    decltype(std::declval<Factory>()(std::move(std::declval<Arg>())));

template <typename Factory, typename... Args>
using PromiseFromFactoryForArgs =
    Promise<FactoryResultForArg<Factory, Args>...>;

template <typename Factory, typename... Args>
struct PromiseFactory {
  template <typename A>
  PromiseFromFactoryForArgs<Factory, Args...> operator()(A& a) {
    return PromiseFromFactoryForArgs<Factory, Args...>(
        (*factory_)(std::move(a)));
  }

  Factory* factory_;
};

template <typename... Cases>
class Visitor {
 public:
  explicit Visitor(Cases... cases) : cases_(std::move(cases)...) {}

  template <typename... Args>
  PromiseFromFactoryForArgs<OverloadFactory<Cases...>, Args...> operator()(
      absl::variant<Args...> arg) {
    return absl::visit(
        PromiseFactory<OverloadFactory<Cases...>, Args...>{&cases_}, arg);
  }

 private:
  OverloadFactory<Cases...> cases_;
};

}  // namespace visitor_detail

// Variant visitor.
// Takes a set of Promises or Promise Factories, and returns a Promise Factory
// that takes an absl::variant as an argument, visits that variant with a
// matching functor in cases, and then takes the resulting Promise and runs it
// to completion.
template <typename... Cases>
visitor_detail::Visitor<Cases...> Visitor(Cases... cases) {
  return visitor_detail::Visitor<Cases...>(std::move(cases)...);
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_VISITOR_H
