//
// Copyright 2018 gRPC authors.
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

#include "src/core/ext/xds/xds_route_config.h"

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/config/route/v3/route.upb.h"
#include "envoy/config/route/v3/route.upbdefs.h"
#include "envoy/config/route/v3/route_components.upb.h"
#include "envoy/type/matcher/v3/regex.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "envoy/type/v3/range.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "re2/re2.h"
#include "upb/def.h"
#include "upb/text_encode.h"
#include "upb/upb.h"

#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/xds/upb_utils.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_cluster_specifier_plugin.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_http_filters.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/ext/xds/xds_resource_type_impl.h"
#include "src/core/ext/xds/xds_routing.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/matchers/matchers.h"

namespace grpc_core {

// TODO(yashykt): Remove once RBAC is no longer experimental
bool XdsRbacEnabled() {
  char* value = gpr_getenv("GRPC_XDS_EXPERIMENTAL_RBAC");
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value, &parsed_value);
  gpr_free(value);
  return parse_succeeded && parsed_value;
}

// TODO(donnadionne): Remove once RLS is no longer experimental
bool XdsRlsEnabled() {
  char* value = gpr_getenv("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value, &parsed_value);
  gpr_free(value);
  return parse_succeeded && parsed_value;
}

//
// XdsRouteConfigResource::RetryPolicy
//

std::string XdsRouteConfigResource::RetryPolicy::RetryBackOff::ToString()
    const {
  std::vector<std::string> contents;
  contents.push_back(
      absl::StrCat("RetryBackOff Base: ", base_interval.ToString()));
  contents.push_back(
      absl::StrCat("RetryBackOff max: ", max_interval.ToString()));
  return absl::StrJoin(contents, ",");
}

std::string XdsRouteConfigResource::RetryPolicy::ToString() const {
  std::vector<std::string> contents;
  contents.push_back(absl::StrFormat("num_retries=%d", num_retries));
  contents.push_back(retry_back_off.ToString());
  return absl::StrCat("{", absl::StrJoin(contents, ","), "}");
}

//
// XdsRouteConfigResource::Route::Matchers
//

std::string XdsRouteConfigResource::Route::Matchers::ToString() const {
  std::vector<std::string> contents;
  contents.push_back(
      absl::StrFormat("PathMatcher{%s}", path_matcher.ToString()));
  for (const HeaderMatcher& header_matcher : header_matchers) {
    contents.push_back(header_matcher.ToString());
  }
  if (fraction_per_million.has_value()) {
    contents.push_back(absl::StrFormat("Fraction Per Million %d",
                                       fraction_per_million.value()));
  }
  return absl::StrJoin(contents, "\n");
}

//
// XdsRouteConfigResource::Route::RouteAction::HashPolicy
//

XdsRouteConfigResource::Route::RouteAction::HashPolicy::HashPolicy(
    const HashPolicy& other)
    : type(other.type),
      header_name(other.header_name),
      regex_substitution(other.regex_substitution) {
  if (other.regex != nullptr) {
    regex =
        absl::make_unique<RE2>(other.regex->pattern(), other.regex->options());
  }
}

XdsRouteConfigResource::Route::RouteAction::HashPolicy&
XdsRouteConfigResource::Route::RouteAction::HashPolicy::operator=(
    const HashPolicy& other) {
  type = other.type;
  header_name = other.header_name;
  if (other.regex != nullptr) {
    regex =
        absl::make_unique<RE2>(other.regex->pattern(), other.regex->options());
  }
  regex_substitution = other.regex_substitution;
  return *this;
}

XdsRouteConfigResource::Route::RouteAction::HashPolicy::HashPolicy(
    HashPolicy&& other) noexcept
    : type(other.type),
      header_name(std::move(other.header_name)),
      regex(std::move(other.regex)),
      regex_substitution(std::move(other.regex_substitution)) {}

XdsRouteConfigResource::Route::RouteAction::HashPolicy&
XdsRouteConfigResource::Route::RouteAction::HashPolicy::operator=(
    HashPolicy&& other) noexcept {
  type = other.type;
  header_name = std::move(other.header_name);
  regex = std::move(other.regex);
  regex_substitution = std::move(other.regex_substitution);
  return *this;
}

bool XdsRouteConfigResource::Route::RouteAction::HashPolicy::HashPolicy::
operator==(const HashPolicy& other) const {
  if (type != other.type) return false;
  if (type == Type::HEADER) {
    if (regex == nullptr) {
      if (other.regex != nullptr) return false;
    } else {
      if (other.regex == nullptr) return false;
      return header_name == other.header_name &&
             regex->pattern() == other.regex->pattern() &&
             regex_substitution == other.regex_substitution;
    }
  }
  return true;
}

std::string XdsRouteConfigResource::Route::RouteAction::HashPolicy::ToString()
    const {
  std::vector<std::string> contents;
  switch (type) {
    case Type::HEADER:
      contents.push_back("type=HEADER");
      break;
    case Type::CHANNEL_ID:
      contents.push_back("type=CHANNEL_ID");
      break;
  }
  contents.push_back(
      absl::StrFormat("terminal=%s", terminal ? "true" : "false"));
  if (type == Type::HEADER) {
    contents.push_back(absl::StrFormat(
        "Header %s:/%s/%s", header_name,
        (regex == nullptr) ? "" : regex->pattern(), regex_substitution));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsRouteConfigResource::Route::RouteAction::ClusterWeight
//

std::string
XdsRouteConfigResource::Route::RouteAction::ClusterWeight::ToString() const {
  std::vector<std::string> contents;
  contents.push_back(absl::StrCat("cluster=", name));
  contents.push_back(absl::StrCat("weight=", weight));
  if (!typed_per_filter_config.empty()) {
    std::vector<std::string> parts;
    for (const auto& p : typed_per_filter_config) {
      const std::string& key = p.first;
      const auto& config = p.second;
      parts.push_back(absl::StrCat(key, "=", config.ToString()));
    }
    contents.push_back(absl::StrCat("typed_per_filter_config={",
                                    absl::StrJoin(parts, ", "), "}"));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsRouteConfigResource::Route::RouteAction
//

std::string XdsRouteConfigResource::Route::RouteAction::ToString() const {
  std::vector<std::string> contents;
  for (const HashPolicy& hash_policy : hash_policies) {
    contents.push_back(absl::StrCat("hash_policy=", hash_policy.ToString()));
  }
  if (retry_policy.has_value()) {
    contents.push_back(absl::StrCat("retry_policy=", retry_policy->ToString()));
  }
  Match(
      action,
      [&contents](const ClusterName& cluster_name) {
        contents.push_back(
            absl::StrFormat("Cluster name: %s", cluster_name.cluster_name));
      },
      [&contents](const std::vector<ClusterWeight>& weighted_clusters) {
        for (const ClusterWeight& cluster_weight : weighted_clusters) {
          contents.push_back(cluster_weight.ToString());
        }
      },
      [&contents](
          const ClusterSpecifierPluginName& cluster_specifier_plugin_name) {
        contents.push_back(absl::StrFormat(
            "Cluster specifier plugin name: %s",
            cluster_specifier_plugin_name.cluster_specifier_plugin_name));
      });
  if (max_stream_duration.has_value()) {
    contents.push_back(max_stream_duration->ToString());
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsRouteConfigResource::Route
//

std::string XdsRouteConfigResource::Route::ToString() const {
  std::vector<std::string> contents;
  contents.push_back(matchers.ToString());
  auto* route_action =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction>(&action);
  if (route_action != nullptr) {
    contents.push_back(absl::StrCat("route=", route_action->ToString()));
  } else if (absl::holds_alternative<
                 XdsRouteConfigResource::Route::NonForwardingAction>(action)) {
    contents.push_back("non_forwarding_action={}");
  } else {
    contents.push_back("unknown_action={}");
  }
  if (!typed_per_filter_config.empty()) {
    contents.push_back("typed_per_filter_config={");
    for (const auto& p : typed_per_filter_config) {
      const std::string& name = p.first;
      const auto& config = p.second;
      contents.push_back(absl::StrCat("  ", name, "=", config.ToString()));
    }
    contents.push_back("}");
  }
  return absl::StrJoin(contents, "\n");
}

//
// XdsRouteConfigResource
//

std::string XdsRouteConfigResource::ToString() const {
  std::vector<std::string> parts;
  for (const VirtualHost& vhost : virtual_hosts) {
    parts.push_back(
        absl::StrCat("vhost={\n"
                     "  domains=[",
                     absl::StrJoin(vhost.domains, ", "),
                     "]\n"
                     "  routes=[\n"));
    for (const XdsRouteConfigResource::Route& route : vhost.routes) {
      parts.push_back("    {\n");
      parts.push_back(route.ToString());
      parts.push_back("\n    }\n");
    }
    parts.push_back("  ]\n");
    parts.push_back("  typed_per_filter_config={\n");
    for (const auto& p : vhost.typed_per_filter_config) {
      const std::string& name = p.first;
      const auto& config = p.second;
      parts.push_back(absl::StrCat("    ", name, "=", config.ToString(), "\n"));
    }
    parts.push_back("  }\n");
    parts.push_back("]\n");
  }
  parts.push_back("cluster_specifier_plugins={\n");
  for (const auto& it : cluster_specifier_plugin_map) {
    parts.push_back(absl::StrFormat("%s={%s}\n", it.first, it.second));
  }
  parts.push_back("}");
  return absl::StrJoin(parts, "");
}

namespace {

absl::StatusOr<XdsRouteConfigResource::ClusterSpecifierPluginMap>
ClusterSpecifierPluginParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_route_v3_RouteConfiguration* route_config) {
  XdsRouteConfigResource::ClusterSpecifierPluginMap
      cluster_specifier_plugin_map;
  size_t num_cluster_specifier_plugins;
  const envoy_config_route_v3_ClusterSpecifierPlugin* const*
      cluster_specifier_plugin =
          envoy_config_route_v3_RouteConfiguration_cluster_specifier_plugins(
              route_config, &num_cluster_specifier_plugins);
  for (size_t i = 0; i < num_cluster_specifier_plugins; ++i) {
    const envoy_config_core_v3_TypedExtensionConfig* extension =
        envoy_config_route_v3_ClusterSpecifierPlugin_extension(
            cluster_specifier_plugin[i]);
    std::string name = UpbStringToStdString(
        envoy_config_core_v3_TypedExtensionConfig_name(extension));
    if (cluster_specifier_plugin_map.find(name) !=
        cluster_specifier_plugin_map.end()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Duplicated definition of cluster_specifier_plugin ", name));
    }
    const google_protobuf_Any* any =
        envoy_config_core_v3_TypedExtensionConfig_typed_config(extension);
    if (any == nullptr) {
      return absl::InvalidArgumentError(
          "Could not obtrain TypedExtensionConfig for plugin config.");
    }
    auto plugin_type = ExtractExtensionTypeName(context, any);
    if (!plugin_type.ok()) return plugin_type.status();
    bool is_optional = envoy_config_route_v3_ClusterSpecifierPlugin_is_optional(
        cluster_specifier_plugin[i]);
    const XdsClusterSpecifierPluginImpl* cluster_specifier_plugin_impl =
        XdsClusterSpecifierPluginRegistry::GetPluginForType(plugin_type->type);
    std::string lb_policy_config;
    if (cluster_specifier_plugin_impl == nullptr) {
      if (!is_optional) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Unknown ClusterSpecifierPlugin type ", plugin_type->type));
      }
      // Optional plugin, leave lb_policy_config empty.
    } else {
      auto config =
          cluster_specifier_plugin_impl->GenerateLoadBalancingPolicyConfig(
              google_protobuf_Any_value(any), context.arena, context.symtab);
      if (!config.ok()) return config.status();
      lb_policy_config = std::move(*config);
    }
    cluster_specifier_plugin_map[std::move(name)] = std::move(lb_policy_config);
  }
  return cluster_specifier_plugin_map;
}

absl::Status RoutePathMatchParse(const envoy_config_route_v3_RouteMatch* match,
                                 XdsRouteConfigResource::Route* route,
                                 bool* ignore_route) {
  auto* case_sensitive_ptr =
      envoy_config_route_v3_RouteMatch_case_sensitive(match);
  bool case_sensitive = true;
  if (case_sensitive_ptr != nullptr) {
    case_sensitive = google_protobuf_BoolValue_value(case_sensitive_ptr);
  }
  StringMatcher::Type type;
  std::string match_string;
  if (envoy_config_route_v3_RouteMatch_has_prefix(match)) {
    absl::string_view prefix =
        UpbStringToAbsl(envoy_config_route_v3_RouteMatch_prefix(match));
    // Empty prefix "" is accepted.
    if (!prefix.empty()) {
      // Prefix "/" is accepted.
      if (prefix[0] != '/') {
        // Prefix which does not start with a / will never match anything, so
        // ignore this route.
        *ignore_route = true;
        return absl::OkStatus();
      }
      std::vector<absl::string_view> prefix_elements =
          absl::StrSplit(prefix.substr(1), absl::MaxSplits('/', 2));
      if (prefix_elements.size() > 2) {
        // Prefix cannot have more than 2 slashes.
        *ignore_route = true;
        return absl::OkStatus();
      } else if (prefix_elements.size() == 2 && prefix_elements[0].empty()) {
        // Prefix contains empty string between the 2 slashes
        *ignore_route = true;
        return absl::OkStatus();
      }
    }
    type = StringMatcher::Type::kPrefix;
    match_string = std::string(prefix);
  } else if (envoy_config_route_v3_RouteMatch_has_path(match)) {
    absl::string_view path =
        UpbStringToAbsl(envoy_config_route_v3_RouteMatch_path(match));
    if (path.empty()) {
      // Path that is empty will never match anything, so ignore this route.
      *ignore_route = true;
      return absl::OkStatus();
    }
    if (path[0] != '/') {
      // Path which does not start with a / will never match anything, so
      // ignore this route.
      *ignore_route = true;
      return absl::OkStatus();
    }
    std::vector<absl::string_view> path_elements =
        absl::StrSplit(path.substr(1), absl::MaxSplits('/', 2));
    if (path_elements.size() != 2) {
      // Path not in the required format of /service/method will never match
      // anything, so ignore this route.
      *ignore_route = true;
      return absl::OkStatus();
    } else if (path_elements[0].empty()) {
      // Path contains empty service name will never match anything, so ignore
      // this route.
      *ignore_route = true;
      return absl::OkStatus();
    } else if (path_elements[1].empty()) {
      // Path contains empty method name will never match anything, so ignore
      // this route.
      *ignore_route = true;
      return absl::OkStatus();
    }
    type = StringMatcher::Type::kExact;
    match_string = std::string(path);
  } else if (envoy_config_route_v3_RouteMatch_has_safe_regex(match)) {
    const envoy_type_matcher_v3_RegexMatcher* regex_matcher =
        envoy_config_route_v3_RouteMatch_safe_regex(match);
    GPR_ASSERT(regex_matcher != nullptr);
    type = StringMatcher::Type::kSafeRegex;
    match_string = UpbStringToStdString(
        envoy_type_matcher_v3_RegexMatcher_regex(regex_matcher));
  } else {
    return absl::InvalidArgumentError(
        "Invalid route path specifier specified.");
  }
  absl::StatusOr<StringMatcher> string_matcher =
      StringMatcher::Create(type, match_string, case_sensitive);
  if (!string_matcher.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("path matcher: ", string_matcher.status().message()));
  }
  route->matchers.path_matcher = std::move(string_matcher.value());
  return absl::OkStatus();
}

absl::Status RouteHeaderMatchersParse(
    const envoy_config_route_v3_RouteMatch* match,
    XdsRouteConfigResource::Route* route) {
  size_t size;
  const envoy_config_route_v3_HeaderMatcher* const* headers =
      envoy_config_route_v3_RouteMatch_headers(match, &size);
  for (size_t i = 0; i < size; ++i) {
    const envoy_config_route_v3_HeaderMatcher* header = headers[i];
    const std::string name =
        UpbStringToStdString(envoy_config_route_v3_HeaderMatcher_name(header));
    HeaderMatcher::Type type;
    std::string match_string;
    int64_t range_start = 0;
    int64_t range_end = 0;
    bool present_match = false;
    if (envoy_config_route_v3_HeaderMatcher_has_exact_match(header)) {
      type = HeaderMatcher::Type::kExact;
      match_string = UpbStringToStdString(
          envoy_config_route_v3_HeaderMatcher_exact_match(header));
    } else if (envoy_config_route_v3_HeaderMatcher_has_safe_regex_match(
                   header)) {
      const envoy_type_matcher_v3_RegexMatcher* regex_matcher =
          envoy_config_route_v3_HeaderMatcher_safe_regex_match(header);
      GPR_ASSERT(regex_matcher != nullptr);
      type = HeaderMatcher::Type::kSafeRegex;
      match_string = UpbStringToStdString(
          envoy_type_matcher_v3_RegexMatcher_regex(regex_matcher));
    } else if (envoy_config_route_v3_HeaderMatcher_has_range_match(header)) {
      type = HeaderMatcher::Type::kRange;
      const envoy_type_v3_Int64Range* range_matcher =
          envoy_config_route_v3_HeaderMatcher_range_match(header);
      range_start = envoy_type_v3_Int64Range_start(range_matcher);
      range_end = envoy_type_v3_Int64Range_end(range_matcher);
    } else if (envoy_config_route_v3_HeaderMatcher_has_present_match(header)) {
      type = HeaderMatcher::Type::kPresent;
      present_match = envoy_config_route_v3_HeaderMatcher_present_match(header);
    } else if (envoy_config_route_v3_HeaderMatcher_has_prefix_match(header)) {
      type = HeaderMatcher::Type::kPrefix;
      match_string = UpbStringToStdString(
          envoy_config_route_v3_HeaderMatcher_prefix_match(header));
    } else if (envoy_config_route_v3_HeaderMatcher_has_suffix_match(header)) {
      type = HeaderMatcher::Type::kSuffix;
      match_string = UpbStringToStdString(
          envoy_config_route_v3_HeaderMatcher_suffix_match(header));
    } else if (envoy_config_route_v3_HeaderMatcher_has_contains_match(header)) {
      type = HeaderMatcher::Type::kContains;
      match_string = UpbStringToStdString(
          envoy_config_route_v3_HeaderMatcher_contains_match(header));
    } else {
      return absl::InvalidArgumentError(
          "Invalid route header matcher specified.");
    }
    bool invert_match =
        envoy_config_route_v3_HeaderMatcher_invert_match(header);
    absl::StatusOr<HeaderMatcher> header_matcher =
        HeaderMatcher::Create(name, type, match_string, range_start, range_end,
                              present_match, invert_match);
    if (!header_matcher.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("header matcher: ", header_matcher.status().message()));
    }
    route->matchers.header_matchers.emplace_back(
        std::move(header_matcher.value()));
  }
  return absl::OkStatus();
}

absl::Status RouteRuntimeFractionParse(
    const envoy_config_route_v3_RouteMatch* match,
    XdsRouteConfigResource::Route* route) {
  const envoy_config_core_v3_RuntimeFractionalPercent* runtime_fraction =
      envoy_config_route_v3_RouteMatch_runtime_fraction(match);
  if (runtime_fraction != nullptr) {
    const envoy_type_v3_FractionalPercent* fraction =
        envoy_config_core_v3_RuntimeFractionalPercent_default_value(
            runtime_fraction);
    if (fraction != nullptr) {
      uint32_t numerator = envoy_type_v3_FractionalPercent_numerator(fraction);
      const auto denominator =
          static_cast<envoy_type_v3_FractionalPercent_DenominatorType>(
              envoy_type_v3_FractionalPercent_denominator(fraction));
      // Normalize to million.
      switch (denominator) {
        case envoy_type_v3_FractionalPercent_HUNDRED:
          numerator *= 10000;
          break;
        case envoy_type_v3_FractionalPercent_TEN_THOUSAND:
          numerator *= 100;
          break;
        case envoy_type_v3_FractionalPercent_MILLION:
          break;
        default:
          return absl::InvalidArgumentError("Unknown denominator type");
      }
      route->matchers.fraction_per_million = numerator;
    }
  }
  return GRPC_ERROR_NONE;
}

template <typename ParentType, typename EntryType>
absl::StatusOr<XdsRouteConfigResource::TypedPerFilterConfig>
ParseTypedPerFilterConfig(
    const XdsResourceType::DecodeContext& context, const ParentType* parent,
    const EntryType* (*entry_func)(const ParentType*, size_t*),
    upb_StringView (*key_func)(const EntryType*),
    const google_protobuf_Any* (*value_func)(const EntryType*)) {
  XdsRouteConfigResource::TypedPerFilterConfig typed_per_filter_config;
  size_t filter_it = kUpb_Map_Begin;
  while (true) {
    const auto* filter_entry = entry_func(parent, &filter_it);
    if (filter_entry == nullptr) break;
    absl::string_view key = UpbStringToAbsl(key_func(filter_entry));
    if (key.empty()) {
      return absl::InvalidArgumentError("empty filter name in map");
    }
    const google_protobuf_Any* any = value_func(filter_entry);
    GPR_ASSERT(any != nullptr);
    absl::string_view filter_type =
        UpbStringToAbsl(google_protobuf_Any_type_url(any));
    if (filter_type.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("no filter config specified for filter name ", key));
    }
    bool is_optional = false;
    if (filter_type ==
        "type.googleapis.com/envoy.config.route.v3.FilterConfig") {
      upb_StringView any_value = google_protobuf_Any_value(any);
      const auto* filter_config = envoy_config_route_v3_FilterConfig_parse(
          any_value.data, any_value.size, context.arena);
      if (filter_config == nullptr) {
        return absl::InvalidArgumentError(
            absl::StrCat("could not parse FilterConfig wrapper for ", key));
      }
      is_optional =
          envoy_config_route_v3_FilterConfig_is_optional(filter_config);
      any = envoy_config_route_v3_FilterConfig_config(filter_config);
      if (any == nullptr) {
        if (is_optional) continue;
        return absl::InvalidArgumentError(
            absl::StrCat("no filter config specified for filter name ", key));
      }
    }
    auto type = ExtractExtensionTypeName(context, any);
    if (!type.ok()) return type.status();
    const XdsHttpFilterImpl* filter_impl =
        XdsHttpFilterRegistry::GetFilterForType(type->type);
    if (filter_impl == nullptr) {
      if (is_optional) continue;
      return absl::InvalidArgumentError(
          absl::StrCat("no filter registered for config type ", type->type));
    }
    absl::StatusOr<XdsHttpFilterImpl::FilterConfig> filter_config =
        filter_impl->GenerateFilterConfigOverride(
            google_protobuf_Any_value(any), context.arena);
    if (!filter_config.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("filter config for type ", type->type,
                       " failed to parse: ", filter_config.status().message()));
    }
    typed_per_filter_config[std::string(key)] = std::move(*filter_config);
  }
  return typed_per_filter_config;
}

absl::Status RetryPolicyParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_route_v3_RetryPolicy* retry_policy,
    absl::optional<XdsRouteConfigResource::RetryPolicy>* retry) {
  std::vector<std::string> errors;
  XdsRouteConfigResource::RetryPolicy retry_to_return;
  auto retry_on = UpbStringToStdString(
      envoy_config_route_v3_RetryPolicy_retry_on(retry_policy));
  std::vector<absl::string_view> codes = absl::StrSplit(retry_on, ',');
  for (const auto& code : codes) {
    if (code == "cancelled") {
      retry_to_return.retry_on.Add(GRPC_STATUS_CANCELLED);
    } else if (code == "deadline-exceeded") {
      retry_to_return.retry_on.Add(GRPC_STATUS_DEADLINE_EXCEEDED);
    } else if (code == "internal") {
      retry_to_return.retry_on.Add(GRPC_STATUS_INTERNAL);
    } else if (code == "resource-exhausted") {
      retry_to_return.retry_on.Add(GRPC_STATUS_RESOURCE_EXHAUSTED);
    } else if (code == "unavailable") {
      retry_to_return.retry_on.Add(GRPC_STATUS_UNAVAILABLE);
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
        gpr_log(GPR_INFO, "Unsupported retry_on policy %s.",
                std::string(code).c_str());
      }
    }
  }
  const google_protobuf_UInt32Value* num_retries =
      envoy_config_route_v3_RetryPolicy_num_retries(retry_policy);
  if (num_retries != nullptr) {
    uint32_t num_retries_value = google_protobuf_UInt32Value_value(num_retries);
    if (num_retries_value == 0) {
      errors.emplace_back(
          "RouteAction RetryPolicy num_retries set to invalid value 0.");
    } else {
      retry_to_return.num_retries = num_retries_value;
    }
  } else {
    retry_to_return.num_retries = 1;
  }
  const envoy_config_route_v3_RetryPolicy_RetryBackOff* backoff =
      envoy_config_route_v3_RetryPolicy_retry_back_off(retry_policy);
  if (backoff != nullptr) {
    const google_protobuf_Duration* base_interval =
        envoy_config_route_v3_RetryPolicy_RetryBackOff_base_interval(backoff);
    if (base_interval == nullptr) {
      errors.emplace_back(
          "RouteAction RetryPolicy RetryBackoff missing base interval.");
    } else {
      retry_to_return.retry_back_off.base_interval =
          ParseDuration(base_interval);
    }
    const google_protobuf_Duration* max_interval =
        envoy_config_route_v3_RetryPolicy_RetryBackOff_max_interval(backoff);
    Duration max;
    if (max_interval != nullptr) {
      max = ParseDuration(max_interval);
    } else {
      // if max interval is not set, it is 10x the base.
      max = 10 * retry_to_return.retry_back_off.base_interval;
    }
    retry_to_return.retry_back_off.max_interval = max;
  } else {
    retry_to_return.retry_back_off.base_interval = Duration::Milliseconds(25);
    retry_to_return.retry_back_off.max_interval = Duration::Milliseconds(250);
  }
  // Return result.
  if (!errors.empty()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Errors parsing retry policy: [", absl::StrJoin(errors, "; "), "]"));
  }
  *retry = retry_to_return;
  return absl::OkStatus();
}

