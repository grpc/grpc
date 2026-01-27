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

#ifndef GRPCPP_IMPL_ENABLED_CALL_OPTIONS_H
#define GRPCPP_IMPL_ENABLED_CALL_OPTIONS_H

#include <string_view>

namespace grpc {
namespace impl {

struct TelemetryLabel {
  std::string_view value;
};

template <typename T>
constexpr bool kIsEnabled = false;
template <>
constexpr bool kIsEnabled<TelemetryLabel> = true;

}  // namespace impl
}  // namespace grpc

#endif  // GRPCPP_IMPL_ENABLED_CALL_OPTIONS_H
