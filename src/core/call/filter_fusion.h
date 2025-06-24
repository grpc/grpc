// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CALL_FILTER_FUSION_H
#define GRPC_SRC_CORE_CALL_FILTER_FUSION_H
#include <grpc/impl/grpc_types.h>

#include <cstddef>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "src/core/call/call_filters.h"
#include "src/core/call/metadata.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/transport/call_final_info.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/manual_constructor.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/type_list.h"

struct grpc_transport_op;

namespace grpc_core {
namespace filters_detail {

template <typename... Ts>
constexpr bool AllNoInterceptor =
    (std::is_same_v<Ts, const NoInterceptor*> && ...);

enum class MethodVariant {
  kNoInterceptor,
  kSimple,
  kChannelAccess,
};

template <typename... Ts>
constexpr MethodVariant MethodVariantForFilters() {
  if constexpr (AllNoInterceptor<Ts...>) {
    return MethodVariant::kNoInterceptor;
  } else if constexpr (!AnyMethodHasChannelAccess<Ts...>) {
    return MethodVariant::kSimple;
  } else {
    return MethodVariant::kChannelAccess;
  }
}

template <typename T>
using Hdl = Arena::PoolPtr<T>;

template <typename T, typename A>
constexpr bool IsSameExcludingCVRef =
    std::is_same<promise_detail::RemoveCVRef<A>, T>::value;

template <typename T, typename A>
using EnableIfSameExcludingCVRef =
    std::enable_if_t<std::is_same<promise_detail::RemoveCVRef<A>, T>::value,
                     void>;

template <typename T, typename MethodType, MethodType method,
          typename Ignored = void>
class AdaptMethod;

template <typename T, const NoInterceptor* method>
class AdaptMethod<T, const NoInterceptor*, method> {
 public:
  explicit AdaptMethod(void* /*call*/, void* /*filter*/ = nullptr) {}
  auto operator()(Hdl<T> x) {
    return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(x)));
  }
  void operator()(T* /*x*/) {}
};

// Overrides for Filter methods with void Return types.
template <typename T, typename A, typename Call, void (Call::*method)(A)>
class AdaptMethod<T, void (Call::*)(A), method,
                  EnableIfSameExcludingCVRef<T, A>> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/ = nullptr) : call_(call) {}

  auto operator()(Hdl<T> x) {
    (call_->*method)(*x);
    return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(x)));
  }

 private:
  Call* call_;
};

template <typename T, typename Call, void (Call::*method)()>
class AdaptMethod<T, void (Call::*)(), method> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/ = nullptr) : call_(call) {}
  auto operator()(Hdl<T> x) {
    (call_->*method)();
    return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(x)));
  }

 private:
  Call* call_;
};

template <typename T, typename A, typename Call, typename Derived,
          void (Call::*method)(A, Derived*)>
class AdaptMethod<T, void (Call::*)(A, Derived*), method,
                  EnableIfSameExcludingCVRef<T, A>> {
 public:
  explicit AdaptMethod(Call* call, Derived* filter)
      : call_(call), filter_(filter) {}
  auto operator()(Hdl<T> x) {
    (call_->*method)(*x, filter_);
    return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(x)));
  }

 private:
  Call* call_;
  Derived* filter_;
};

// primary template handles types that have no nested ::Call member:
template <class, class = void>
constexpr const bool kHasCallMember = false;

// specialization recognizes types that have a nested ::Call member:
template <class T>
constexpr const bool kHasCallMember<T, std::void_t<typename T::Call>> = true;

// Override for filter method types that take a pointer to the filter along
// with another arbitrary pointer type. Expected to be used to handle
// OnFinalize methods.
template <typename A, typename Call, typename Derived,
          void (Call::*method)(const A*, Derived*)>
class AdaptMethod<
    A, void (Call::*)(const A*, Derived*), method,
    std::enable_if_t<std::is_same<typename Derived::Call, Call>::value, void>> {
 public:
  explicit AdaptMethod(Call* call, Derived* filter)
      : call_(call), filter_(filter) {}
  void operator()(A* arg) { (call_->*method)(arg, filter_); }

 private:
  Call* call_;
  Derived* filter_;
};

// Same as above with the const qualifiers removed.
template <typename A, typename Call, typename Derived,
          void (Call::*method)(A*, Derived*)>
class AdaptMethod<
    A, void (Call::*)(A*, Derived*), method,
    std::enable_if_t<std::is_same<typename Derived::Call, Call>::value, void>> {
 public:
  explicit AdaptMethod(Call* call, Derived* filter)
      : call_(call), filter_(filter) {}
  void operator()(A* arg) { (call_->*method)(arg, filter_); }

 private:
  Call* call_;
  Derived* filter_;
};

// Override for filter method types that another arbitrary pointer type.
// Expected to be used to handle OnFinalize methods.
template <typename A, typename Call, void (Call::*method)(const A*)>
class AdaptMethod<A, void (Call::*)(const A*), method,
                  std::enable_if_t<!kHasCallMember<A>, void>> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/) : call_(call) {}
  void operator()(A* arg) { (call_->*method)(arg); }

 private:
  Call* call_;
};

// Same as above with the const qualifiers removed.
template <typename A, typename Call, void (Call::*method)(A*)>
class AdaptMethod<A, void (Call::*)(A*), method,
                  std::enable_if_t<!kHasCallMember<A>, void>> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/) : call_(call) {}
  void operator()(A* arg) { (call_->*method)(arg); }

 private:
  Call* call_;
};

