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

#include "src/core/xds/grpc/xds_http_rbac_filter.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <utility>
#include <variant>

#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/rbac/v3/rbac.upb.h"
#include "envoy/config/route/v3/route_components.upb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.upb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.upbdefs.h"
#include "envoy/type/matcher/v3/metadata.upb.h"
#include "envoy/type/matcher/v3/path.upb.h"
#include "envoy/type/matcher/v3/regex.upb.h"
#include "envoy/type/matcher/v3/string.upb.h"
#include "envoy/type/v3/range.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "src/core/config/experiment_env_var.h"
#include "src/core/ext/filters/rbac/rbac_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/upb_utils.h"
#include "src/core/xds/grpc/xds_audit_logger_registry.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "upb/base/string_view.h"
#include "upb/message/array.h"
#include "upb/message/map.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

absl::string_view XdsHttpRbacFilter::ConfigProtoName() const {
  return "envoy.extensions.filters.http.rbac.v3.RBAC";
}

absl::string_view XdsHttpRbacFilter::OverrideConfigProtoName() const {
  return "envoy.extensions.filters.http.rbac.v3.RBACPerRoute";
}

void XdsHttpRbacFilter::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_rbac_v3_RBAC_getmsgdef(symtab);
}

void XdsHttpRbacFilter::AddFilter(
    FilterChainBuilder& builder,
    RefCountedPtr<const FilterConfig> config) const {
  builder.AddFilter<RbacFilter>(std::move(config));
}

namespace {

Rbac::CidrRange ParseCidrRange(const envoy_config_core_v3_CidrRange* range) {
  Rbac::CidrRange cidr_range;
  cidr_range.address_prefix = UpbStringToStdString(
      envoy_config_core_v3_CidrRange_address_prefix(range));
  cidr_range.prefix_len =
      ParseUInt32Value(envoy_config_core_v3_CidrRange_prefix_len(range))
          .value_or(0);
  return cidr_range;
}

StringMatcher ParsePathMatcher(const XdsResourceType::DecodeContext& context,
                               const envoy_type_matcher_v3_PathMatcher* matcher,
                               ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".path");
  const auto* path = envoy_type_matcher_v3_PathMatcher_path(matcher);
  if (path == nullptr) {
    errors->AddError("field not present");
    return StringMatcher();
  }
  return StringMatcherParse(context, path, errors);
}

HeaderMatcher ParseHeaderMatcher(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_route_v3_HeaderMatcher* matcher,
    ValidationErrors* errors) {
  HeaderMatcher header_matcher =
      ParseXdsHeaderMatcher(context, matcher, errors);
  ValidationErrors::ScopedField field(errors, ".name");
  if (header_matcher.name() == ":scheme") {
    errors->AddError("':scheme' not allowed in header");
  } else if (absl::StartsWith(header_matcher.name(), "grpc-")) {
    errors->AddError("'grpc-' prefixes not allowed in header");
  }
  return header_matcher;
}

std::unique_ptr<Rbac::Permission> ParsePermission(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_rbac_v3_Permission* permission, size_t depth,
    ValidationErrors* errors);

std::vector<std::unique_ptr<Rbac::Permission>> ParsePermissionSet(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_rbac_v3_Permission_Set* set, size_t depth,
    ValidationErrors* errors) {
  std::vector<std::unique_ptr<Rbac::Permission>> result;
  size_t size;
  const envoy_config_rbac_v3_Permission* const* rules =
      envoy_config_rbac_v3_Permission_Set_rules(set, &size);
  for (size_t i = 0; i < size; ++i) {
    ValidationErrors::ScopedField field(errors,
                                        absl::StrCat(".rules[", i, "]"));
    result.push_back(ParsePermission(context, rules[i], depth + 1, errors));
  }
  return result;
};

