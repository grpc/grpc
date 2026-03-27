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

#ifndef GRPC_SRC_CORE_LIB_RESOURCE_QUOTA_TELEMETRY_H
#define GRPC_SRC_CORE_LIB_RESOURCE_QUOTA_TELEMETRY_H

#include "src/core/telemetry/instrument.h"

namespace grpc_core {

class ResourceQuotaDomain final : public InstrumentDomain<ResourceQuotaDomain> {
 public:
  GRPC_INSTRUMENT_DOMAIN_LABELS("grpc.resource_quota");
  using Backend = HighContentionBackend;
  static constexpr absl::string_view kName = "resource_quota";

  // Use function-local statics (Meyers singletons) instead of static inline
  // data members to ensure lazy initialization on first use.  This avoids
  // duplicate metric registration when the header is transitively included by
  // translation units compiled into different shared libraries (e.g. libgrpc
  // and libgrpc++), where each DSO's .init_array would otherwise run its own
  // copy of the static inline initializers.
  static const auto& NumCallsDropped() {
    static const auto handle = RegisterCounter(
        "grpc.resource_quota.calls_dropped",
        "EXPERIMENTAL.  Number of calls dropped due to resource quota "
        "exceeded",
        "calls");
    return handle;
  }
  static const auto& NumCallsRejected() {
    static const auto handle = RegisterCounter(
        "grpc.resource_quota.calls_rejected",
        "EXPERIMENTAL.  Number of calls rejected due to resource quota "
        "exceeded",
        "calls");
    return handle;
  }
  static const auto& NumConnectionsDropped() {
    static const auto handle = RegisterCounter(
        "grpc.resource_quota.connections_dropped",
        "EXPERIMENTAL.  Number of connections dropped due to resource quota "
        "exceeded",
        "connections");
    return handle;
  }
  static const auto& DblInstantaneousMemoryPressure() {
    static const auto handle = RegisterDoubleGauge(
        "grpc.resource_quota.instantaneous_memory_pressure",
        "The current instantaneously measured memory pressure.", "ratio");
    return handle;
  }
  static const auto& DblMemoryPressureControlValue() {
    static const auto handle = RegisterDoubleGauge(
        "grpc.resource_quota.memory_pressure_control_value",
        "A control value that can be used to scale buffer sizes up or down to "
        "adjust memory pressure to our target set point.",
        "ratio");
    return handle;
  }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_RESOURCE_QUOTA_TELEMETRY_H