template <typename T, typename AnyType = void>
struct TakeValueExists {
  static constexpr bool value = false;
};

template <typename T>
struct TakeValueExists<T,
                       absl::void_t<decltype(TakeValue(std::declval<T>()))>> {
  static constexpr bool value = true;
};

template <typename T, typename AnyType = void>
struct StatusType {
  static constexpr bool value = false;
};

template <typename T>
struct StatusType<
    T, absl::enable_if_t<
           std::is_same<decltype(IsStatusOk(std::declval<T>())), bool>::value &&
               !std::is_same<T, ServerMetadataHandle>::value &&
               !TakeValueExists<T>::value,
           void>> {
  static constexpr bool value = true;
};

template <typename T, typename = void>
struct HasStatusMethod {
  static constexpr bool value = false;
};

template <typename T>
struct HasStatusMethod<
    T, std::enable_if_t<!std::is_void_v<decltype(std::declval<T>().status())>,
                        void>> {
  static constexpr bool value = true;
};

template <typename T>
constexpr bool IsPromise = std::is_invocable_v<T>;

// For types T which are of the form StatusOr<U>. Type TakeValue on Type T
// must return a value of type U. Further type T must have a method called
// status() and must return a bool when IsStatusOk is called on an object of
// type T.
template <typename T, typename U, typename AnyType = void>
struct StatusOrType {
  static constexpr bool value = false;
};
template <typename T, typename U>
struct StatusOrType<
    T, U,
    absl::enable_if_t<
        std::is_same<decltype(IsStatusOk(std::declval<T>())), bool>::value &&
            TakeValueExists<T>::value && HasStatusMethod<T>::value &&
            std::is_same<decltype(TakeValue(std::declval<T>())), U>::value,
        void>> {
  static constexpr bool value = true;
};

// Overrides for Filter methods with a Return type supporting a bool ok()
// method without holding a value within e.g., for absl::Status or StatusFlag
// return types.
template <typename T, typename A, typename R, typename Call,
          R (Call::*method)(A)>
class AdaptMethod<
    T, R (Call::*)(A), method,
    absl::enable_if_t<StatusType<R>::value && IsSameExcludingCVRef<T, A>,
                      void>> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/ = nullptr) : call_(call) {}
  auto operator()(Hdl<T> x) {
    R result = (call_->*method)(*x);
    if (IsStatusOk(result)) {
      return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(x)));
    }
    return Immediate(
        ServerMetadataOrHandle<T>::Failure(ServerMetadataFromStatus(result)));
  }

 private:
  Call* call_;
};

template <typename T, typename R, typename Call, R (Call::*method)()>
class AdaptMethod<T, R (Call::*)(), method,
                  absl::enable_if_t<StatusType<R>::value, void>> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/ = nullptr) : call_(call) {}
  auto operator()(Hdl<T> x) {
    R result = (call_->*method)();
    if (IsStatusOk(result)) {
      return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(x)));
    };
    return Immediate(
        ServerMetadataOrHandle<T>::Failure(ServerMetadataFromStatus(result)));
  }

 private:
  Call* call_;
};

template <typename T, typename A, typename R, typename Call, typename Derived,
          R (Call::*method)(A, Derived*)>
class AdaptMethod<
    T, R (Call::*)(A, Derived*), method,
    absl::enable_if_t<StatusType<R>::value && IsSameExcludingCVRef<T, A>,
                      void>> {
 public:
  explicit AdaptMethod(Call* call, Derived* filter)
      : call_(call), filter_(filter) {}
  auto operator()(Hdl<T> x) {
    R result = (call_->*method)(*x, filter_);
    if (IsStatusOk(result)) {
      return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(x)));
    }
    return Immediate(
        ServerMetadataOrHandle<T>::Failure(ServerMetadataFromStatus(result)));
  }

 private:
  Call* call_;
  Derived* filter_;
};

// Overrides for Filter methods with a Return type supporting a bool ok()
// method and holding a value within e.g., for absl::StatusOr<T> return types.
template <typename T, typename A, typename R, typename Call,
          R (Call::*method)(A)>
class AdaptMethod<
    T, R (Call::*)(A), method,
    absl::enable_if_t<StatusOrType<R, T>::value && IsSameExcludingCVRef<T, A>,
                      void>> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/ = nullptr) : call_(call) {}
  auto operator()(Hdl<T> x) {
    R result = (call_->*method)(*x);
    if (IsStatusOk(result)) {
      return Immediate(
          ServerMetadataOrHandle<T>::Ok(TakeValue(std::move(result))));
    }
    return Immediate(ServerMetadataOrHandle<T>::Failure(
        ServerMetadataFromStatus(std::move(result.status()))));
  }

 private:
  Call* call_;
};

template <typename T, typename R, typename Call, R (Call::*method)()>
class AdaptMethod<T, R (Call::*)(), method,
                  absl::enable_if_t<StatusOrType<R, T>::value, void>> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/ = nullptr) : call_(call) {}
  auto operator()(Hdl<T> x) {
    R result = (call_->*method)();
    if (IsStatusOk(result)) {
      return Immediate(
          ServerMetadataOrHandle<T>::Ok(TakeValue(std::move(result))));
    }
    return Immediate(ServerMetadataOrHandle<T>::Failure(
        ServerMetadataFromStatus(std::move(result.status()))));
  }

 private:
  Call* call_;
};

template <typename T, typename A, typename R, typename Call, typename Derived,
          R (Call::*method)(A, Derived*)>
