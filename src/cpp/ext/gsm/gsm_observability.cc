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

#include "src/cpp/ext/gsm/gsm_observability.h"

#include "absl/status/status.h"

#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

//
// GsmCustomObservabilityBuilder
//

GsmCustomObservabilityBuilder& GsmCustomObservabilityBuilder::SetMeterProvider(
    std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider>
        meter_provider) {
  builder_.SetMeterProvider(meter_provider);
  return *this;
}

GsmCustomObservabilityBuilder& GsmCustomObservabilityBuilder::EnableMetrics(
    const absl::flat_hash_set<absl::string_view>& metric_names) {
  builder_.EnableMetrics(metric_names);
  return *this;
}

GsmCustomObservabilityBuilder& GsmCustomObservabilityBuilder::DisableMetrics(
    const absl::flat_hash_set<absl::string_view>& metric_names) {
  builder_.DisableMetrics(metric_names);
  return *this;
}

absl::StatusOr<GsmObservability>
GsmCustomObservabilityBuilder::BuildAndRegister() {
  return absl::UnimplementedError("Not Implemented");
}

}  // namespace internal
}  // namespace grpc