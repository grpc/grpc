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

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_http_rbac_filter.h"

#include <stddef.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
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
#include "upb/upb.h"

#include "src/core/ext/filters/rbac/rbac_filter.h"
#include "src/core/ext/filters/rbac/rbac_service_config_parser.h"
#include "src/core/ext/xds/upb_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

namespace {

Json ParseRegexMatcherToJson(
    const envoy_type_matcher_v3_RegexMatcher* regex_matcher) {
  return Json::Object(
      {{"regex", UpbStringToStdString(envoy_type_matcher_v3_RegexMatcher_regex(
                     regex_matcher))}});
}

Json ParseInt64RangeToJson(const envoy_type_v3_Int64Range* range) {
  return Json::Object{{"start", envoy_type_v3_Int64Range_start(range)},
                      {"end", envoy_type_v3_Int64Range_end(range)}};
}

Json ParseHeaderMatcherToJson(const envoy_config_route_v3_HeaderMatcher* header,
                              ValidationErrors* errors) {
  Json::Object header_json;
  {
    ValidationErrors::ScopedField field(errors, ".name");
    std::string name =
        UpbStringToStdString(envoy_config_route_v3_HeaderMatcher_name(header));
    if (name == ":scheme") {
      errors->AddError("':scheme' not allowed in header");
    } else if (absl::StartsWith(name, "grpc-")) {
      errors->AddError("'grpc-' prefixes not allowed in header");
    }
    header_json.emplace("name", std::move(name));
  }
  if (envoy_config_route_v3_HeaderMatcher_has_exact_match(header)) {
    header_json.emplace(
        "exactMatch",
        UpbStringToStdString(
            envoy_config_route_v3_HeaderMatcher_exact_match(header)));
  } else if (envoy_config_route_v3_HeaderMatcher_has_safe_regex_match(header)) {
    header_json.emplace(
        "safeRegexMatch",
        ParseRegexMatcherToJson(
            envoy_config_route_v3_HeaderMatcher_safe_regex_match(header)));
  } else if (envoy_config_route_v3_HeaderMatcher_has_range_match(header)) {
    header_json.emplace(
        "rangeMatch",
        ParseInt64RangeToJson(
            envoy_config_route_v3_HeaderMatcher_range_match(header)));
  } else if (envoy_config_route_v3_HeaderMatcher_has_present_match(header)) {
    header_json.emplace(
        "presentMatch",
        envoy_config_route_v3_HeaderMatcher_present_match(header));
  } else if (envoy_config_route_v3_HeaderMatcher_has_prefix_match(header)) {
    header_json.emplace(
        "prefixMatch",
        UpbStringToStdString(
            envoy_config_route_v3_HeaderMatcher_prefix_match(header)));
  } else if (envoy_config_route_v3_HeaderMatcher_has_suffix_match(header)) {
    header_json.emplace(
        "suffixMatch",
        UpbStringToStdString(
            envoy_config_route_v3_HeaderMatcher_suffix_match(header)));
  } else if (envoy_config_route_v3_HeaderMatcher_has_contains_match(header)) {
    header_json.emplace(
        "containsMatch",
        UpbStringToStdString(
            envoy_config_route_v3_HeaderMatcher_contains_match(header)));
  } else {
    errors->AddError("invalid route header matcher specified");
  }
  header_json.emplace("invertMatch",
                      envoy_config_route_v3_HeaderMatcher_invert_match(header));
  return header_json;
}

Json ParseStringMatcherToJson(
    const envoy_type_matcher_v3_StringMatcher* matcher,
    ValidationErrors* errors) {
  Json::Object json;
  if (envoy_type_matcher_v3_StringMatcher_has_exact(matcher)) {
    json.emplace("exact",
                 UpbStringToStdString(
                     envoy_type_matcher_v3_StringMatcher_exact(matcher)));
  } else if (envoy_type_matcher_v3_StringMatcher_has_prefix(matcher)) {
    json.emplace("prefix",
                 UpbStringToStdString(
                     envoy_type_matcher_v3_StringMatcher_prefix(matcher)));
  } else if (envoy_type_matcher_v3_StringMatcher_has_suffix(matcher)) {
    json.emplace("suffix",
                 UpbStringToStdString(
                     envoy_type_matcher_v3_StringMatcher_suffix(matcher)));
  } else if (envoy_type_matcher_v3_StringMatcher_has_safe_regex(matcher)) {
    json.emplace("safeRegex",
                 ParseRegexMatcherToJson(
                     envoy_type_matcher_v3_StringMatcher_safe_regex(matcher)));
  } else if (envoy_type_matcher_v3_StringMatcher_has_contains(matcher)) {
    json.emplace("contains",
                 UpbStringToStdString(
                     envoy_type_matcher_v3_StringMatcher_contains(matcher)));
  } else {
    errors->AddError("invalid match pattern");
  }
  json.emplace("ignoreCase",
               envoy_type_matcher_v3_StringMatcher_ignore_case(matcher));
  return json;
}

Json ParsePathMatcherToJson(const envoy_type_matcher_v3_PathMatcher* matcher,
                            ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".path");
  const auto* path = envoy_type_matcher_v3_PathMatcher_path(matcher);
  if (path == nullptr) {
    errors->AddError("field not present");
    return Json();
  }
  Json path_json = ParseStringMatcherToJson(path, errors);
  return Json::Object{{"path", std::move(path_json)}};
}