class AdaptMethod<
    T, R (Call::*)(A, Derived*), method,
    absl::enable_if_t<StatusOrType<R, T>::value && IsSameExcludingCVRef<T, A>,
                      void>> {
 public:
  using UnwrappedType = decltype(TakeValue(std::declval<R>()));
  static_assert(std::is_same<UnwrappedType, T>::value);
  explicit AdaptMethod(Call* call, Derived* filter)
      : call_(call), filter_(filter) {}
  auto operator()(Hdl<T> x) {
    R result = (call_->*method)(*x, filter_);
    if (IsStatusOk(result)) {
      return Immediate(
          ServerMetadataOrHandle<T>::Ok(TakeValue(std::move(result))));
    }
    return Immediate(ServerMetadataOrHandle<T>::Failure(
        ServerMetadataFromStatus(std::move(result.status()))));
  }

 private:
  Call* call_;
  Derived* filter_;
};

// Overrides for filter methods which take a Hdl<T> type or void as input and
// return a StatusOr<Hdl<T>> type as output
template <typename T, typename R, typename Call, R (Call::*method)(Hdl<T>)>
class AdaptMethod<T, R (Call::*)(Hdl<T>), method,
                  absl::enable_if_t<StatusOrType<R, Hdl<T>>::value, void>> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/ = nullptr) : call_(call) {}
  auto operator()(Hdl<T> x) {
    R result = (call_->*method)(std::move(x));
    if (IsStatusOk(result)) {
      return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(*result)));
    }

    return Immediate(ServerMetadataOrHandle<T>::Failure(
        ServerMetadataFromStatus(result.status())));
  }

 private:
  Call* call_;
};

template <typename T, typename R, typename Call, R (Call::*method)()>
class AdaptMethod<T, R (Call::*)(), method,
                  absl::enable_if_t<StatusOrType<R, Hdl<T>>::value, void>> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/ = nullptr) : call_(call) {}
  auto operator()(Hdl<T> /*x*/) {
    R result = (call_->*method)();
    if (IsStatusOk(result)) {
      return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(*result)));
    }

    return Immediate(ServerMetadataOrHandle<T>::Failure(
        ServerMetadataFromStatus(result.status())));
  }

 private:
  Call* call_;
};

template <typename T, typename R, typename Call, typename Derived,
          R (Call::*method)(Hdl<T>, Derived*)>
class AdaptMethod<T, R (Call::*)(Hdl<T>, Derived*), method,
                  absl::enable_if_t<StatusOrType<R, Hdl<T>>::value, void>> {
 public:
  explicit AdaptMethod(Call* call, Derived* filter)
      : call_(call), filter_(filter) {}
  auto operator()(Hdl<T> x) {
    R result = (call_->*method)(std::move(x), filter_);
    if (IsStatusOk(result)) {
      return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(*result)));
    }

    return Immediate(ServerMetadataOrHandle<T>::Failure(
        ServerMetadataFromStatus(result.status())));
  }

 private:
  Call* call_;
  Derived* filter_;
};

// For methods which return a promise where the result of the promise
template <typename T, typename R, typename Call, typename Derived,
          R (Call::*method)(Hdl<T>, Derived*)>
class AdaptMethod<
    T, R (Call::*)(Hdl<T>, Derived*), method,
    std::enable_if_t<IsPromise<R> && std::is_same_v<PromiseResult<R>,
                                                    absl::StatusOr<Hdl<T>>>,
                     void>> {
 public:
  explicit AdaptMethod(Call* call, Derived* filter)
      : call_(call), filter_(filter) {}
  auto operator()(Hdl<T> x) {
    return Map((call_->*method)(std::move(x), filter_),
               [](absl::StatusOr<Hdl<T>> result) {
                 if (result.ok()) {
                   return ServerMetadataOrHandle<T>::Ok(std::move(*result));
                 }
                 return ServerMetadataOrHandle<T>::Failure(
                     ServerMetadataFromStatus(result.status()));
               });
  }

 private:
  Call* call_;
  Derived* filter_;
};

template <typename T, typename R, typename Call, typename Derived,
          R (Call::*method)(T&, Derived*)>
class AdaptMethod<
    T, R (Call::*)(T&, Derived*), method,
    std::enable_if_t<
        IsPromise<R> && std::is_same_v<absl::Status, PromiseResult<R>>, void>> {
 public:
  explicit AdaptMethod(Call* call, Derived* filter)
      : call_(call), filter_(filter) {}
  auto operator()(Hdl<T> x) {
    return Map((call_->*method)(*x, filter_),
               [hdl = std::move(x)](absl::Status status) mutable {
                 if (status.ok()) {
                   return ServerMetadataOrHandle<T>::Ok(std::move(hdl));
                 }
                 return ServerMetadataOrHandle<T>::Failure(
                     ServerMetadataFromStatus(status));
               });
  }

 private:
  Call* call_;
  Derived* filter_;
};

template <typename T, typename Call, typename Derived,
          Hdl<T> (Call::*method)(Hdl<T>, Derived*)>
class AdaptMethod<T, Hdl<T> (Call::*)(Hdl<T>, Derived*), method> {
 public:
  explicit AdaptMethod(Call* call, Derived* filter)
      : call_(call), filter_(filter) {}
  auto operator()(Hdl<T> x) {
    Hdl<T> result = (call_->*method)(std::move(x), filter_);
    return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(result)));
  }

 private:
  Call* call_;
  Derived* filter_;
};

// Overrides for filter methods which return a ServerMetadataHandle type as
// output.
template <typename T, typename A, typename Call,
          ServerMetadataHandle (Call::*method)(A)>
