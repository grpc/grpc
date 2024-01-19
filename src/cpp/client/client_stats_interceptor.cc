//
//
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
//
//

#include <grpcpp/support/client_interceptor.h>

#include "src/core/lib/gprpp/crash.h"

namespace grpc {
namespace internal {

experimental::ClientInterceptorFactoryInterface*
    g_global_client_stats_interceptor_factory = nullptr;

void RegisterGlobalClientStatsInterceptorFactory(
    grpc::experimental::ClientInterceptorFactoryInterface* factory) {
  if (internal::g_global_client_stats_interceptor_factory != nullptr) {
    grpc_core::Crash(
        "It is illegal to call RegisterGlobalClientStatsInterceptorFactory "
        "multiple times.");
  }
  internal::g_global_client_interceptor_factory = factory;
}

}  // namespace internal
}  // namespace grpc