Json ParseCidrRangeToJson(const envoy_config_core_v3_CidrRange* range) {
  Json::Object json;
  json.emplace("addressPrefix",
               UpbStringToStdString(
                   envoy_config_core_v3_CidrRange_address_prefix(range)));
  const auto* prefix_len = envoy_config_core_v3_CidrRange_prefix_len(range);
  if (prefix_len != nullptr) {
    json.emplace("prefixLen", google_protobuf_UInt32Value_value(prefix_len));
  }
  return json;
}

Json ParseMetadataMatcherToJson(
    const envoy_type_matcher_v3_MetadataMatcher* metadata_matcher) {
  Json::Object json;
  // The fields "filter", "path" and "value" are irrelevant to gRPC as per
  // https://github.com/grpc/proposal/blob/master/A41-xds-rbac.md and are not
  // being parsed.
  json.emplace("invert",
               envoy_type_matcher_v3_MetadataMatcher_invert(metadata_matcher));
  return json;
}

Json ParsePermissionToJson(const envoy_config_rbac_v3_Permission* permission,
                           ValidationErrors* errors) {
  Json::Object permission_json;
  // Helper function to parse Permission::Set to JSON. Used by `and_rules` and
  // `or_rules`.
  auto parse_permission_set_to_json =
      [errors](const envoy_config_rbac_v3_Permission_Set* set) -> Json {
    Json::Array rules_json;
    size_t size;
    const envoy_config_rbac_v3_Permission* const* rules =
        envoy_config_rbac_v3_Permission_Set_rules(set, &size);
    for (size_t i = 0; i < size; ++i) {
      ValidationErrors::ScopedField field(errors,
                                          absl::StrCat(".rules[", i, "]"));
      Json permission_json = ParsePermissionToJson(rules[i], errors);
      rules_json.emplace_back(std::move(permission_json));
    }
    return Json::Object({{"rules", std::move(rules_json)}});
  };
  if (envoy_config_rbac_v3_Permission_has_and_rules(permission)) {
    ValidationErrors::ScopedField field(errors, ".and_permission");
    const auto* and_rules =
        envoy_config_rbac_v3_Permission_and_rules(permission);
    Json permission_set_json = parse_permission_set_to_json(and_rules);
    permission_json.emplace("andRules", std::move(permission_set_json));
  } else if (envoy_config_rbac_v3_Permission_has_or_rules(permission)) {
    ValidationErrors::ScopedField field(errors, ".or_permission");
    const auto* or_rules = envoy_config_rbac_v3_Permission_or_rules(permission);
    Json permission_set_json = parse_permission_set_to_json(or_rules);
    permission_json.emplace("orRules", std::move(permission_set_json));
  } else if (envoy_config_rbac_v3_Permission_has_any(permission)) {
    permission_json.emplace("any",
                            envoy_config_rbac_v3_Permission_any(permission));
  } else if (envoy_config_rbac_v3_Permission_has_header(permission)) {
    ValidationErrors::ScopedField field(errors, ".header");
    Json header_json = ParseHeaderMatcherToJson(
        envoy_config_rbac_v3_Permission_header(permission), errors);
    permission_json.emplace("header", std::move(header_json));
  } else if (envoy_config_rbac_v3_Permission_has_url_path(permission)) {
    ValidationErrors::ScopedField field(errors, ".url_path");
    Json url_path_json = ParsePathMatcherToJson(
        envoy_config_rbac_v3_Permission_url_path(permission), errors);
    permission_json.emplace("urlPath", std::move(url_path_json));
  } else if (envoy_config_rbac_v3_Permission_has_destination_ip(permission)) {
    permission_json.emplace(
        "destinationIp",
        ParseCidrRangeToJson(
            envoy_config_rbac_v3_Permission_destination_ip(permission)));
  } else if (envoy_config_rbac_v3_Permission_has_destination_port(permission)) {
    permission_json.emplace(
        "destinationPort",
        envoy_config_rbac_v3_Permission_destination_port(permission));
  } else if (envoy_config_rbac_v3_Permission_has_metadata(permission)) {
    permission_json.emplace(
        "metadata", ParseMetadataMatcherToJson(
                        envoy_config_rbac_v3_Permission_metadata(permission)));
  } else if (envoy_config_rbac_v3_Permission_has_not_rule(permission)) {
    ValidationErrors::ScopedField field(errors, ".not_rule");
    Json not_rule_json = ParsePermissionToJson(
        envoy_config_rbac_v3_Permission_not_rule(permission), errors);
    permission_json.emplace("notRule", std::move(not_rule_json));
  } else if (envoy_config_rbac_v3_Permission_has_requested_server_name(
                 permission)) {
    ValidationErrors::ScopedField field(errors, ".requested_server_name");
    Json requested_server_name_json = ParseStringMatcherToJson(
        envoy_config_rbac_v3_Permission_requested_server_name(permission),
        errors);
    permission_json.emplace("requestedServerName",
                            std::move(requested_server_name_json));
  } else {
    errors->AddError("invalid rule");
  }
  return permission_json;
}

