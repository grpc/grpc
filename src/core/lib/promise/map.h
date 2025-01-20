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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_MAP_H
#define GRPC_SRC_CORE_LIB_PROMISE_MAP_H

#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace promise_detail {

template <typename Fn, typename Arg, typename SfinaeVoid = void>
class WrappedFn;

template <typename Fn, typename Arg>
class WrappedFn<
    Fn, Arg, std::enable_if_t<!std::is_void_v<std::invoke_result_t<Fn, Arg>>>> {
 public:
  using Result = RemoveCVRef<std::invoke_result_t<Fn, Arg>>;
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION explicit WrappedFn(Fn&& fn)
      : fn_(std::move(fn)) {}
  WrappedFn(const WrappedFn&) = delete;
  WrappedFn& operator=(const WrappedFn&) = delete;
  WrappedFn(WrappedFn&&) = default;
  WrappedFn& operator=(WrappedFn&&) = default;
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Result operator()(Arg&& arg) {
    return fn_(std::forward<Arg>(arg));
  }

 private:
  GPR_NO_UNIQUE_ADDRESS Fn fn_;
};

template <typename Fn, typename Arg>
class WrappedFn<
    Fn, Arg, std::enable_if_t<std::is_void_v<std::invoke_result_t<Fn, Arg>>>> {
 public:
  using Result = Empty;
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION explicit WrappedFn(Fn&& fn)
      : fn_(std::move(fn)) {}
  WrappedFn(const WrappedFn&) = delete;
  WrappedFn& operator=(const WrappedFn&) = delete;
  WrappedFn(WrappedFn&&) = default;
  WrappedFn& operator=(WrappedFn&&) = default;
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Empty operator()(Arg&& arg) {
    fn_(std::forward<Arg>(arg));
    return Empty{};
  }

 private:
  GPR_NO_UNIQUE_ADDRESS Fn fn_;
};

template <typename PromiseResult, typename Fn0, typename Fn1>
class FusedFns {
  using InnerResult =
      decltype(std::declval<Fn0>()(std::declval<PromiseResult>()));

 public:
  using Result = typename WrappedFn<Fn1, InnerResult>::Result;

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION FusedFns(Fn0 fn0, Fn1 fn1)
      : fn0_(std::move(fn0)), fn1_(std::move(fn1)) {}
  FusedFns(const FusedFns&) = delete;
  FusedFns& operator=(const FusedFns&) = delete;
  FusedFns(FusedFns&&) = default;
  FusedFns& operator=(FusedFns&&) = default;

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Result operator()(PromiseResult arg) {
    InnerResult inner_result = fn0_(std::move(arg));
    return fn1_(std::move(inner_result));
  }

 private:
  GPR_NO_UNIQUE_ADDRESS Fn0 fn0_;
  GPR_NO_UNIQUE_ADDRESS WrappedFn<Fn1, InnerResult> fn1_;
};

}  // namespace promise_detail

// Mapping combinator.
// Takes a promise, and a synchronous function to mutate its result, and
// returns a promise.
template <typename Promise, typename Fn>
class Map {
  using PromiseType = promise_detail::PromiseLike<Promise>;

 public:
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Map(Promise promise, Fn fn)
      : promise_(std::move(promise)), fn_(std::move(fn)) {}

  Map(const Map&) = delete;
  Map& operator=(const Map&) = delete;
  // NOLINTNEXTLINE(performance-noexcept-move-constructor): clang6 bug
  Map(Map&& other) = default;
  // NOLINTNEXTLINE(performance-noexcept-move-constructor): clang6 bug
  Map& operator=(Map&& other) = default;

  using PromiseResult = typename PromiseType::Result;
  using Result = typename promise_detail::WrappedFn<Fn, PromiseResult>::Result;

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Poll<Result> operator()() {
    Poll<PromiseResult> r = promise_();
    if (auto* p = r.value_if_ready()) {
      return fn_(std::move(*p));
    }
    return Pending();
  }

 private:
  template <typename SomeOtherPromise, typename SomeOtherFn>
  friend class Map;

