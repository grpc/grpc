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

// Combine the result of a series of filter methods into a single method.
template <typename Call, typename T, auto... filter_methods>
class Executor;

template <typename Call, typename T, auto filter_method_0,
          auto... filter_methods>
class Executor<Call, T, filter_method_0, filter_methods...> {
 public:
  auto ExecuteCombined(Call* call, Hdl<T> hdl) {
    return ExecuteCombined(
        call, std::move(hdl),
        std::index_sequence_for<decltype(filter_methods)...>());
  }

 private:
  template <size_t... I>
  auto ExecuteCombined(Call* call, Hdl<T> hdl, std::index_sequence<I...>) {
    return TrySeq(AdaptMethod<T, decltype(filter_method_0), filter_method_0>(
                      call->template fused_child<0>())(std::move(hdl)),
                  AdaptMethod<T, decltype(filter_methods), filter_methods>(
                      call->template fused_child<I + 1>())(std::move(hdl))...);
  }
};

}  // namespace filters_detail
}  // namespace grpc_core

#endif