std::unique_ptr<Rbac::Permission> ParsePermission(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_rbac_v3_Permission* permission, size_t depth,
    ValidationErrors* errors) {
  if (depth > 16) {
    errors->AddError("exceeded max recursion depth");
    return nullptr;
  }
  auto result = std::make_unique<Rbac::Permission>();
  if (envoy_config_rbac_v3_Permission_has_and_rules(permission)) {
    ValidationErrors::ScopedField field(errors, ".and_permission");
    *result = Rbac::Permission::MakeAndPermission(ParsePermissionSet(
        context, envoy_config_rbac_v3_Permission_and_rules(permission), depth,
        errors));
  } else if (envoy_config_rbac_v3_Permission_has_or_rules(permission)) {
    ValidationErrors::ScopedField field(errors, ".or_permission");
    *result = Rbac::Permission::MakeOrPermission(ParsePermissionSet(
        context, envoy_config_rbac_v3_Permission_or_rules(permission), depth,
        errors));
  } else if (envoy_config_rbac_v3_Permission_has_any(permission)) {
    *result = Rbac::Permission::MakeAnyPermission();
  } else if (envoy_config_rbac_v3_Permission_has_header(permission)) {
    ValidationErrors::ScopedField field(errors, ".header");
    *result = Rbac::Permission::MakeHeaderPermission(ParseHeaderMatcher(
        context, envoy_config_rbac_v3_Permission_header(permission), errors));
  } else if (envoy_config_rbac_v3_Permission_has_url_path(permission)) {
    ValidationErrors::ScopedField field(errors, ".url_path");
    *result = Rbac::Permission::MakePathPermission(ParsePathMatcher(
        context, envoy_config_rbac_v3_Permission_url_path(permission), errors));
  } else if (envoy_config_rbac_v3_Permission_has_destination_ip(permission)) {
    *result = Rbac::Permission::MakeDestIpPermission(ParseCidrRange(
        envoy_config_rbac_v3_Permission_destination_ip(permission)));
  } else if (envoy_config_rbac_v3_Permission_has_destination_port(permission)) {
    *result = Rbac::Permission::MakeDestPortPermission(
        envoy_config_rbac_v3_Permission_destination_port(permission));
  } else if (envoy_config_rbac_v3_Permission_has_metadata(permission)) {
    const auto* metadata = envoy_config_rbac_v3_Permission_metadata(permission);
    // The fields "filter", "path" and "value" are irrelevant to gRPC as per
    // https://github.com/grpc/proposal/blob/master/A41-xds-rbac.md and are not
    // being parsed.
    *result = Rbac::Permission::MakeMetadataPermission(
        envoy_type_matcher_v3_MetadataMatcher_invert(metadata));
  } else if (envoy_config_rbac_v3_Permission_has_not_rule(permission)) {
    ValidationErrors::ScopedField field(errors, ".not_rule");
    auto not_permission = ParsePermission(
        context, envoy_config_rbac_v3_Permission_not_rule(permission),
        depth + 1, errors);
    *result = Rbac::Permission::MakeNotPermission(std::move(not_permission));
  } else if (envoy_config_rbac_v3_Permission_has_requested_server_name(
                 permission)) {
    ValidationErrors::ScopedField field(errors, ".requested_server_name");
    *result = Rbac::Permission::MakeReqServerNamePermission(StringMatcherParse(
        context,
        envoy_config_rbac_v3_Permission_requested_server_name(permission),
        errors));
  } else {
    errors->AddError("invalid rule");
  }
  return result;
}

std::unique_ptr<Rbac::Principal> ParsePrincipal(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_rbac_v3_Principal* principal, size_t depth,
    ValidationErrors* errors);

std::vector<std::unique_ptr<Rbac::Principal>> ParsePrincipalSet(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_rbac_v3_Principal_Set* set, size_t depth,
    ValidationErrors* errors) {
  std::vector<std::unique_ptr<Rbac::Principal>> result;
  size_t size;
  const envoy_config_rbac_v3_Principal* const* ids =
      envoy_config_rbac_v3_Principal_Set_ids(set, &size);
  for (size_t i = 0; i < size; ++i) {
    ValidationErrors::ScopedField field(errors, absl::StrCat(".ids[", i, "]"));
    result.push_back(ParsePrincipal(context, ids[i], depth + 1, errors));
  }
  return result;
};

