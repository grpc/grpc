//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_BOOTSTRAP_GRPC_BUILDER_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_BOOTSTRAP_GRPC_BUILDER_H

#include <memory>
#include <optional>

#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_audit_logger_registry.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_http_filter_registry.h"
#include "src/core/xds/grpc/xds_lb_policy_registry.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "upb/reflection/def.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class GrpcXdsBootstrapBuilder final {
 public:
  static absl::StatusOr<std::unique_ptr<GrpcXdsBootstrap>> Build(
      absl::string_view json_string);

  static void SetXdsHttpFilterFactoryForTest(
      absl::AnyInvocable<std::unique_ptr<XdsHttpFilterFactory>()> factory);

  static XdsHttpFilterRegistry CreateXdsHttpFilterRegistry(
      bool register_builtins = true);

  static XdsLbPolicyRegistry CreateXdsLbPolicyRegistry();

  static XdsAuditLoggerRegistry CreateXdsAuditLoggerRegistry();
};

// Exposed for testing purposes only.
class XdsHttpRouterFilterFactory final : public XdsHttpFilterFactory {
 public:
  absl::string_view ConfigProtoName() const override;
  absl::string_view OverrideConfigProtoName() const override;
  void PopulateSymtab(upb_DefPool* symtab) const override;
  std::optional<Json> GenerateFilterConfig(
      absl::string_view /*instance_name*/,
      const XdsResourceType::DecodeContext& context,
      const XdsExtension& extension, ValidationErrors* errors) const override;
  std::optional<Json> GenerateFilterConfigOverride(
      absl::string_view /*instance_name*/,
      const XdsResourceType::DecodeContext& context,
      const XdsExtension& extension, ValidationErrors* errors) const override;
  const grpc_channel_filter* channel_filter() const override { return nullptr; }
  absl::StatusOr<ServiceConfigJsonEntry> GenerateMethodConfig(
      const Json& /*hcm_filter_config*/,
      const Json* /*filter_config_override*/) const override {
    // This will never be called, since channel_filter() returns null.
    return absl::UnimplementedError("router filter should never be called");
  }
  absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const Json& /*hcm_filter_config*/) const override {
    // This will never be called, since channel_filter() returns null.
    return absl::UnimplementedError("router filter should never be called");
  }
  void AddFilter(FilterChainBuilder& /*builder*/,
                 RefCountedPtr<const FilterConfig> /*config*/) const override {}
  RefCountedPtr<const FilterConfig> ParseTopLevelConfig(
      absl::string_view instance_name,
      const XdsResourceType::DecodeContext& context,
      const XdsExtension& extension, ValidationErrors* errors) const override;
  RefCountedPtr<const FilterConfig> ParseOverrideConfig(
      absl::string_view instance_name,
      const XdsResourceType::DecodeContext& context,
      const XdsExtension& extension, ValidationErrors* errors) const override;
  bool IsSupportedOnClients() const override { return true; }
  bool IsSupportedOnServers() const override { return true; }
  bool IsTerminalFilter() const override { return true; }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_BOOTSTRAP_GRPC_BUILDER_H