Json ParsePrincipalToJson(const envoy_config_rbac_v3_Principal* principal,
                          ValidationErrors* errors) {
  Json::Object principal_json;
  // Helper function to parse Principal::Set to JSON. Used by `and_ids` and
  // `or_ids`.
  auto parse_principal_set_to_json =
      [errors](const envoy_config_rbac_v3_Principal_Set* set) -> Json {
    Json::Array ids_json;
    size_t size;
    const envoy_config_rbac_v3_Principal* const* ids =
        envoy_config_rbac_v3_Principal_Set_ids(set, &size);
    for (size_t i = 0; i < size; ++i) {
      ValidationErrors::ScopedField field(errors,
                                          absl::StrCat(".ids[", i, "]"));
      Json principal_json = ParsePrincipalToJson(ids[i], errors);
      ids_json.emplace_back(std::move(principal_json));
    }
    return Json::Object({{"ids", std::move(ids_json)}});
  };
  if (envoy_config_rbac_v3_Principal_has_and_ids(principal)) {
    ValidationErrors::ScopedField field(errors, ".and_ids");
    const auto* and_rules = envoy_config_rbac_v3_Principal_and_ids(principal);
    Json principal_set_json = parse_principal_set_to_json(and_rules);
    principal_json.emplace("andIds", std::move(principal_set_json));
  } else if (envoy_config_rbac_v3_Principal_has_or_ids(principal)) {
    ValidationErrors::ScopedField field(errors, ".or_ids");
    const auto* or_rules = envoy_config_rbac_v3_Principal_or_ids(principal);
    Json principal_set_json = parse_principal_set_to_json(or_rules);
    principal_json.emplace("orIds", std::move(principal_set_json));
  } else if (envoy_config_rbac_v3_Principal_has_any(principal)) {
    principal_json.emplace("any",
                           envoy_config_rbac_v3_Principal_any(principal));
  } else if (envoy_config_rbac_v3_Principal_has_authenticated(principal)) {
    auto* authenticated_json =
        principal_json.emplace("authenticated", Json::Object())
            .first->second.mutable_object();
    const auto* principal_name =
        envoy_config_rbac_v3_Principal_Authenticated_principal_name(
            envoy_config_rbac_v3_Principal_authenticated(principal));
    if (principal_name != nullptr) {
      ValidationErrors::ScopedField field(errors,
                                          ".authenticated.principal_name");
      Json principal_name_json =
          ParseStringMatcherToJson(principal_name, errors);
      authenticated_json->emplace("principalName",
                                  std::move(principal_name_json));
    }
  } else if (envoy_config_rbac_v3_Principal_has_source_ip(principal)) {
    principal_json.emplace(
        "sourceIp", ParseCidrRangeToJson(
                        envoy_config_rbac_v3_Principal_source_ip(principal)));
  } else if (envoy_config_rbac_v3_Principal_has_direct_remote_ip(principal)) {
    principal_json.emplace(
        "directRemoteIp",
        ParseCidrRangeToJson(
            envoy_config_rbac_v3_Principal_direct_remote_ip(principal)));
  } else if (envoy_config_rbac_v3_Principal_has_remote_ip(principal)) {
    principal_json.emplace(
        "remoteIp", ParseCidrRangeToJson(
                        envoy_config_rbac_v3_Principal_remote_ip(principal)));
  } else if (envoy_config_rbac_v3_Principal_has_header(principal)) {
    ValidationErrors::ScopedField field(errors, ".header");
    Json header_json = ParseHeaderMatcherToJson(
        envoy_config_rbac_v3_Principal_header(principal), errors);
    principal_json.emplace("header", std::move(header_json));
  } else if (envoy_config_rbac_v3_Principal_has_url_path(principal)) {
    ValidationErrors::ScopedField field(errors, ".url_path");
    Json url_path_json = ParsePathMatcherToJson(
        envoy_config_rbac_v3_Principal_url_path(principal), errors);
    principal_json.emplace("urlPath", std::move(url_path_json));
  } else if (envoy_config_rbac_v3_Principal_has_metadata(principal)) {
    principal_json.emplace(
        "metadata", ParseMetadataMatcherToJson(
                        envoy_config_rbac_v3_Principal_metadata(principal)));
  } else if (envoy_config_rbac_v3_Principal_has_not_id(principal)) {
    ValidationErrors::ScopedField field(errors, ".not_id");
    Json not_id_json = ParsePrincipalToJson(
        envoy_config_rbac_v3_Principal_not_id(principal), errors);
    principal_json.emplace("notId", std::move(not_id_json));
  } else {
    errors->AddError("invalid rule");
  }
  return principal_json;
}