class AdaptMethod<T, ServerMetadataHandle (Call::*)(A), method,
                  EnableIfSameExcludingCVRef<T, A>> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/ = nullptr) : call_(call) {}
  auto operator()(Hdl<T> x) {
    ServerMetadataHandle handle = (call_->*method)(*x);
    if (handle == nullptr) {
      return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(x)));
    }

    return Immediate(ServerMetadataOrHandle<T>::Failure(std::move(handle)));
  }

 private:
  Call* call_;
};

template <typename T, typename Call, ServerMetadataHandle (Call::*method)()>
class AdaptMethod<T, ServerMetadataHandle (Call::*)(), method> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/ = nullptr) : call_(call) {}
  auto operator()(Hdl<T> x) {
    ServerMetadataHandle handle = (call_->*method)();
    if (handle == nullptr) {
      return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(x)));
    }

    return Immediate(ServerMetadataOrHandle<T>::Failure(std::move(handle)));
  }

 private:
  Call* call_;
};

template <typename T, typename A, typename Call, typename Derived,
          ServerMetadataHandle (Call::*method)(A, Derived*)>
class AdaptMethod<T, ServerMetadataHandle (Call::*)(A, Derived*), method,
                  EnableIfSameExcludingCVRef<T, A>> {
 public:
  explicit AdaptMethod(Call* call, Derived* filter)
      : call_(call), filter_(filter) {}
  auto operator()(Hdl<T> x) {
    ServerMetadataHandle handle = (call_->*method)(*x, filter_);
    if (handle == nullptr) {
      return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(x)));
    }

    return Immediate(ServerMetadataOrHandle<T>::Failure(std::move(handle)));
  }

 private:
  Call* call_;
  Derived* filter_;
};

template <typename T, typename MethodType, MethodType method,
          typename Ignored = void>
class AdaptOnServerTrailingMetadataMethod;

template <typename T, const NoInterceptor* method>
class AdaptOnServerTrailingMetadataMethod<T, const NoInterceptor*, method> {
 public:
  explicit AdaptOnServerTrailingMetadataMethod(void* /*call*/,
                                               void* /*filter*/ = nullptr) {}
  auto operator()(T& x) {}
  void operator()(T* /*x*/) {}
};

template <typename T, typename A, typename Call, typename Derived,
          void (Call::*method)(A, Derived*)>
class AdaptOnServerTrailingMetadataMethod<
    T, void (Call::*)(A, Derived*), method, EnableIfSameExcludingCVRef<T, A>> {
 public:
  explicit AdaptOnServerTrailingMetadataMethod(Call* call, Derived* filter)
      : call_(call), filter_(filter) {}
  void operator()(T& x) { (call_->*method)(x, filter_); }

 private:
  Call* call_;
  Derived* filter_;
};

template <typename T, typename A, typename Call, typename Derived,
          absl::Status (Call::*method)(A, Derived*)>
class AdaptOnServerTrailingMetadataMethod<
    T, absl::Status (Call::*)(A, Derived*), method,
    EnableIfSameExcludingCVRef<T, A>> {
 public:
  explicit AdaptOnServerTrailingMetadataMethod(Call* call, Derived* filter)
      : call_(call), filter_(filter) {}
  void operator()(T& x) {
    absl::Status status = (call_->*method)(x, filter_);
    if (!status.ok()) {
      SetServerMetadataFromStatus(x, status);
    }
  }

 private:
  Call* call_;
  Derived* filter_;
};

template <typename T, typename A, typename Call, void (Call::*method)(A)>
class AdaptOnServerTrailingMetadataMethod<T, void (Call::*)(A), method,
                                          EnableIfSameExcludingCVRef<T, A>> {
 public:
  explicit AdaptOnServerTrailingMetadataMethod(Call* call, void* /*filter*/)
      : call_(call) {}
  void operator()(T& x) { (call_->*method)(x); }

 private:
  Call* call_;
};

template <typename T, typename A, typename Call,
          absl::Status (Call::*method)(A)>
class AdaptOnServerTrailingMetadataMethod<T, absl::Status (Call::*)(A), method,
                                          EnableIfSameExcludingCVRef<T, A>> {
 public:
  explicit AdaptOnServerTrailingMetadataMethod(Call* call, void* /*filter*/)
      : call_(call) {}
  void operator()(T& x) {
    absl::Status status = (call_->*method)(x);
    if (!status.ok()) {
      SetServerMetadataFromStatus(x, status);
    }
  }

 private:
  Call* call_;
};

template <typename MethodType, MethodType method, typename Ignored = void>
class AdaptOnClientToServerHalfCloseMethod;

template <const NoInterceptor* method>
class AdaptOnClientToServerHalfCloseMethod<const NoInterceptor*, method> {
 public:
  explicit AdaptOnClientToServerHalfCloseMethod(void* /*call*/) {}
  void operator()() {}
};

template <typename Call, void (Call::*method)()>
class AdaptOnClientToServerHalfCloseMethod<void (Call::*)(), method> {
 public:
  explicit AdaptOnClientToServerHalfCloseMethod(Call* call) : call_(call) {}
  void operator()() { (call_->*method)(); }

 private:
  Call* call_;
};

template <auto... filter_methods>
struct FilterMethods {
  using Methods = Valuelist<filter_methods...>;
  using Idxs = std::make_index_sequence<sizeof...(filter_methods)>;
};

template <typename... Filters>
struct FilterTypes {
  using Types = Typelist<Filters...>;
  using Idxs = std::make_index_sequence<sizeof...(Filters)>;
};