absl::StatusOr<XdsRouteConfigResource::Route::RouteAction> RouteActionParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_route_v3_Route* route_msg,
    const std::map<std::string /*cluster_specifier_plugin_name*/,
                   std::string /*LB policy config*/>&
        cluster_specifier_plugin_map,
    bool* ignore_route) {
  XdsRouteConfigResource::Route::RouteAction route;
  const envoy_config_route_v3_RouteAction* route_action =
      envoy_config_route_v3_Route_route(route_msg);
  // Get the cluster or weighted_clusters in the RouteAction.
  if (envoy_config_route_v3_RouteAction_has_cluster(route_action)) {
    std::string cluster_name = UpbStringToStdString(
        envoy_config_route_v3_RouteAction_cluster(route_action));
    if (cluster_name.empty()) {
      return absl::InvalidArgumentError(
          "RouteAction cluster contains empty cluster name.");
    }
    route.action = XdsRouteConfigResource::Route::RouteAction::ClusterName{
        std::move(cluster_name)};
  } else if (envoy_config_route_v3_RouteAction_has_weighted_clusters(
                 route_action)) {
    std::vector<XdsRouteConfigResource::Route::RouteAction::ClusterWeight>
        action_weighted_clusters;
    const envoy_config_route_v3_WeightedCluster* weighted_cluster =
        envoy_config_route_v3_RouteAction_weighted_clusters(route_action);
    uint32_t total_weight = 100;
    const google_protobuf_UInt32Value* weight =
        envoy_config_route_v3_WeightedCluster_total_weight(weighted_cluster);
    if (weight != nullptr) {
      total_weight = google_protobuf_UInt32Value_value(weight);
    }
    size_t clusters_size;
    const envoy_config_route_v3_WeightedCluster_ClusterWeight* const* clusters =
        envoy_config_route_v3_WeightedCluster_clusters(weighted_cluster,
                                                       &clusters_size);
    uint32_t sum_of_weights = 0;
    for (size_t j = 0; j < clusters_size; ++j) {
      const envoy_config_route_v3_WeightedCluster_ClusterWeight*
          cluster_weight = clusters[j];
      XdsRouteConfigResource::Route::RouteAction::ClusterWeight cluster;
      cluster.name = UpbStringToStdString(
          envoy_config_route_v3_WeightedCluster_ClusterWeight_name(
              cluster_weight));
      if (cluster.name.empty()) {
        return absl::InvalidArgumentError(
            "RouteAction weighted_cluster cluster contains empty cluster "
            "name.");
      }
      const google_protobuf_UInt32Value* weight =
          envoy_config_route_v3_WeightedCluster_ClusterWeight_weight(
              cluster_weight);
      if (weight == nullptr) {
        return absl::InvalidArgumentError(
            "RouteAction weighted_cluster cluster missing weight");
      }
      cluster.weight = google_protobuf_UInt32Value_value(weight);
      if (cluster.weight == 0) continue;
      sum_of_weights += cluster.weight;
      if (context.server.ShouldUseV3()) {
        auto typed_per_filter_config = ParseTypedPerFilterConfig<
            envoy_config_route_v3_WeightedCluster_ClusterWeight,
            envoy_config_route_v3_WeightedCluster_ClusterWeight_TypedPerFilterConfigEntry>(
            context, cluster_weight,
            envoy_config_route_v3_WeightedCluster_ClusterWeight_typed_per_filter_config_next,
            envoy_config_route_v3_WeightedCluster_ClusterWeight_TypedPerFilterConfigEntry_key,
            envoy_config_route_v3_WeightedCluster_ClusterWeight_TypedPerFilterConfigEntry_value);
        if (!typed_per_filter_config.ok()) {
          return typed_per_filter_config.status();
        }
        cluster.typed_per_filter_config = std::move(*typed_per_filter_config);
      }
      action_weighted_clusters.emplace_back(std::move(cluster));
    }
    if (total_weight != sum_of_weights) {
      return absl::InvalidArgumentError(
          "RouteAction weighted_cluster has incorrect total weight");
    }
    if (action_weighted_clusters.empty()) {
      return absl::InvalidArgumentError(
          "RouteAction weighted_cluster has no valid clusters specified.");
    }
    route.action = std::move(action_weighted_clusters);
  } else if (XdsRlsEnabled() &&
             envoy_config_route_v3_RouteAction_has_cluster_specifier_plugin(
                 route_action)) {
    std::string plugin_name = UpbStringToStdString(
        envoy_config_route_v3_RouteAction_cluster_specifier_plugin(
            route_action));
    if (plugin_name.empty()) {
      return absl::InvalidArgumentError(
          "RouteAction cluster contains empty cluster specifier plugin name.");
    }
    auto it = cluster_specifier_plugin_map.find(plugin_name);
    if (it == cluster_specifier_plugin_map.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("RouteAction cluster contains cluster specifier plugin "
                       "name not configured: ",
                       plugin_name));
    }
    if (it->second.empty()) *ignore_route = true;
    route.action =
        XdsRouteConfigResource::Route::RouteAction::ClusterSpecifierPluginName{
            std::move(plugin_name)};
  } else {
    // No cluster or weighted_clusters or plugin found in RouteAction, ignore
    // this route.
    *ignore_route = true;
  }
  if (!*ignore_route) {
    const envoy_config_route_v3_RouteAction_MaxStreamDuration*
        max_stream_duration =
            envoy_config_route_v3_RouteAction_max_stream_duration(route_action);
    if (max_stream_duration != nullptr) {
      const google_protobuf_Duration* duration =
          envoy_config_route_v3_RouteAction_MaxStreamDuration_grpc_timeout_header_max(
              max_stream_duration);
      if (duration == nullptr) {
        duration =
            envoy_config_route_v3_RouteAction_MaxStreamDuration_max_stream_duration(
                max_stream_duration);
      }
      if (duration != nullptr) {
        route.max_stream_duration = ParseDuration(duration);
      }
    }
  }
  // Get HashPolicy from RouteAction
  size_t size = 0;
  const envoy_config_route_v3_RouteAction_HashPolicy* const* hash_policies =
      envoy_config_route_v3_RouteAction_hash_policy(route_action, &size);
  for (size_t i = 0; i < size; ++i) {
    const envoy_config_route_v3_RouteAction_HashPolicy* hash_policy =
        hash_policies[i];
    XdsRouteConfigResource::Route::RouteAction::HashPolicy policy;
    policy.terminal =
        envoy_config_route_v3_RouteAction_HashPolicy_terminal(hash_policy);
    const envoy_config_route_v3_RouteAction_HashPolicy_Header* header;
    const envoy_config_route_v3_RouteAction_HashPolicy_FilterState*
        filter_state;
    if ((header = envoy_config_route_v3_RouteAction_HashPolicy_header(
             hash_policy)) != nullptr) {
      policy.type =
          XdsRouteConfigResource::Route::RouteAction::HashPolicy::Type::HEADER;
      policy.header_name = UpbStringToStdString(
          envoy_config_route_v3_RouteAction_HashPolicy_Header_header_name(
              header));
      const struct envoy_type_matcher_v3_RegexMatchAndSubstitute*
          regex_rewrite =
              envoy_config_route_v3_RouteAction_HashPolicy_Header_regex_rewrite(
                  header);
      if (regex_rewrite != nullptr) {
        const envoy_type_matcher_v3_RegexMatcher* regex_matcher =
            envoy_type_matcher_v3_RegexMatchAndSubstitute_pattern(
                regex_rewrite);
        if (regex_matcher == nullptr) {
          gpr_log(
              GPR_DEBUG,
              "RouteAction HashPolicy contains policy specifier Header with "
              "RegexMatchAndSubstitution but RegexMatcher pattern is "
              "missing");
          continue;
        }
        RE2::Options options;
        policy.regex = absl::make_unique<RE2>(
            UpbStringToStdString(
                envoy_type_matcher_v3_RegexMatcher_regex(regex_matcher)),
            options);
        if (!policy.regex->ok()) {
          gpr_log(
              GPR_DEBUG,
              "RouteAction HashPolicy contains policy specifier Header with "
              "RegexMatchAndSubstitution but RegexMatcher pattern does not "
              "compile");
          continue;
        }
        policy.regex_substitution = UpbStringToStdString(
            envoy_type_matcher_v3_RegexMatchAndSubstitute_substitution(
                regex_rewrite));
      }
    } else if ((filter_state =
                    envoy_config_route_v3_RouteAction_HashPolicy_filter_state(
                        hash_policy)) != nullptr) {
      std::string key = UpbStringToStdString(
          envoy_config_route_v3_RouteAction_HashPolicy_FilterState_key(
              filter_state));
      if (key == "io.grpc.channel_id") {
        policy.type = XdsRouteConfigResource::Route::RouteAction::HashPolicy::
            Type::CHANNEL_ID;
      } else {
        gpr_log(GPR_DEBUG,
                "RouteAction HashPolicy contains policy specifier "
                "FilterState but "
                "key is not io.grpc.channel_id.");
        continue;
      }
    } else {
      gpr_log(GPR_DEBUG,
              "RouteAction HashPolicy contains unsupported policy specifier.");
      continue;
    }
    route.hash_policies.emplace_back(std::move(policy));
  }
  // Get retry policy
  const envoy_config_route_v3_RetryPolicy* retry_policy =
      envoy_config_route_v3_RouteAction_retry_policy(route_action);
  if (retry_policy != nullptr) {
    absl::optional<XdsRouteConfigResource::RetryPolicy> retry;
    absl::Status status = RetryPolicyParse(context, retry_policy, &retry);
    if (!status.ok()) return status;
    route.retry_policy = retry;
  }
  return route;
}

}  // namespace

