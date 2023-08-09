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

#ifndef GRPC_SRC_CPP_EXT_GSM_GSM_OBSERVABILITY_H
#define GRPC_SRC_CPP_EXT_GSM_GSM_OBSERVABILITY_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

class GsmObservability {};

class GsmCustomObservabilityBuilder {
 public:
  // TODO(yashykt): Should this take the SDK or the API MeterProvider? Benefit
  // of SDK MeterProvider - Can explicitly set histogram bucket boundaries, but
  // in the next iteration of the API, we would have it there as well.
  GsmCustomObservabilityBuilder& SetMeterProvider(
      std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider>
          meter_provider);
  // Enable metrics in \a metric_names
  GsmCustomObservabilityBuilder& EnableMetrics(
      const absl::flat_hash_set<absl::string_view>& metric_names);
  // Disable metrics in \a metric_names
  GsmCustomObservabilityBuilder& DisableMetrics(
      const absl::flat_hash_set<absl::string_view>& metric_names);
  // Builds the GsmObservability plugin. The return status shows whether
  // GsmObservability was successfully enabled or not. TODO(): Is the
  // GsmObservability object useful?
  absl::StatusOr<GsmObservability> BuildAndRegister();

 private:
  OpenTelemetryPluginBuilder builder_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_GSM_GSM_OBSERVABILITY_H
