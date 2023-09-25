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

#ifndef GRPCPP_EXT_CSM_OBSERVABILITY_H
#define GRPCPP_EXT_CSM_OBSERVABILITY_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace experimental {

// This is a no-op at present, but in the future, this object would be useful
// for performing cleanup.
class CsmObservability {};

// CsmObservabilityBuilder configures observability for all service mesh traffic
// for a binary running on CSM.
class CsmObservabilityBuilder {
 public:
  CsmObservabilityBuilder& SetMeterProvider(
      std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider>
          meter_provider);
  // If set, \a target_attribute_filter is called per channel to decide whether
  // to record the target attribute on client or to replace it with "other".
  // This helps reduce the cardinality on metrics in cases where many channels
  // are created with different targets in the same binary (which might happen
  // for example, if the channel target string uses IP addresses directly).
  CsmObservabilityBuilder& SetTargetAttributeFilter(
      absl::AnyInvocable<bool(absl::string_view /*target*/) const>
          target_attribute_filter);
  // If set, \a generic_method_attribute_filter is called per call with a
  // generic method type to decide whether to record the method name or to
  // replace it with "other". Non-generic or pre-registered methods remain
  // unaffected. If not set, by default, generic method names are replaced with
  // "other" when recording metrics.
  CsmObservabilityBuilder& SetGenericMethodAttributeFilter(
      absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>
          generic_method_attribute_filter);
  // Builds the CsmObservability plugin. The return status shows whether
  // CsmObservability was successfully enabled or not.
  //
  // The most common way to use this API is -
  //
  // auto observability =
  //    CsmObservabilityBuilder().SetMeterProvider(provider).BuildAndRegister();
  //
  // The set of instruments available are -
  // grpc.client.attempt.started
  // grpc.client.attempt.duration
  // grpc.client.attempt.sent_total_compressed_message_size
  // grpc.client.attempt.rcvd_total_compressed_message_size
  // grpc.server.call.started
  // grpc.server.call.duration
  // grpc.server.call.sent_total_compressed_message_size
  // grpc.server.call.rcvd_total_compressed_message_size
  absl::StatusOr<CsmObservability> BuildAndRegister();

 private:
  internal::OpenTelemetryPluginBuilder builder_;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_EXT_CSM_OBSERVABILITY_H
