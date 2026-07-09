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

#include "src/core/xds/grpc/xds_audit_logger_registry.h"

#include <string>
#include <utility>

#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/config/rbac/v3/rbac.upb.h"
#include "src/core/lib/security/authorization/audit_logging.h"
#include "src/core/util/match.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

using experimental::AuditLoggerRegistry;

std::shared_ptr<const experimental::AuditLoggerFactory::Config>
XdsAuditLoggerRegistry::ParseXdsAuditLoggerConfig(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_rbac_v3_RBAC_AuditLoggingOptions_AuditLoggerConfig*
        logger_config,
    ValidationErrors* errors) const {
  const auto* typed_extension_config =
      envoy_config_rbac_v3_RBAC_AuditLoggingOptions_AuditLoggerConfig_audit_logger(
          logger_config);
  ValidationErrors::ScopedField field(errors, ".audit_logger");
  if (typed_extension_config == nullptr) {
    errors->AddError("field not present");
    return nullptr;
  }
  ValidationErrors::ScopedField field2(errors, ".typed_config");
  const auto* typed_config =
      envoy_config_core_v3_TypedExtensionConfig_typed_config(
          typed_extension_config);
  auto extension = ExtractXdsExtension(context, typed_config, errors);
  if (!extension.has_value()) return nullptr;
  absl::string_view name;
  Json config;
  Match(
      extension->value,
      // Built-in logger types.
      [&](absl::string_view serialized_value) {
        auto it = audit_logger_config_factories_.find(extension->type);
        if (it == audit_logger_config_factories_.end()) return;
        name = it->second->name();
        config = Json::FromObject(it->second->ConvertXdsAuditLoggerConfig(
            context, serialized_value, errors));
      },
      // Custom logger types.
      [&](Json json) {
        if (!AuditLoggerRegistry::FactoryExists(extension->type)) return;
        name = extension->type;
        config = json;
      });
  if (name.empty()) {
    if (!envoy_config_rbac_v3_RBAC_AuditLoggingOptions_AuditLoggerConfig_is_optional(
            logger_config)) {
      errors->AddError("unsupported audit logger type");
    }
    return nullptr;
  }
  // Validate the converted config.
  auto parsed_config = AuditLoggerRegistry::ParseConfig(name, config);
  if (!parsed_config.ok()) {
    errors->AddError(parsed_config.status().message());
    return nullptr;
  }
  return std::move(*parsed_config);
}

Json XdsAuditLoggerRegistry::ConvertXdsAuditLoggerConfig(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_rbac_v3_RBAC_AuditLoggingOptions_AuditLoggerConfig*
        logger_config,
    ValidationErrors* errors) const {
  const auto* typed_extension_config =
      envoy_config_rbac_v3_RBAC_AuditLoggingOptions_AuditLoggerConfig_audit_logger(
          logger_config);
  ValidationErrors::ScopedField field(errors, ".audit_logger");
  if (typed_extension_config == nullptr) {
    errors->AddError("field not present");
    return Json();  // A null Json object.
  }
  ValidationErrors::ScopedField field2(errors, ".typed_config");
  const auto* typed_config =
      envoy_config_core_v3_TypedExtensionConfig_typed_config(
          typed_extension_config);
  auto extension = ExtractXdsExtension(context, typed_config, errors);
  if (!extension.has_value()) return Json();
  absl::string_view name;
  Json config;
  Match(
      extension->value,
      // Built-in logger types.
      [&](absl::string_view serialized_value) {
        auto it = audit_logger_config_factories_.find(extension->type);
        if (it == audit_logger_config_factories_.end()) return;
        name = it->second->name();
        config = Json::FromObject(it->second->ConvertXdsAuditLoggerConfig(
            context, serialized_value, errors));
      },
      // Custom logger types.
      [&](Json json) {
        if (!AuditLoggerRegistry::FactoryExists(extension->type)) return;
        name = extension->type;
        config = json;
      });
  // Config not found in either case if name is empty.
  if (name.empty()) {
    if (!envoy_config_rbac_v3_RBAC_AuditLoggingOptions_AuditLoggerConfig_is_optional(
            logger_config)) {
      errors->AddError("unsupported audit logger type");
    }
    return Json();
  }
  // Validate the converted config.
  auto result = AuditLoggerRegistry::ParseConfig(name, config);
  if (!result.ok()) {
    errors->AddError(result.status().message());
    return Json();
  }
  return Json::FromObject({{std::string(name), std::move(config)}});
}

}  // namespace grpc_core
