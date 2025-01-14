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

template <typename T>
using Hdl = Arena::PoolPtr<T>;

template <typename T, typename MethodType, MethodType method>
class AdaptMethod;

template <typename T, const NoInterceptor* method>
class AdaptMethod<T, const NoInterceptor*, method> {
 public:
  explicit AdaptMethod(void* /*call*/, void* /*filter*/ = nullptr) {}
  auto operator()(Hdl<T> x) {
    return ServerMetadataOrHandle<T>::Ok(std::move(x));
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
  using Filters = Valuelist<filter_methods...>;
  using Idxs = std::index_sequence_for<decltype(filter_methods)...>;
};

template <auto... filter_methods>
struct ReverseFilterMethods {
  using Filters = ReverseValues<Valuelist<filter_methods...>>;
};

// Combine the result of a series of filter methods into a single method.
template <typename Call, typename T, typename FilterMethods>
class Executor {
 public:
  static auto ExecuteCombined(Call* call, Hdl<T> hdl) {
    return TrySeq(AdaptMethod<T, decltype(filter_method_0), filter_method_0>(
                      call->template fused_child<0>())(std::move(hdl)),
                  AdaptMethod<T, decltype(filter_methods), filter_methods>(
                      call->template fused_child<I + 1>())(std::move(hdl))...);
  }

 private:
};

template <typename Derived, typename... Filters>
class FuseOnClientInitialMetadata {
 public:
  auto OnClientInitialMetadata(ClientMetadataHandle md) {
    return Executor<Derived, ClientMetadata, ValueList<&Filters::Call::OnClientInitialMetadata...>>::
        ExecuteCombined(static_cast<Derived*>(this), std::move(md));
  }
};

template <typename Derived, typename... Filters>
class FuseOnServerToClientMetadata {
 public:
  auto OnServerToClientMetadata(ServerMetadataHandle md) {
    return ReverseExecutor<Derived, ServerMetadata, &Filters::Call::OnServerToClientMetadata...>::
        ExecuteCombined(static_cast<Derived*>(this), std::move(md));
  }
};

template <typename Derived, typename... Filters>
class FuseOnClientToServerMessage {
 public:
  auto OnClientToServerMessage(MessageHandle md) {
    return Executor<Derived, ClientMetadata, &Filters::Call::OnClientInitialMetadata...>::
        ExecuteCombined(static_cast<Derived*>(this), std::move(md));
  }
};

template <typename Derived, typename... Filters>
class FuseOnServerToClientMessage {
 public:
  auto OnServerToClientMessage(MessageHandle md) {
    return ReverseExecutor<Derived, ClientMetadata, &Filters::Call::OnClientInitialMetadata...>::
        ExecuteCombined(static_cast<Derived*>(this), std::move(md));
  }
};

template <typename... Filters>
class FusedFilter {
 public:
  class Call : public FuseOnClientInitialMetadata<Call, Filters...>,
               public FuseOnClientToServerMessage<Call, Filters...>,
               public FuseOnServerToClientMessage<Call, Filters...> {
   public:
    template <size_t I>
    auto* fused_child() {
      return std::get<I>(filters_);
    }

   private:
    std::tuple<Filters::Call...> filters_;
  };
};

}  // namespace filters_detail
}  // namespace grpc_core

#endif
