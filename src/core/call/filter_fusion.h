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

#include <utility>

#include "src/core/lib/transport/call_filters.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/util/type_list.h"

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

template <auto... Ts>
constexpr MethodVariant MethodVariantForFilters() {
  if constexpr (AllNoInterceptor<decltype(Ts)...>) {
    return MethodVariant::kNoInterceptor;
  } else if constexpr (!AnyMethodHasChannelAccess<Ts...>) {
    return MethodVariant::kSimple;
  } else {
    return MethodVariant::kChannelAccess;
  }
}

template <typename T>
using Hdl = Arena::PoolPtr<T>;

template <typename T, typename MethodType, MethodType method>
class AdaptMethod;

template <typename T, const NoInterceptor* method>
class AdaptMethod<T, const NoInterceptor*, method> {
 public:
  explicit AdaptMethod(void* /*call*/, void* /*filter*/ = nullptr) {}
  auto operator()(Hdl<T> x) {
    return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(x)));
  }
};

template <typename T, typename Call, void (Call::*method)(T&)>
class AdaptMethod<T, void (Call::*)(T&), method> {
 public:
  explicit AdaptMethod(Call* call, void* /*filter*/ = nullptr) : call_(call) {}
  auto operator()(Hdl<T> x) {
    (call_->*method)(*x);
    return Immediate(ServerMetadataOrHandle<T>::Ok(std::move(x)));
  }

 private:
  Call* call_;
};

template <auto... filter_methods>
struct FilterMethods {
  using Methods = Valuelist<filter_methods...>;
  using Idxs = std::make_index_sequence<sizeof...(filter_methods)>;
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
template <typename Call, typename T, auto filter_method_0,
          auto... filter_methods, size_t I0, size_t... Is>
auto ExecuteCombinedWithChannelAccess(
    Call* call, Hdl<T> hdl, Valuelist<filter_method_0, filter_methods...>,
    std::index_sequence<I0, Is...>) {
  return TrySeq(AdaptMethod<T, decltype(filter_method_0), filter_method_0>(
                    call->template fused_child<I0>())(std::move(hdl)),
                AdaptMethod<T, decltype(filter_methods), filter_methods>(
                    call->template fused_child<Is>())...);
}

template <typename FilterMethods, typename Call, typename T>
auto ExecuteCombinedWithChannelAccess(Call* call, Hdl<T> hdl) {
  return ExecuteCombined(call, std::move(hdl), FilterMethods::Methods(),
                         FilterMethods::Idxs());
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
      return ExecuteCombinedWithChannelAccess<typename ForwardOrReverse<      \
          forward, &Filters::Call::name...>::OrderMethod>(                    \
          static_cast<typename Derived::Call*>(this), channel, std::move(x)); \
    }                                                                         \
  };                                                                          \
  template <typename Derived, typename... Filters>                            \
  using Fuse##name =                                                          \
      FuseImpl##name<MethodVariantForFilters<&Filters::Call::name...>(),      \
                     Derived, Filters...>

GRPC_FUSE_METHOD(OnClientInitialMetadata, ClientMetadataHandle, true);
GRPC_FUSE_METHOD(OnServerInitialMetadata, ServerMetadataHandle, false);
GRPC_FUSE_METHOD(OnClientToServerMessage, MessageHandle, true);
GRPC_FUSE_METHOD(OnServerToClientMessage, MessageHandle, false);

#undef GRPC_FUSE_METHOD

template <typename... Filters>
class FusedFilter {
 public:
  class Call : public FuseOnClientInitialMetadata<FusedFilter, Filters...>,
               public FuseOnServerInitialMetadata<FusedFilter, Filters...>,
               public FuseOnClientToServerMessage<FusedFilter, Filters...>,
               public FuseOnServerToClientMessage<FusedFilter, Filters...> {
   public:
    template <size_t I>
    auto* fused_child() {
      return &std::get<I>(filters_);
    }

    using FuseOnClientInitialMetadata<FusedFilter,
                                      Filters...>::OnClientInitialMetadata;
    using FuseOnServerInitialMetadata<FusedFilter,
                                      Filters...>::OnServerInitialMetadata;
    using FuseOnClientToServerMessage<FusedFilter,
                                      Filters...>::OnClientToServerMessage;
    using FuseOnServerToClientMessage<FusedFilter,
                                      Filters...>::OnServerToClientMessage;

   private:
    std::tuple<typename Filters::Call...> filters_;
  };
};

}  // namespace filters_detail

template <typename... Filters>
using FusedFilter = filters_detail::FusedFilter<Filters...>;

}  // namespace grpc_core

#endif