absl::StatusOr<XdsRouteConfigResource> XdsRouteConfigResource::Parse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_route_v3_RouteConfiguration* route_config) {
  XdsRouteConfigResource rds_update;
  // Get the cluster spcifier plugins
  if (XdsRlsEnabled()) {
    auto cluster_specifier_plugin_map =
        ClusterSpecifierPluginParse(context, route_config);
    if (!cluster_specifier_plugin_map.ok()) {
      return cluster_specifier_plugin_map.status();
    }
    rds_update.cluster_specifier_plugin_map =
        std::move(*cluster_specifier_plugin_map);
  }
  // Get the virtual hosts.
  size_t num_virtual_hosts;
  const envoy_config_route_v3_VirtualHost* const* virtual_hosts =
      envoy_config_route_v3_RouteConfiguration_virtual_hosts(
          route_config, &num_virtual_hosts);
  for (size_t i = 0; i < num_virtual_hosts; ++i) {
    rds_update.virtual_hosts.emplace_back();
    XdsRouteConfigResource::VirtualHost& vhost =
        rds_update.virtual_hosts.back();
    // Parse domains.
    size_t domain_size;
    upb_StringView const* domains = envoy_config_route_v3_VirtualHost_domains(
        virtual_hosts[i], &domain_size);
    for (size_t j = 0; j < domain_size; ++j) {
      std::string domain_pattern = UpbStringToStdString(domains[j]);
      if (!XdsRouting::IsValidDomainPattern(domain_pattern)) {
        return absl::InvalidArgumentError(
            absl::StrCat("Invalid domain pattern \"", domain_pattern, "\"."));
      }
      vhost.domains.emplace_back(std::move(domain_pattern));
    }
    if (vhost.domains.empty()) {
      return absl::InvalidArgumentError("VirtualHost has no domains");
    }
    // Parse typed_per_filter_config.
    if (context.server.ShouldUseV3()) {
      auto typed_per_filter_config = ParseTypedPerFilterConfig<
          envoy_config_route_v3_VirtualHost,
          envoy_config_route_v3_VirtualHost_TypedPerFilterConfigEntry>(
          context, virtual_hosts[i],
          envoy_config_route_v3_VirtualHost_typed_per_filter_config_next,
          envoy_config_route_v3_VirtualHost_TypedPerFilterConfigEntry_key,
          envoy_config_route_v3_VirtualHost_TypedPerFilterConfigEntry_value);
      if (!typed_per_filter_config.ok()) {
        return typed_per_filter_config.status();
      }
      vhost.typed_per_filter_config = std::move(*typed_per_filter_config);
    }
    // Parse retry policy.
    absl::optional<XdsRouteConfigResource::RetryPolicy>
        virtual_host_retry_policy;
    const envoy_config_route_v3_RetryPolicy* retry_policy =
        envoy_config_route_v3_VirtualHost_retry_policy(virtual_hosts[i]);
    if (retry_policy != nullptr) {
      absl::Status status =
          RetryPolicyParse(context, retry_policy, &virtual_host_retry_policy);
      if (!status.ok()) return status;
    }
    // Parse routes.
    size_t num_routes;
    const envoy_config_route_v3_Route* const* routes =
        envoy_config_route_v3_VirtualHost_routes(virtual_hosts[i], &num_routes);
    if (num_routes < 1) {
      return absl::InvalidArgumentError("No route found in the virtual host.");
    }
    // Build a set of cluster_specifier_plugin configured to make sure each is
    // actually referenced by a route action.
    std::set<absl::string_view> cluster_specifier_plugins;
    for (auto& plugin : rds_update.cluster_specifier_plugin_map) {
      cluster_specifier_plugins.emplace(plugin.first);
    }
    // Loop over the whole list of routes
    for (size_t j = 0; j < num_routes; ++j) {
      const envoy_config_route_v3_RouteMatch* match =
          envoy_config_route_v3_Route_match(routes[j]);
      if (match == nullptr) {
        return absl::InvalidArgumentError("Match can't be null.");
      }
      size_t query_parameters_size;
      static_cast<void>(envoy_config_route_v3_RouteMatch_query_parameters(
          match, &query_parameters_size));
      if (query_parameters_size > 0) {
        continue;
      }
      XdsRouteConfigResource::Route route;
      bool ignore_route = false;
      absl::Status status = RoutePathMatchParse(match, &route, &ignore_route);
      if (!status.ok()) return status;
      if (ignore_route) continue;
      status = RouteHeaderMatchersParse(match, &route);
      if (!status.ok()) return status;
      status = RouteRuntimeFractionParse(match, &route);
      if (!status.ok()) return status;
      if (envoy_config_route_v3_Route_has_route(routes[j])) {
        route.action.emplace<XdsRouteConfigResource::Route::RouteAction>();
        auto route_action = RouteActionParse(
            context, routes[j], rds_update.cluster_specifier_plugin_map,
            &ignore_route);
        if (!route_action.ok()) return route_action.status();
        if (ignore_route) continue;
        if (route_action->retry_policy == absl::nullopt &&
            retry_policy != nullptr) {
          route_action->retry_policy = virtual_host_retry_policy;
        }
        // Mark off plugins used in route action.
        auto* cluster_specifier_action =
            absl::get_if<XdsRouteConfigResource::Route::RouteAction::
                             ClusterSpecifierPluginName>(&route_action->action);
        if (cluster_specifier_action != nullptr) {
          cluster_specifier_plugins.erase(
              cluster_specifier_action->cluster_specifier_plugin_name);
        }
        route.action = std::move(*route_action);
      } else if (envoy_config_route_v3_Route_has_non_forwarding_action(
                     routes[j])) {
        route.action
            .emplace<XdsRouteConfigResource::Route::NonForwardingAction>();
      }
      if (context.server.ShouldUseV3()) {
        auto typed_per_filter_config = ParseTypedPerFilterConfig<
            envoy_config_route_v3_Route,
            envoy_config_route_v3_Route_TypedPerFilterConfigEntry>(
            context, routes[j],
            envoy_config_route_v3_Route_typed_per_filter_config_next,
            envoy_config_route_v3_Route_TypedPerFilterConfigEntry_key,
            envoy_config_route_v3_Route_TypedPerFilterConfigEntry_value);
        if (!typed_per_filter_config.ok()) {
          return typed_per_filter_config.status();
        }
        route.typed_per_filter_config = std::move(*typed_per_filter_config);
      }
      vhost.routes.emplace_back(std::move(route));
    }
    if (vhost.routes.empty()) {
      return absl::InvalidArgumentError("No valid routes specified.");
    }
    // For plugins not used in route action, delete from the update to prevent
    // further use.
    for (auto& unused_plugin : cluster_specifier_plugins) {
      rds_update.cluster_specifier_plugin_map.erase(std::string(unused_plugin));
    }
  }
  return rds_update;
}

