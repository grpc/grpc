/*
 *
 * Copyright 2020 The gRPC Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/lib/transport/metadata_batch.h"

using grpc_core::internal::ClientChannelMethodParsedConfig;

namespace grpc_core {
namespace internal {

class FaultInjectionData {
 public:
  FaultInjectionData();
  ~FaultInjectionData();

  void SetFaultInjectionPolicy(
      const ClientChannelMethodParsedConfig::FaultInjectionPolicy*
          fault_injection_policy) {
    fault_injection_policy_ = *fault_injection_policy;
  }
  void UpdateByMetadata(grpc_metadata_batch* initial_metadata);

  grpc_error* MaybeAbort();

 private:
  bool BeginFaultInjection();
  bool EndFaultInjection();
  bool fault_injected_;
  bool injection_finished_;
  ClientChannelMethodParsedConfig::FaultInjectionPolicy fault_injection_policy_;
};

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_H */