template <std::size_t, typename FilterTypeList, typename Ignored = void>
struct TypesFromIdx;

template <typename... Filters>
struct TypesFromIdx<0, Typelist<Filters...>> {
  using Types = Typelist<Filters...>;
};

template <std::size_t N, typename Filter0, typename... Filters>
struct TypesFromIdx<N, Typelist<Filter0, Filters...>,
                    std::enable_if_t<N != 0>> {
  using Types = typename TypesFromIdx<N - 1, Typelist<Filters...>>::Types;
};

template <std::size_t, typename>
struct make_reverse_index_sequence_helper;

template <std::size_t N, std::size_t... NN>
struct make_reverse_index_sequence_helper<N, std::index_sequence<NN...>>
    : std::index_sequence<(N - NN)...> {};

template <size_t N>
struct make_reverse_index_sequence
    : make_reverse_index_sequence_helper<
          N - 1, decltype(std::make_index_sequence<N>{})> {};

template <auto... filter_methods>
struct ReverseFilterMethods {
  using Methods = ReverseValues<filter_methods...>;
  using Idxs = make_reverse_index_sequence<sizeof...(filter_methods)>;
};

template <typename... Filters>
struct ReverseFilterTypes {
  using Types = Reverse<Filters...>;
  using Idxs = make_reverse_index_sequence<sizeof...(Filters)>;
  template <bool forward, auto... filter_methods>
  struct ForwardOrReverse;
};

template <bool forward, auto... filter_methods>
struct ForwardOrReverse;

template <auto... filter_methods>
struct ForwardOrReverse<true, filter_methods...> {
  using OrderMethod = FilterMethods<filter_methods...>;
};

template <auto... filter_methods>
struct ForwardOrReverse<false, filter_methods...> {
  using OrderMethod = ReverseFilterMethods<filter_methods...>;
};

template <bool forward, typename... filter_types>
struct ForwardOrReverseTypes;

template <typename... filter_types>
struct ForwardOrReverseTypes<true, filter_types...> {
  using OrderMethod = FilterTypes<filter_types...>;
};

template <typename... filter_types>
struct ForwardOrReverseTypes<false, filter_types...> {
  using OrderMethod = ReverseFilterTypes<filter_types...>;
};

// Combine the result of a series of filter methods into a single method.
template <typename Call, typename T, auto filter_method_0,
          auto... filter_methods, size_t I0, size_t... Is>
auto ExecuteCombined(Call* call, Hdl<T> hdl,
                     Valuelist<filter_method_0, filter_methods...>,
                     std::index_sequence<I0, Is...>) {
  return TrySeq(AdaptMethod<T, decltype(filter_method_0), filter_method_0>(
                    call->template fused_child<I0>())(std::move(hdl)),
                AdaptMethod<T, decltype(filter_methods), filter_methods>(
                    call->template fused_child<Is>())...);
}

template <typename FilterMethods, typename Call, typename T>
auto ExecuteCombined(Call* call, Hdl<T> hdl) {
  return ExecuteCombined(call, std::move(hdl),
                         typename FilterMethods::Methods(),
                         typename FilterMethods::Idxs());
}

// Combine the result of a series of filter methods into a single method.
template <typename Call, typename Derived, typename T, typename Filter0,
          typename... Filters, auto filter_method_0, auto... filter_methods,
          size_t I0, size_t... Is>
auto ExecuteCombinedWithChannelAccess(
    Call* call, Derived* channel, Hdl<T> hdl, Typelist<Filter0, Filters...>,
    Valuelist<filter_method_0, filter_methods...>,
    std::index_sequence<I0, Is...>) {
  return TrySeq(
      AdaptMethod<T, decltype(filter_method_0), filter_method_0>(
          call->template fused_child<I0>(),
          reinterpret_cast<Filter0*>(channel->template get_fused_filter<I0>()))(
          std::move(hdl)),
      AdaptMethod<T, decltype(filter_methods), filter_methods>(
          call->template fused_child<Is>(),
          reinterpret_cast<Filters*>(
              channel->template get_fused_filter<Is>()))...);
}

template <typename FilterMethods, typename FilterTypes, typename Call,
          typename Derived, typename T>
auto ExecuteCombinedWithChannelAccess(Call* call, Derived* channel,
                                      Hdl<T> hdl) {
  return ExecuteCombinedWithChannelAccess(
      call, channel, std::move(hdl), typename FilterTypes::Types(),
      typename FilterMethods::Methods(), typename FilterMethods::Idxs());
}

// Combine the result of a series of OnFinalize filter methods into a single
// method.
template <typename Call, typename Derived, typename T, typename... Filters,
          auto... filter_methods, size_t... Is>
void ExecuteCombinedOnFinalizeWithChannelAccess(Call* call, Derived* channel,
                                                T* call_final_info,
                                                Typelist<Filters...>,
                                                Valuelist<filter_methods...>,
                                                std::index_sequence<Is...>) {
  (AdaptMethod<T, decltype(filter_methods), filter_methods>(
       call->template fused_child<Is>(),
       reinterpret_cast<Filters*>(channel->template get_fused_filter<Is>()))(
       call_final_info),
   ...);
}

template <typename Call, typename T, auto... filter_methods, size_t... Is>
void ExecuteCombinedOnFinalize(Call* call, T* call_final_info,
                               Valuelist<filter_methods...>,
                               std::index_sequence<Is...>) {
  (AdaptMethod<T, decltype(filter_methods), filter_methods>(
       call->template fused_child<Is>(), nullptr)(call_final_info),
   ...);
}