Json ParsePolicyToJson(const envoy_config_rbac_v3_Policy* policy,
                       ValidationErrors* errors) {
  Json::Object policy_json;
  size_t size;
  Json::Array permissions_json;
  const envoy_config_rbac_v3_Permission* const* permissions =
      envoy_config_rbac_v3_Policy_permissions(policy, &size);
  for (size_t i = 0; i < size; ++i) {
    ValidationErrors::ScopedField field(errors,
                                        absl::StrCat(".permissions[", i, "]"));
    Json permission_json = ParsePermissionToJson(permissions[i], errors);
    permissions_json.emplace_back(std::move(permission_json));
  }
  policy_json.emplace("permissions", std::move(permissions_json));
  Json::Array principals_json;
  const envoy_config_rbac_v3_Principal* const* principals =
      envoy_config_rbac_v3_Policy_principals(policy, &size);
  for (size_t i = 0; i < size; ++i) {
    ValidationErrors::ScopedField field(errors,
                                        absl::StrCat(".principals[", i, "]"));
    Json principal_json = ParsePrincipalToJson(principals[i], errors);
    principals_json.emplace_back(std::move(principal_json));
  }
  policy_json.emplace("principals", std::move(principals_json));
  if (envoy_config_rbac_v3_Policy_has_condition(policy)) {
    ValidationErrors::ScopedField field(errors, ".condition");
    errors->AddError("condition not supported");
  }
  if (envoy_config_rbac_v3_Policy_has_checked_condition(policy)) {
    ValidationErrors::ScopedField field(errors, ".checked_condition");
    errors->AddError("checked condition not supported");
  }
  return policy_json;
}

