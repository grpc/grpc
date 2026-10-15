// Copyright 2026 gRPC authors.
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

#include "src/core/lib/resource_quota/telemetry.h"

#include <grpc/support/port_platform.h>  // IWYU pragma: keep

#include "src/core/telemetry/instrument.h"

namespace grpc_core {

// Telemetry domain static handle definitions and registrations (Dynamic on
// Load)
ResourceQuotaDomain::CounterHandle ResourceQuotaDomain::kCallsDropped =
    ResourceQuotaDomain::RegisterCounter(
        "grpc.resource_quota.calls_dropped",
        "EXPERIMENTAL.  Number of calls dropped due to resource quota exceeded",
        "calls");
ResourceQuotaDomain::CounterHandle ResourceQuotaDomain::kCallsRejected =
    ResourceQuotaDomain::RegisterCounter(
        "grpc.resource_quota.calls_rejected",
        "EXPERIMENTAL.  Number of calls rejected due to resource quota "
        "exceeded",
        "calls");
ResourceQuotaDomain::CounterHandle ResourceQuotaDomain::kConnectionsDropped =
    ResourceQuotaDomain::RegisterCounter(
        "grpc.resource_quota.connections_dropped",
        "EXPERIMENTAL.  Number of connections dropped due to resource quota "
        "exceeded",
        "connections");
ResourceQuotaDomain::DoubleGaugeHandle
    ResourceQuotaDomain::kInstantaneousMemoryPressure =
        ResourceQuotaDomain::RegisterDoubleGauge(
            "grpc.resource_quota.instantaneous_memory_pressure",
            "The current instantaneously measured memory pressure.", "ratio");
ResourceQuotaDomain::DoubleGaugeHandle ResourceQuotaDomain::
    kMemoryPressureControlValue = ResourceQuotaDomain::RegisterDoubleGauge(
        "grpc.resource_quota.memory_pressure_control_value",
        "A control value that can be used to scale buffer sizes up or down to "
        "adjust memory pressure to our target set point.",
        "ratio");

}  // namespace grpc_core