// Combine the result of a series of OnServerTrailingMetadata filter methods
// into a single method.
template <typename Call, typename Derived, typename T, typename... Filters,
          auto... filter_methods, size_t... Is>
void ExecuteCombinedOnServerTrailingMetadataWithChannelAccess(
    Call* call, Derived* channel, T& metadata, Typelist<Filters...>,
    Valuelist<filter_methods...>, std::index_sequence<Is...>) {
  (AdaptOnServerTrailingMetadataMethod<T, decltype(filter_methods),
                                       filter_methods>(
       call->template fused_child<Is>(),
       reinterpret_cast<Filters*>(channel->template get_fused_filter<Is>()))(
       metadata),
   ...);
}

template <typename FilterMethods, typename FilterTypes, typename Call,
          typename Derived, typename T>
void ExecuteCombinedOnServerTrailingMetadataWithChannelAccess(Call* call,
                                                              Derived* channel,
                                                              T& metadata) {
  ExecuteCombinedOnServerTrailingMetadataWithChannelAccess(
      call, channel, metadata, typename FilterTypes::Types(),
      typename FilterMethods::Methods(), typename FilterMethods::Idxs());
}

template <typename Call, typename T, auto... filter_methods, size_t... Is>
void ExecuteCombinedOnServerTrailingMetadata(Call* call, T& metadata,
                                             Valuelist<filter_methods...>,
                                             std::index_sequence<Is...>) {
  (AdaptOnServerTrailingMetadataMethod<T, decltype(filter_methods),
                                       filter_methods>(
       call->template fused_child<Is>(), nullptr)(metadata),
   ...);
}

template <typename FilterMethods, typename Call, typename T>
void ExecuteCombinedOnServerTrailingMetadata(Call* call, T& metadata) {
  ExecuteCombinedOnServerTrailingMetadata(call, metadata,
                                          typename FilterMethods::Methods(),
                                          typename FilterMethods::Idxs());
}

template <typename Call, auto... filter_methods, size_t... Is>
void ExecuteCombinedOnClientToServerHalfClose(Call* call,
                                              Valuelist<filter_methods...>,
                                              std::index_sequence<Is...>) {
  (AdaptOnClientToServerHalfCloseMethod<decltype(filter_methods),
                                        filter_methods>(
       call->template fused_child<Is>())(),
   ...);
}

template <typename FilterMethods, typename Call>
void ExecuteCombinedOnClientToServerHalfClose(Call* call) {
  ExecuteCombinedOnClientToServerHalfClose(
      call, typename FilterMethods::Methods(), typename FilterMethods::Idxs());
}

template <typename FilterMethods, typename FilterTypes, typename Call,
          typename Derived, typename T>
void ExecuteCombinedWithChannelAccess(Call* call, Derived* channel,
                                      T* call_final_info) {
  ExecuteCombinedOnFinalizeWithChannelAccess(
      call, channel, call_final_info, typename FilterTypes::Types(),
      typename FilterMethods::Methods(), typename FilterMethods::Idxs());
}

template <typename FilterMethods, typename Call, typename T>
void ExecuteCombined(Call* call, T* call_final_info) {
  ExecuteCombinedOnFinalize(call, call_final_info,
                            typename FilterMethods::Methods(),
                            typename FilterMethods::Idxs());
}

#define GRPC_FUSE_METHOD(name, type, forward)                                 \
  template <MethodVariant variant, typename Derived, typename... Filters>     \
  class FuseImpl##name;                                                       \
  template <typename Derived, typename... Filters>                            \
  class FuseImpl##name<MethodVariant::kNoInterceptor, Derived, Filters...> {  \
   public:                                                                    \
    static inline const NoInterceptor name;                                   \
  };                                                                          \
  template <typename Derived, typename... Filters>                            \
  class FuseImpl##name<MethodVariant::kSimple, Derived, Filters...> {         \
   public:                                                                    \
    auto name(type x) {                                                       \
      return ExecuteCombined<typename ForwardOrReverse<                       \
          forward, &Filters::Call::name...>::OrderMethod>(                    \
          static_cast<typename Derived::Call*>(this), std::move(x));          \
    }                                                                         \
  };                                                                          \
  template <typename Derived, typename... Filters>                            \
  class FuseImpl##name<MethodVariant::kChannelAccess, Derived, Filters...> {  \
   public:                                                                    \
    auto name(type x, Derived* channel) {                                     \
      return ExecuteCombinedWithChannelAccess<                                \
          typename ForwardOrReverse<forward,                                  \
                                    &Filters::Call::name...>::OrderMethod,    \
          typename ForwardOrReverseTypes<forward, Filters...>::OrderMethod>(  \
          static_cast<typename Derived::Call*>(this), channel, std::move(x)); \
    }                                                                         \
  };                                                                          \
  template <typename Derived, typename... Filters>                            \
  using Fuse##name = FuseImpl##name<                                          \
      MethodVariantForFilters<decltype(&Filters::Call::name)...>(), Derived,  \
      Filters...>

template <MethodVariant variant, typename Derived, typename... Filters>
class FuseImplOnServerTrailingMetadata;

template <typename Derived, typename... Filters>
class FuseImplOnServerTrailingMetadata<MethodVariant::kNoInterceptor, Derived,
                                       Filters...> {
 public:
  static inline const NoInterceptor OnServerTrailingMetadata;
};

