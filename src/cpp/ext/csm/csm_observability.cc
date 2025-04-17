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

#include "src/cpp/ext/csm/csm_observability.h"

#include <grpc/support/port_platform.h>
#include <grpcpp/ext/csm_observability.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "google/cloud/opentelemetry/resource_detector.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/resource/resource_detector.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/uri.h"
#include "src/core/xds/grpc/xds_enabled_server.h"
#include "src/cpp/ext/csm/metadata_exchange.h"
#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {

namespace internal {

namespace {
std::atomic<bool> g_csm_plugin_enabled(false);
}

bool CsmServerSelector(const grpc_core::ChannelArgs& /*args*/) {
  return g_csm_plugin_enabled;
}

bool CsmChannelTargetSelector(absl::string_view target) {
  if (!g_csm_plugin_enabled) return false;
  auto uri = grpc_core::URI::Parse(target);
  if (!uri.ok()) {
    LOG(ERROR) << "Failed to parse URI: " << target;
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

class CsmOpenTelemetryPluginOption
    : public grpc::internal::InternalOpenTelemetryPluginOption {
 public:
  CsmOpenTelemetryPluginOption()
      : labels_injector_(std::make_unique<internal::ServiceMeshLabelsInjector>(
            google::cloud::otel::MakeResourceDetector()
                ->Detect()
                .GetAttributes())) {}

  bool IsActiveOnClientChannel(absl::string_view target) const override {
    return CsmChannelTargetSelector(target);
  }

  bool IsActiveOnServer(const grpc_core::ChannelArgs& args) const override {
    return CsmServerSelector(args);
  }

  const grpc::internal::LabelsInjector* labels_injector() const override {
    return labels_injector_.get();
  }

 private:
  std::unique_ptr<internal::ServiceMeshLabelsInjector> labels_injector_;
};

}  // namespace internal

//
// CsmObservability
//

CsmObservability::~CsmObservability() {
  if (valid_) {
    internal::g_csm_plugin_enabled = false;
  }
}

CsmObservability::CsmObservability(CsmObservability&& other) noexcept {
  other.valid_ = false;
}
CsmObservability& CsmObservability::operator=(
    CsmObservability&& other) noexcept {
  other.valid_ = false;
  return *this;
}

//
// CsmObservabilityBuilder
//

CsmObservabilityBuilder::CsmObservabilityBuilder()
    : builder_(
          std::make_unique<grpc::internal::OpenTelemetryPluginBuilderImpl>()) {}

CsmObservabilityBuilder::~CsmObservabilityBuilder() = default;

CsmObservabilityBuilder& CsmObservabilityBuilder::SetMeterProvider(
    std::shared_ptr<opentelemetry::metrics::MeterProvider> meter_provider) {
  builder_->SetMeterProvider(meter_provider);
  return *this;
}

CsmObservabilityBuilder& CsmObservabilityBuilder::SetTargetAttributeFilter(
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_attribute_filter) {
  builder_->SetTargetAttributeFilter(std::move(target_attribute_filter));
  return *this;
}

CsmObservabilityBuilder&
CsmObservabilityBuilder::SetGenericMethodAttributeFilter(
    absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>
        generic_method_attribute_filter) {
  builder_->SetGenericMethodAttributeFilter(
      std::move(generic_method_attribute_filter));
  return *this;
}

absl::StatusOr<CsmObservability> CsmObservabilityBuilder::BuildAndRegister() {
  builder_->AddPluginOption(
      std::make_unique<grpc::internal::CsmOpenTelemetryPluginOption>());
  auto status = builder_->BuildAndRegisterGlobal();
  internal::g_csm_plugin_enabled = true;
  if (!status.ok()) {
    return status;
  }
  return CsmObservability();
}

}  // namespace grpc