std::unique_ptr<Rbac::Principal> ParsePrincipal(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_rbac_v3_Principal* principal, size_t depth,
    ValidationErrors* errors) {
  if (depth > 16) {
    errors->AddError("exceeded max recursion depth");
    return nullptr;
  }
  auto result = std::make_unique<Rbac::Principal>();
  if (envoy_config_rbac_v3_Principal_has_and_ids(principal)) {
    ValidationErrors::ScopedField field(errors, ".and_ids");
    *result = Rbac::Principal::MakeAndPrincipal(ParsePrincipalSet(
        context, envoy_config_rbac_v3_Principal_and_ids(principal), depth,
        errors));
  } else if (envoy_config_rbac_v3_Principal_has_or_ids(principal)) {
    ValidationErrors::ScopedField field(errors, ".or_ids");
    *result = Rbac::Principal::MakeOrPrincipal(ParsePrincipalSet(
        context, envoy_config_rbac_v3_Principal_or_ids(principal), depth,
        errors));
  } else if (envoy_config_rbac_v3_Principal_has_any(principal)) {
    *result = Rbac::Principal::MakeAnyPrincipal();
  } else if (envoy_config_rbac_v3_Principal_has_authenticated(principal)) {
    std::optional<StringMatcher> string_matcher;
    const auto* principal_name =
        envoy_config_rbac_v3_Principal_Authenticated_principal_name(
            envoy_config_rbac_v3_Principal_authenticated(principal));
    if (principal_name != nullptr) {
      ValidationErrors::ScopedField field(errors,
                                          ".authenticated.principal_name");
      string_matcher = StringMatcherParse(context, principal_name, errors);
    }
    *result =
        Rbac::Principal::MakeAuthenticatedPrincipal(std::move(string_matcher));
  } else if (envoy_config_rbac_v3_Principal_has_source_ip(principal)) {
    *result = Rbac::Principal::MakeSourceIpPrincipal(
        ParseCidrRange(envoy_config_rbac_v3_Principal_source_ip(principal)));
  } else if (envoy_config_rbac_v3_Principal_has_direct_remote_ip(principal)) {
    *result = Rbac::Principal::MakeDirectRemoteIpPrincipal(ParseCidrRange(
        envoy_config_rbac_v3_Principal_direct_remote_ip(principal)));
  } else if (envoy_config_rbac_v3_Principal_has_remote_ip(principal)) {
    *result = Rbac::Principal::MakeRemoteIpPrincipal(
        ParseCidrRange(envoy_config_rbac_v3_Principal_remote_ip(principal)));
  } else if (envoy_config_rbac_v3_Principal_has_header(principal)) {
    ValidationErrors::ScopedField field(errors, ".header");
    *result = Rbac::Principal::MakeHeaderPrincipal(ParseHeaderMatcher(
        context, envoy_config_rbac_v3_Principal_header(principal), errors));
  } else if (envoy_config_rbac_v3_Principal_has_url_path(principal)) {
    ValidationErrors::ScopedField field(errors, ".url_path");
    *result = Rbac::Principal::MakePathPrincipal(ParsePathMatcher(
        context, envoy_config_rbac_v3_Principal_url_path(principal), errors));
  } else if (envoy_config_rbac_v3_Principal_has_metadata(principal)) {
    const auto* metadata = envoy_config_rbac_v3_Principal_metadata(principal);
    // The fields "filter", "path" and "value" are irrelevant to gRPC as per
    // https://github.com/grpc/proposal/blob/master/A41-xds-rbac.md and are not
    // being parsed.
    *result = Rbac::Principal::MakeMetadataPrincipal(
        envoy_type_matcher_v3_MetadataMatcher_invert(metadata));
  } else if (envoy_config_rbac_v3_Principal_has_not_id(principal)) {
    ValidationErrors::ScopedField field(errors, ".not_id");
    auto not_principal = ParsePrincipal(
        context, envoy_config_rbac_v3_Principal_not_id(principal), depth + 1,
        errors);
    *result = Rbac::Principal::MakeNotPrincipal(std::move(not_principal));
  } else {
    errors->AddError("invalid rule");
  }
  return result;
}