template <typename Derived, typename... Filters>
class FuseImplOnServerTrailingMetadata<MethodVariant::kSimple, Derived,
                                       Filters...> {
 public:
  void OnServerTrailingMetadata(ServerMetadata& x) {
    return ExecuteCombinedOnServerTrailingMetadata<typename ForwardOrReverse<
        false, &Filters::Call::OnServerTrailingMetadata...>::OrderMethod>(
        static_cast<typename Derived::Call*>(this), x);
  }
};

template <typename Derived, typename... Filters>
class FuseImplOnServerTrailingMetadata<MethodVariant::kChannelAccess, Derived,
                                       Filters...> {
 public:
  void OnServerTrailingMetadata(ServerMetadata& x, Derived* channel) {
    return ExecuteCombinedOnServerTrailingMetadataWithChannelAccess<
        typename ForwardOrReverse<
            false, &Filters::Call::OnServerTrailingMetadata...>::OrderMethod,
        typename ForwardOrReverseTypes<false, Filters...>::OrderMethod>(
        static_cast<typename Derived::Call*>(this), channel, x);
  }
};

template <typename Derived, typename... Filters>
using FuseOnServerTrailingMetadata = FuseImplOnServerTrailingMetadata<
    MethodVariantForFilters<
        decltype(&Filters::Call::OnServerTrailingMetadata)...>(),
    Derived, Filters...>;

template <bool is_no_interceptor, typename Derived, typename... Filters>
class FuseImplOnClientToServerHalfClose;

template <typename Derived, typename... Filters>
class FuseImplOnClientToServerHalfClose<true, Derived, Filters...> {
 public:
  static inline const NoInterceptor OnClientToServerHalfClose;
};

template <typename Derived, typename... Filters>
class FuseImplOnClientToServerHalfClose<false, Derived, Filters...> {
 public:
  void OnClientToServerHalfClose() {
    return ExecuteCombinedOnClientToServerHalfClose<typename ForwardOrReverse<
        true, &Filters::Call::OnClientToServerHalfClose...>::OrderMethod>(
        static_cast<typename Derived::Call*>(this));
  }
};

template <typename Derived, typename... Filters>
using FuseOnClientToServerHalfClose = FuseImplOnClientToServerHalfClose<
    AllNoInterceptor<decltype(&Filters::Call::OnClientToServerHalfClose)...>,
    Derived, Filters...>;

GRPC_FUSE_METHOD(OnClientInitialMetadata, ClientMetadataHandle, true);
GRPC_FUSE_METHOD(OnServerInitialMetadata, ServerMetadataHandle, false);
GRPC_FUSE_METHOD(OnClientToServerMessage, MessageHandle, true);
GRPC_FUSE_METHOD(OnServerToClientMessage, MessageHandle, false);
GRPC_FUSE_METHOD(OnFinalize, const grpc_call_final_info*, true);

template <typename FilterTypelist>
struct FilterWrapper;

template <typename Filter>
struct FilterWrapper<Typelist<Filter>> {
  FilterWrapper(const ChannelArgs& args, ChannelFilter::Args filter_args)
      : filter_(Filter::Create(args, filter_args)) {}

  absl::Status status() const { return filter_.status(); }
  bool StartTransportOp(grpc_transport_op* op) {
    CHECK(filter_.ok());
    return (*filter_)->StartTransportOp(op);
  }

  Filter* get_filter() { return (*filter_).get(); }

  bool GetChannelInfo(const grpc_channel_info* info) {
    CHECK(filter_.ok());
    return (*filter_)->GetChannelInfo(info);
  }

 private:
  absl::StatusOr<std::unique_ptr<Filter>> filter_;
};

template <typename Filter0, typename... Filters>
struct FilterWrapper<Typelist<Filter0, Filters...>>
    : public FilterWrapper<Typelist<Filters...>> {
  FilterWrapper(const ChannelArgs& args, ChannelFilter::Args filter_args)
      : filter0_(Filter0::Create(args, filter_args)),
        FilterWrapper<Typelist<Filters...>>(args, filter_args) {}

  absl::Status status() const {
    if (filter0_.ok()) {
      return FilterWrapper<Typelist<Filters...>>::status();
    }
    return filter0_.status();
  }

  Filter0* get_filter() { return (*filter0_).get(); }

  bool StartTransportOp(grpc_transport_op* op) {
    CHECK(filter0_.ok());
    return (*filter0_)->StartTransportOp(op) ||
           FilterWrapper<Typelist<Filters...>>::StartTransportOp(op);
  }

  bool GetChannelInfo(const grpc_channel_info* info) {
    CHECK(filter0_.ok());
    return (*filter0_)->GetChannelInfo(info) ||
           FilterWrapper<Typelist<Filters...>>::GetChannelInfo(info);
  }

 private:
  absl::StatusOr<std::unique_ptr<Filter0>> filter0_;
};

template <typename Derived, typename SfinaeVoid = void>
struct ConstructCall;

template <typename Derived>
struct ConstructCall<Derived, absl::void_t<decltype(typename Derived::Call(
                                  std::declval<Derived*>()))>> {
 public:
  using Call = typename Derived::Call;
  static void Run(ManualConstructor<Call>& call, Derived* channel) {
    call.Init(channel);
  }
};

template <typename Derived>
struct ConstructCall<Derived,
                     absl::void_t<decltype(typename Derived::Call())>> {
 public:
  using Call = typename Derived::Call;
  static void Run(ManualConstructor<Call>& call, Derived* /*channel*/) {
    call.Init();
  }
};

template <typename FilterTypelist>
struct CallWrapper;

template <typename Filter>
struct CallWrapper<Typelist<Filter>> {
  using Call = typename Filter::Call;
  explicit CallWrapper(FilterWrapper<Typelist<Filter>>* channel) {
    ConstructCall<Filter>::Run(call_, channel->get_filter());
  }