//
// XdsRouteConfigResourceType
//

namespace {

void MaybeLogRouteConfiguration(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_route_v3_RouteConfiguration* route_config) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_MessageDef* msg_type =
        envoy_config_route_v3_RouteConfiguration_getmsgdef(context.symtab);
    char buf[10240];
    upb_TextEncode(route_config, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] RouteConfiguration: %s", context.client,
            buf);
  }
}

}  // namespace

absl::StatusOr<XdsResourceType::DecodeResult>
XdsRouteConfigResourceType::Decode(
    const XdsResourceType::DecodeContext& context,
    absl::string_view serialized_resource, bool /*is_v2*/) const {
  // Parse serialized proto.
  auto* resource = envoy_config_route_v3_RouteConfiguration_parse(
      serialized_resource.data(), serialized_resource.size(), context.arena);
  if (resource == nullptr) {
    return absl::InvalidArgumentError(
        "Can't parse RouteConfiguration resource.");
  }
  MaybeLogRouteConfiguration(context, resource);
  // Validate resource.
  DecodeResult result;
  result.name = UpbStringToStdString(
      envoy_config_route_v3_RouteConfiguration_name(resource));
  auto rds_update = XdsRouteConfigResource::Parse(context, resource);
  if (!rds_update.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_ERROR, "[xds_client %p] invalid RouteConfiguration %s: %s",
              context.client, result.name.c_str(),
              rds_update.status().ToString().c_str());
    }
    result.resource = rds_update.status();
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_INFO, "[xds_client %p] parsed RouteConfiguration %s: %s",
              context.client, result.name.c_str(),
              rds_update->ToString().c_str());
    }
    auto resource = absl::make_unique<ResourceDataSubclass>();
    resource->resource = std::move(*rds_update);
    result.resource = std::move(resource);
  }
  return std::move(result);
}

}  // namespace grpc_core