Rbac::Policy ParsePolicy(const XdsResourceType::DecodeContext& context,
                         const envoy_config_rbac_v3_Policy* policy,
                         ValidationErrors* errors) {
  Rbac::Policy result;
  // permissions
  size_t size;
  const envoy_config_rbac_v3_Permission* const* permissions =
      envoy_config_rbac_v3_Policy_permissions(policy, &size);
  std::vector<std::unique_ptr<Rbac::Permission>> or_permissions;
  for (size_t i = 0; i < size; ++i) {
    ValidationErrors::ScopedField field(errors,
                                        absl::StrCat(".permissions[", i, "]"));
    or_permissions.push_back(
        ParsePermission(context, permissions[i], /*depth=*/0, errors));
  }
  result.permissions =
      Rbac::Permission::MakeOrPermission(std::move(or_permissions));
  // principals
  const envoy_config_rbac_v3_Principal* const* principals =
      envoy_config_rbac_v3_Policy_principals(policy, &size);
  std::vector<std::unique_ptr<Rbac::Principal>> or_principals;
  for (size_t i = 0; i < size; ++i) {
    ValidationErrors::ScopedField field(errors,
                                        absl::StrCat(".principals[", i, "]"));
    or_principals.push_back(
        ParsePrincipal(context, principals[i], /*depth=*/0, errors));
  }
  result.principals =
      Rbac::Principal::MakeOrPrincipal(std::move(or_principals));
  // condition
  if (envoy_config_rbac_v3_Policy_has_condition(policy)) {
    ValidationErrors::ScopedField field(errors, ".condition");
    errors->AddError("condition not supported");
  }
  // checked_condition
  if (envoy_config_rbac_v3_Policy_has_checked_condition(policy)) {
    ValidationErrors::ScopedField field(errors, ".checked_condition");
    errors->AddError("checked condition not supported");
  }
  return result;
}