  Call* get_call() { return call_.get(); }

  ~CallWrapper() { call_.Destroy(); }

 private:
  ManualConstructor<Call> call_;
};

template <typename Filter0, typename... Filters>
struct CallWrapper<Typelist<Filter0, Filters...>>
    : public CallWrapper<Typelist<Filters...>> {
  using Call = typename Filter0::Call;
  explicit CallWrapper(FilterWrapper<Typelist<Filter0, Filters...>>* channel)
      : CallWrapper<Typelist<Filters...>>(
            reinterpret_cast<FilterWrapper<Typelist<Filters...>>*>(channel)) {
    ConstructCall<Filter0>::Run(call_, channel->get_filter());
  }

  Call* get_call() { return call_.get(); }

  ~CallWrapper() { call_.Destroy(); }

 private:
  ManualConstructor<Call> call_;
};

#undef GRPC_FUSE_METHOD

template <FilterEndpoint ep, uint8_t kFlags, typename... Filters>
class FusedFilter
    : public ImplementChannelFilter<FusedFilter<ep, kFlags, Filters...>> {
 public:
  static const grpc_channel_filter kFilter;

  static absl::string_view TypeName() {
    static const std::string kName = absl::StrCat(
        absl::StrJoin(std::make_tuple(Filters::TypeName()...), "+"));
    return kName;
  }

  template <size_t I>
  auto* get_fused_filter() {
    return reinterpret_cast<FilterWrapper<
        typename TypesFromIdx<I, Typelist<Filters...>>::Types>*>(filters_.get())
        ->get_filter();
  }

  FilterWrapper<Typelist<Filters...>>* get_wrapper() { return filters_.get(); }

  static absl::StatusOr<std::unique_ptr<FusedFilter<ep, kFlags, Filters...>>>
  Create(const ChannelArgs& args, ChannelFilter::Args filter_args) {
    auto filters_wrapper =
        std::make_unique<FilterWrapper<Typelist<Filters...>>>(args,
                                                              filter_args);
    GRPC_RETURN_IF_ERROR(filters_wrapper->status());
    auto res = absl::WrapUnique<FusedFilter<ep, kFlags, Filters...>>(
        new FusedFilter<ep, kFlags, Filters...>(std::move(filters_wrapper)));
    return res;
  }

  static constexpr bool IsFused = true;

  static constexpr bool FusedFilterHasAsyncErrorInterceptor() {
    return (
        promise_filter_detail::CallHasAsyncErrorInterceptor<Filters>::value ||
        ...);
  }

  class Call : public FuseOnClientInitialMetadata<FusedFilter, Filters...>,
               public FuseOnServerInitialMetadata<FusedFilter, Filters...>,
               public FuseOnClientToServerMessage<FusedFilter, Filters...>,
               public FuseOnServerToClientMessage<FusedFilter, Filters...>,
               public FuseOnServerTrailingMetadata<FusedFilter, Filters...>,
               public FuseOnClientToServerHalfClose<FusedFilter, Filters...>,
               public FuseOnFinalize<FusedFilter, Filters...> {
   public:
    using Idxs = std::make_index_sequence<sizeof...(Filters)>;
    template <size_t I>
    auto* fused_child() {
      return reinterpret_cast<CallWrapper<
          typename TypesFromIdx<I, Typelist<Filters...>>::Types>*>(
                 &filter_calls_)
          ->get_call();
    }

    explicit Call(FusedFilter<ep, kFlags, Filters...>* filter)
        : filter_calls_(filter->get_wrapper()) {};

    using FuseOnClientInitialMetadata<FusedFilter,
                                      Filters...>::OnClientInitialMetadata;
    using FuseOnServerInitialMetadata<FusedFilter,
                                      Filters...>::OnServerInitialMetadata;
    using FuseOnClientToServerMessage<FusedFilter,
                                      Filters...>::OnClientToServerMessage;
    using FuseOnServerToClientMessage<FusedFilter,
                                      Filters...>::OnServerToClientMessage;
    using FuseOnServerTrailingMetadata<FusedFilter,
                                       Filters...>::OnServerTrailingMetadata;
    using FuseOnClientToServerHalfClose<FusedFilter,
                                        Filters...>::OnClientToServerHalfClose;
    using FuseOnFinalize<FusedFilter, Filters...>::OnFinalize;

   private:
    CallWrapper<Typelist<Filters...>> filter_calls_;
  };

  bool StartTransportOp(grpc_transport_op* op) override {
    return filters_->StartTransportOp(op);
  }

  bool GetChannelInfo(const grpc_channel_info* info) override {
    return filters_->GetChannelInfo(info);
  }

 private:
  explicit FusedFilter(
      std::unique_ptr<FilterWrapper<Typelist<Filters...>>> filters)
      : filters_(std::move(filters)) {};

  std::unique_ptr<FilterWrapper<Typelist<Filters...>>> filters_;
};

template <FilterEndpoint ep, uint8_t kFlags, typename... Filters>
const grpc_channel_filter FusedFilter<ep, kFlags, Filters...>::kFilter =
    MakePromiseBasedFilter<FusedFilter<ep, kFlags, Filters...>, ep, kFlags>();

}  // namespace filters_detail

template <FilterEndpoint ep, uint8_t kFlags, typename... Filters>
using FusedFilter = filters_detail::FusedFilter<ep, kFlags, Filters...>;

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CALL_FILTER_FUSION_H
