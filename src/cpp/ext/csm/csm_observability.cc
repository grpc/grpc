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

#include <grpc/support/port_platform.h>

#include "src/cpp/ext/csm/csm_observability.h"

#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

//
// CsmObservabilityBuilder
//

CsmObservabilityBuilder& CsmObservabilityBuilder::SetMeterProvider(
    std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider>
        meter_provider) {
  builder_.SetMeterProvider(meter_provider);
  return *this;
}

CsmObservabilityBuilder& CsmObservabilityBuilder::EnableMetric(
    absl::string_view metric_name) {
  builder_.EnableMetric(metric_name);
  return *this;
}

CsmObservabilityBuilder& CsmObservabilityBuilder::DisableMetric(
    absl::string_view metric_name) {
  builder_.DisableMetric(metric_name);
  return *this;
}

CsmObservabilityBuilder& CsmObservabilityBuilder::DisableAllMetrics() {
  builder_.DisableAllMetrics();
  return *this;
}

absl::StatusOr<CsmObservability> CsmObservabilityBuilder::BuildAndRegister() {
  builder_.BuildAndRegisterGlobal();
  return CsmObservability();
}

}  // namespace internal
}  // namespace grpc