Rbac ParseXdsRbac(const XdsResourceType::DecodeContext& context,
                  const envoy_extensions_filters_http_rbac_v3_RBAC* rbac,
                  ValidationErrors* errors) {
  const auto* rules = envoy_extensions_filters_http_rbac_v3_RBAC_rules(rbac);
  if (rules == nullptr) {
    // No enforcing to be applied. An empty deny policy with an empty map
    // is equivalent to no enforcing.
    return Rbac("", Rbac::Action::kDeny, {});
  }
  ValidationErrors::ScopedField field(errors, ".rules");
  Rbac result;
  // action
  switch (envoy_config_rbac_v3_RBAC_action(rules)) {
    case envoy_config_rbac_v3_RBAC_ALLOW:
      result.action = Rbac::Action::kAllow;
      break;
    case envoy_config_rbac_v3_RBAC_DENY:
      result.action = Rbac::Action::kDeny;
      break;
    case envoy_config_rbac_v3_RBAC_LOG:
      // Treat Log action as RBAC being absent.
      // No enforcing to be applied. An empty deny policy with an empty map
      // is equivalent to no enforcing.
      return Rbac("", Rbac::Action::kDeny, {});
    default:
      ValidationErrors::ScopedField field(errors, ".action");
      errors->AddError("unknown action");
  }
  // policies
  if (envoy_config_rbac_v3_RBAC_policies_size(rules) != 0) {
    size_t iter = kUpb_Map_Begin;
    upb_StringView key_view;
    const envoy_config_rbac_v3_Policy* val;
    while (envoy_config_rbac_v3_RBAC_policies_next(rules, &key_view, &val,
                                                   &iter)) {
      absl::string_view key = UpbStringToAbsl(key_view);
      ValidationErrors::ScopedField field(errors,
                                          absl::StrCat(".policies[", key, "]"));
      result.policies[std::string(key)] = ParsePolicy(context, val, errors);
    }
  }
  // audit_logging_options
  // TODO(lwge): Remove env var guard once the feature is stable.
  if (IsExperimentEnvVarEnabled("GRPC_EXPERIMENTAL_XDS_RBAC_AUDIT_LOGGING") &&
      envoy_config_rbac_v3_RBAC_has_audit_logging_options(rules)) {
    ValidationErrors::ScopedField field(errors, ".audit_logging_options");
    const auto* audit_logging_options =
        envoy_config_rbac_v3_RBAC_audit_logging_options(rules);
    int32_t audit_condition =
        envoy_config_rbac_v3_RBAC_AuditLoggingOptions_audit_condition(
            audit_logging_options);
    switch (audit_condition) {
      case envoy_config_rbac_v3_RBAC_AuditLoggingOptions_NONE:
        result.audit_condition = Rbac::AuditCondition::kNone;
        break;
      case envoy_config_rbac_v3_RBAC_AuditLoggingOptions_ON_DENY:
        result.audit_condition = Rbac::AuditCondition::kOnDeny;
        break;
      case envoy_config_rbac_v3_RBAC_AuditLoggingOptions_ON_ALLOW:
        result.audit_condition = Rbac::AuditCondition::kOnAllow;
        break;
      case envoy_config_rbac_v3_RBAC_AuditLoggingOptions_ON_DENY_AND_ALLOW:
        result.audit_condition = Rbac::AuditCondition::kOnDenyAndAllow;
        break;
      default:
        ValidationErrors::ScopedField field(errors, ".audit_condition");
        errors->AddError("invalid audit condition");
    }
    size_t size;
    const envoy_config_rbac_v3_RBAC_AuditLoggingOptions_AuditLoggerConfig* const*
        logger_configs =
            envoy_config_rbac_v3_RBAC_AuditLoggingOptions_logger_configs(
                audit_logging_options, &size);
    const auto& registry =
        DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap())
            .audit_logger_registry();
    for (size_t i = 0; i < size; ++i) {
      ValidationErrors::ScopedField field(
          errors, absl::StrCat(".logger_configs[", i, "]"));
      auto config = registry.ParseXdsAuditLoggerConfig(
          context, logger_configs[i], errors);
      // Config will be null if audit logger is unsupported.
      // Note that this may not be an error if the logger is optional.
      if (config != nullptr) result.logger_configs.push_back(std::move(config));
    }
  }
  return result;
}

}  // namespace

RefCountedPtr<const FilterConfig> XdsHttpRbacFilter::ParseTopLevelConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse HTTP RBAC filter config");
    return nullptr;
  }
  auto* rbac = envoy_extensions_filters_http_rbac_v3_RBAC_parse(
      serialized_filter_config->data(), serialized_filter_config->size(),
      context.arena);
  if (rbac == nullptr) {
    errors->AddError("could not parse HTTP RBAC filter config");
    return nullptr;
  }
  auto config = MakeRefCounted<RbacFilter::Config>();
  config->rbac = ParseXdsRbac(context, rbac, errors);
  return config;
}

RefCountedPtr<const FilterConfig> XdsHttpRbacFilter::ParseOverrideConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse RBACPerRoute");
    return nullptr;
  }
  auto* rbac_per_route =
      envoy_extensions_filters_http_rbac_v3_RBACPerRoute_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena);
  if (rbac_per_route == nullptr) {
    errors->AddError("could not parse RBACPerRoute");
    return nullptr;
  }
  auto config = MakeRefCounted<RbacFilter::Config>();
  const auto* rbac =
      envoy_extensions_filters_http_rbac_v3_RBACPerRoute_rbac(rbac_per_route);
  if (rbac == nullptr) {
    // No enforcing to be applied. An empty deny policy with an empty map
    // is equivalent to no enforcing.
    config->rbac = Rbac("", Rbac::Action::kDeny, {});
  } else {
    ValidationErrors::ScopedField field(errors, ".rbac");
    config->rbac = ParseXdsRbac(context, rbac, errors);
  }
  return config;
}

}  // namespace grpc_core