  GPR_NO_UNIQUE_ADDRESS PromiseType promise_;
  GPR_NO_UNIQUE_ADDRESS promise_detail::WrappedFn<Fn, PromiseResult> fn_;
};

template <typename Promise, typename Fn0, typename Fn1>
class Map<Map<Promise, Fn0>, Fn1> {
  using InnerMapFn = decltype(std::declval<Map<Promise, Fn0>>().fn_);
  using FusedFn =
      promise_detail::FusedFns<typename Map<Promise, Fn0>::PromiseResult,
                               InnerMapFn, Fn1>;
  using PromiseType = typename Map<Promise, Fn0>::PromiseType;

 public:
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Map(Map<Promise, Fn0> map, Fn1 fn1)
      : promise_(std::move(map.promise_)),
        fn_(FusedFn(std::move(map.fn_), std::move(fn1))) {}

  Map(const Map&) = delete;
  Map& operator=(const Map&) = delete;
  // NOLINTNEXTLINE(performance-noexcept-move-constructor): clang6 bug
  Map(Map&& other) = default;
  // NOLINTNEXTLINE(performance-noexcept-move-constructor): clang6 bug
  Map& operator=(Map&& other) = default;

  using PromiseResult = typename Map<Promise, Fn0>::PromiseResult;
  using Result = typename FusedFn::Result;

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Poll<Result> operator()() {
    Poll<PromiseResult> r = promise_();
    if (auto* p = r.value_if_ready()) {
      return fn_(std::move(*p));
    }
    return Pending();
  }

 private:
  template <typename SomeOtherPromise, typename SomeOtherFn>
  friend class Map;

  GPR_NO_UNIQUE_ADDRESS PromiseType promise_;
  GPR_NO_UNIQUE_ADDRESS FusedFn fn_;
};

template <typename Promise, typename Fn>
Map(Promise, Fn) -> Map<Promise, Fn>;

// Maps a promise to a new promise that returns a tuple of the original result
// and a bool indicating whether there was ever a Pending{} value observed from
// polling.
template <typename Promise>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION inline auto CheckDelayed(Promise promise) {
  using P = promise_detail::PromiseLike<Promise>;
  return [delayed = false, promise = P(std::move(promise))]() mutable
             -> Poll<std::tuple<typename P::Result, bool>> {
    auto r = promise();
    if (r.pending()) {
      delayed = true;
      return Pending{};
    }
    return std::make_tuple(std::move(r.value()), delayed);
  };
}

// Callable that takes a tuple and returns one element
template <size_t kElem>
struct JustElem {
  template <typename... A>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto operator()(std::tuple<A...>&& t)
      const -> decltype(std::get<kElem>(std::forward<std::tuple<A...>>(t))) {
    return std::get<kElem>(std::forward<std::tuple<A...>>(t));
  }
  template <typename... A>
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto operator()(
      const std::tuple<A...>& t) const -> decltype(std::get<kElem>(t)) {
    return std::get<kElem>(t);
  }
};

namespace promise_detail {
template <typename Fn>
class MapError {
 public:
  explicit MapError(Fn fn) : fn_(std::move(fn)) {}
  absl::Status operator()(absl::Status status) {
    if (status.ok()) return status;
    return fn_(std::move(status));
  }
  template <typename T>
  absl::StatusOr<T> operator()(absl::StatusOr<T> status) {
    if (status.ok()) return status;
    return fn_(std::move(status.status()));
  }

 private:
  Fn fn_;
};
}  // namespace promise_detail

// Map status->better status in the case of errors
template <typename Promise, typename Fn>
auto MapErrors(Promise promise, Fn fn) {
  return Map(std::move(promise), promise_detail::MapError<Fn>(std::move(fn)));
}

// Simple mapper to add a prefix to the message of an error
template <typename Promise>
auto AddErrorPrefix(absl::string_view prefix, Promise promise) {
  return MapErrors(std::move(promise), [prefix](absl::Status status) {
    absl::Status out(status.code(), absl::StrCat(prefix, status.message()));
    status.ForEachPayload(
        [&out](absl::string_view name, const absl::Cord& value) {
          out.SetPayload(name, value);
        });
    return out;
  });
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_MAP_H
