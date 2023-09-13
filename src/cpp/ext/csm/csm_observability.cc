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

#include <string>
#include <utility>

#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/uri/uri_parser.h"
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

CsmObservabilityBuilder& CsmObservabilityBuilder::SetTargetSelector(
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_selector) {
  builder_.SetTargetSelector(std::move(target_selector));
  return *this;
}

CsmObservabilityBuilder& CsmObservabilityBuilder::SetTargetAttributeFilter(
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_attribute_filter) {
  builder_.SetTargetAttributeFilter(std::move(target_attribute_filter));
  return *this;
}

absl::StatusOr<CsmObservability> CsmObservabilityBuilder::BuildAndRegister() {
  builder_.SetServerSelector([](const grpc_core::ChannelArgs& args) {
    // Directly using the channel arg string to avoid a dependency on xDS.
    return args.GetBool("grpc.internal.xds_server_config_fetcher")
        .value_or(false);
  });
  builder_.BuildAndRegisterGlobal();
  builder_.SetTargetSelector(CsmChannelTargetSelector);
  return CsmObservability();
}

bool CsmChannelTargetSelector(absl::string_view target) {
  auto uri = grpc_core::URI::Parse(target);
  if (!uri.ok()) {
    gpr_log(GPR_ERROR, "Failed to parse URI: %s", std::string(target).c_str());
    return false;
  }
  // CSM channels should have an "xds" scheme
  if (uri->scheme() != "xds") {
    return false;
  }
  // If set, the authority should be TD
  if (!uri->authority().empty() &&
      uri->authority() != "traffic-director-global.xds.googleapis.com") {
    return false;
  }
  return true;
}

}  // namespace internal
}  // namespace grpc