Json ParseHttpRbacToJson(const envoy_extensions_filters_http_rbac_v3_RBAC* rbac,
                         ValidationErrors* errors) {
  Json::Object rbac_json;
  const auto* rules = envoy_extensions_filters_http_rbac_v3_RBAC_rules(rbac);
  if (rules != nullptr) {
    ValidationErrors::ScopedField field(errors, ".rules");
    int action = envoy_config_rbac_v3_RBAC_action(rules);
    // Treat Log action as RBAC being absent
    if (action == envoy_config_rbac_v3_RBAC_LOG) {
      return rbac_json;
    }
    Json::Object inner_rbac_json;
    inner_rbac_json.emplace("action", envoy_config_rbac_v3_RBAC_action(rules));
    if (envoy_config_rbac_v3_RBAC_has_policies(rules)) {
      Json::Object policies_object;
      size_t iter = kUpb_Map_Begin;
      while (true) {
        auto* entry = envoy_config_rbac_v3_RBAC_policies_next(rules, &iter);
        if (entry == nullptr) {
          break;
        }
        absl::string_view key =
            UpbStringToAbsl(envoy_config_rbac_v3_RBAC_PoliciesEntry_key(entry));
        ValidationErrors::ScopedField field(
            errors, absl::StrCat(".policies[", key, "]"));
        Json policy = ParsePolicyToJson(
            envoy_config_rbac_v3_RBAC_PoliciesEntry_value(entry), errors);
        policies_object.emplace(std::string(key), std::move(policy));
      }
      inner_rbac_json.emplace("policies", std::move(policies_object));
    }
    rbac_json.emplace("rules", std::move(inner_rbac_json));
  }
  return rbac_json;
}

}  // namespace

absl::string_view XdsHttpRbacFilter::ConfigProtoName() const {
  return "envoy.extensions.filters.http.rbac.v3.RBAC";
}

absl::string_view XdsHttpRbacFilter::OverrideConfigProtoName() const {
  return "envoy.extensions.filters.http.rbac.v3.RBACPerRoute";
}

void XdsHttpRbacFilter::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_rbac_v3_RBAC_getmsgdef(symtab);
}

absl::optional<XdsHttpFilterImpl::FilterConfig>
XdsHttpRbacFilter::GenerateFilterConfig(XdsExtension extension,
                                        upb_Arena* arena,
                                        ValidationErrors* errors) const {
  absl::string_view* serialized_filter_config =
      absl::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse HTTP RBAC filter config");
    return absl::nullopt;
  }
  auto* rbac = envoy_extensions_filters_http_rbac_v3_RBAC_parse(
      serialized_filter_config->data(), serialized_filter_config->size(),
      arena);
  if (rbac == nullptr) {
    errors->AddError("could not parse HTTP RBAC filter config");
    return absl::nullopt;
  }
  return FilterConfig{ConfigProtoName(), ParseHttpRbacToJson(rbac, errors)};
}

absl::optional<XdsHttpFilterImpl::FilterConfig>
XdsHttpRbacFilter::GenerateFilterConfigOverride(
    XdsExtension extension, upb_Arena* arena, ValidationErrors* errors) const {
  absl::string_view* serialized_filter_config =
      absl::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse RBACPerRoute");
    return absl::nullopt;
  }
  auto* rbac_per_route =
      envoy_extensions_filters_http_rbac_v3_RBACPerRoute_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          arena);
  if (rbac_per_route == nullptr) {
    errors->AddError("could not parse RBACPerRoute");
    return absl::nullopt;
  }
  Json rbac_json;
  const auto* rbac =
      envoy_extensions_filters_http_rbac_v3_RBACPerRoute_rbac(rbac_per_route);
  if (rbac == nullptr) {
    rbac_json = Json::Object();
  } else {
    ValidationErrors::ScopedField field(errors, ".rbac");
    rbac_json = ParseHttpRbacToJson(rbac, errors);
  }
  return FilterConfig{OverrideConfigProtoName(), std::move(rbac_json)};
}

const grpc_channel_filter* XdsHttpRbacFilter::channel_filter() const {
  return &RbacFilter::kFilterVtable;
}

ChannelArgs XdsHttpRbacFilter::ModifyChannelArgs(
    const ChannelArgs& args) const {
  return args.Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
}

absl::StatusOr<XdsHttpFilterImpl::ServiceConfigJsonEntry>
XdsHttpRbacFilter::GenerateServiceConfig(
    const FilterConfig& hcm_filter_config,
    const FilterConfig* filter_config_override) const {
  Json policy_json = filter_config_override != nullptr
                         ? filter_config_override->config
                         : hcm_filter_config.config;
  // The policy JSON may be empty, that's allowed.
  return ServiceConfigJsonEntry{"rbacPolicy", policy_json.Dump()};
}

}  // namespace grpc_core
