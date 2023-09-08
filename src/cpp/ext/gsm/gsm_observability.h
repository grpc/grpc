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

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

class CsmObservability {};

class CsmObservabilityBuilder {
 public:
  // TODO(yashykt): Should this take the SDK or the API MeterProvider? Benefit
  // of SDK MeterProvider - Can explicitly set histogram bucket boundaries, but
  // in the next iteration of the API, we would have it there as well.
  CsmObservabilityBuilder& SetMeterProvider(
      std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider>
          meter_provider);
  // Methods to manipulate which instruments are enabled in the OTel Stats
  // Plugin. The default set of instruments are -
  // grpc.client.attempt.started
  // grpc.client.attempt.duration
  // grpc.client.attempt.sent_total_compressed_message_size
  // grpc.client.attempt.rcvd_total_compressed_message_size
  // grpc.server.call.started
  // grpc.server.call.duration
  // grpc.server.call.sent_total_compressed_message_size
  // grpc.server.call.rcvd_total_compressed_message_size
  CsmObservabilityBuilder& EnableMetric(absl::string_view metric_name);
  CsmObservabilityBuilder& DisableMetric(absl::string_view metric_name);
  CsmObservabilityBuilder& DisableAllMetrics();

  // If set, \a target_selector is called once per channel to decide whether to
  // collect metrics on that target or not.
  CsmObservabilityBuilder& SetTargetSelector(
      absl::AnyInvocable<bool(absl::string_view /*target*/) const>
          target_selector);

  // Builds the CsmObservability plugin. The return status shows whether
  // CsmObservability was successfully enabled or not. TODO(): Is the
  // CsmObservability object useful?
  absl::StatusOr<CsmObservability> BuildAndRegister();

 private:
  OpenTelemetryPluginBuilder builder_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_GSM_GSM_OBSERVABILITY_H
