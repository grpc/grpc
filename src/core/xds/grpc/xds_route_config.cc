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

#include "src/core/xds/grpc/xds_route_config.h"

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "re2/re2.h"
#include "src/core/util/match.h"
#include "src/core/util/matchers.h"
#include "src/core/util/string.h"

namespace grpc_core {

//
// XdsRouteConfigResource::FilterConfigOverride
//

std::string XdsRouteConfigResource::FilterConfigOverride::ToString() const {
  std::string result = "{config_proto_type=";
  StrAppend(result, config_proto_type);
  StrAppend(result, ", config=");
  StrAppend(result, JsonDump(config));
  StrAppend(result, ", filter_config=");
  StrAppend(result,
            filter_config == nullptr ? "null" : filter_config->ToString());
  StrAppend(result, "}");
  return result;
}

//
// XdsRouteConfigResource::RetryPolicy
//

std::string XdsRouteConfigResource::RetryPolicy::RetryBackOff::ToString()
    const {
  std::string result = "RetryBackOff Base: ";
  StrAppend(result, base_interval.ToString());
  StrAppend(result, ",RetryBackOff max: ");
  StrAppend(result, max_interval.ToString());
  return result;
}

std::string XdsRouteConfigResource::RetryPolicy::ToString() const {
  std::string result = "{num_retries=";
  StrAppend(result, std::to_string(num_retries));
  StrAppend(result, ",");
  StrAppend(result, retry_back_off.ToString());
  StrAppend(result, "}");
  return result;
}

//
// XdsRouteConfigResource::Route::Matchers
//

std::string XdsRouteConfigResource::Route::Matchers::ToString() const {
  std::string result = "PathMatcher{";
  StrAppend(result, path_matcher.ToString());
  StrAppend(result, "}");
  for (const HeaderMatcher& header_matcher : header_matchers) {
    StrAppend(result, "\n");
    StrAppend(result, header_matcher.ToString());
  }
  if (fraction_per_million.has_value()) {
    StrAppend(result, "\nFraction Per Million ");
    StrAppend(result, std::to_string(fraction_per_million.value()));
  }
  return result;
}

//
// XdsRouteConfigResource::Route::RouteAction::HashPolicy::Header
//

XdsRouteConfigResource::Route::RouteAction::HashPolicy::Header::Header(
    const Header& other)
    : header_name(other.header_name),
      regex_substitution(other.regex_substitution) {
  if (other.regex != nullptr) {
    regex =
        std::make_unique<RE2>(other.regex->pattern(), other.regex->options());
  }
}

XdsRouteConfigResource::Route::RouteAction::HashPolicy::Header&
XdsRouteConfigResource::Route::RouteAction::HashPolicy::Header::operator=(
    const Header& other) {
  header_name = other.header_name;
  if (other.regex != nullptr) {
    regex =
        std::make_unique<RE2>(other.regex->pattern(), other.regex->options());
  }
  regex_substitution = other.regex_substitution;
  return *this;
}

XdsRouteConfigResource::Route::RouteAction::HashPolicy::Header::Header(
    Header&& other) noexcept
    : header_name(std::move(other.header_name)),
      regex(std::move(other.regex)),
      regex_substitution(std::move(other.regex_substitution)) {}

XdsRouteConfigResource::Route::RouteAction::HashPolicy::Header&
XdsRouteConfigResource::Route::RouteAction::HashPolicy::Header::operator=(
    Header&& other) noexcept {
  header_name = std::move(other.header_name);
  regex = std::move(other.regex);
  regex_substitution = std::move(other.regex_substitution);
  return *this;
}

bool XdsRouteConfigResource::Route::RouteAction::HashPolicy::Header::operator==(
    const Header& other) const {
  if (header_name != other.header_name) return false;
  if (regex == nullptr) {
    if (other.regex != nullptr) return false;
  } else {
    if (other.regex == nullptr) return false;
    if (regex->pattern() != other.regex->pattern()) return false;
  }
  return regex_substitution == other.regex_substitution;
}

std::string
XdsRouteConfigResource::Route::RouteAction::HashPolicy::Header::ToString()
    const {
  std::string result = "Header ";
  StrAppend(result, header_name);
  StrAppend(result, "/");
  if (regex != nullptr) {
    StrAppend(result, regex->pattern());
  }
  StrAppend(result, "/");
  StrAppend(result, regex_substitution);
  return result;
}

//
// XdsRouteConfigResource::Route::RouteAction::HashPolicy
//

std::string XdsRouteConfigResource::Route::RouteAction::HashPolicy::ToString()
    const {
  std::string result = "{";
  Match(
      policy,
      [&](const Header& header) { StrAppend(result, header.ToString()); },
      [&](const ChannelId&) { StrAppend(result, "ChannelId"); });
  StrAppend(result, ", terminal=");
  StrAppend(result, terminal ? "true" : "false");
  StrAppend(result, "}");
  return result;
}

//
// XdsRouteConfigResource::Route::RouteAction::ClusterWeight
//

std::string
XdsRouteConfigResource::Route::RouteAction::ClusterWeight::ToString() const {
  std::string result = "{cluster=";
  StrAppend(result, name);
  StrAppend(result, ", weight=");
  StrAppend(result, std::to_string(weight));
  if (!typed_per_filter_config.empty()) {
    StrAppend(result, ", typed_per_filter_config={");
    bool is_first = true;
    for (const auto& [key, config] : typed_per_filter_config) {
      if (!is_first) StrAppend(result, ", ");
      StrAppend(result, key);
      StrAppend(result, "=");
      StrAppend(result, config.ToString());
      is_first = false;
    }
    StrAppend(result, "}");
  }
  StrAppend(result, "}");
  return result;
}

//
// XdsRouteConfigResource::Route::RouteAction
//

std::string XdsRouteConfigResource::Route::RouteAction::ToString() const {
  std::string result = "{";
  bool is_first = true;
  for (const HashPolicy& hash_policy : hash_policies) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "hash_policy=");
    StrAppend(result, hash_policy.ToString());
    is_first = false;
  }
  if (retry_policy.has_value()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "retry_policy=");
    StrAppend(result, retry_policy->ToString());
    is_first = false;
  }
  Match(
      action,
      [&](const ClusterName& cluster_name) {
        if (!is_first) StrAppend(result, ", ");
        StrAppend(result, "Cluster name: ");
        StrAppend(result, cluster_name.cluster_name);
        is_first = false;
      },
      [&](const std::vector<ClusterWeight>& weighted_clusters) {
        for (const ClusterWeight& cluster_weight : weighted_clusters) {
          if (!is_first) StrAppend(result, ", ");
          StrAppend(result, cluster_weight.ToString());
          is_first = false;
        }
      },
      [&](const ClusterSpecifierPluginName& cluster_specifier_plugin_name) {
        if (!is_first) StrAppend(result, ", ");
        StrAppend(result, "Cluster specifier plugin name: ");
        StrAppend(result,
                  cluster_specifier_plugin_name.cluster_specifier_plugin_name);
        is_first = false;
      });
  if (max_stream_duration.has_value()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, max_stream_duration->ToString());
    is_first = false;
  }
  if (auto_host_rewrite) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "auto_host_rewrite=true");
    is_first = false;
  }
  StrAppend(result, "}");
  return result;
}

//
// XdsRouteConfigResource::Route
//

std::string XdsRouteConfigResource::Route::ToString() const {
  std::string result = matchers.ToString();
  auto* route_action =
      std::get_if<XdsRouteConfigResource::Route::RouteAction>(&action);
  StrAppend(result, "\n");
  if (route_action != nullptr) {
    StrAppend(result, "route=");
    StrAppend(result, route_action->ToString());
  } else if (std::holds_alternative<
                 XdsRouteConfigResource::Route::NonForwardingAction>(action)) {
    StrAppend(result, "non_forwarding_action={}");
  } else {
    StrAppend(result, "unknown_action={}");
  }
  if (!typed_per_filter_config.empty()) {
    StrAppend(result, "\ntyped_per_filter_config={");
    for (const auto& [name, config] : typed_per_filter_config) {
      StrAppend(result, "\n  ");
      StrAppend(result, name);
      StrAppend(result, "=");
      StrAppend(result, config.ToString());
    }
    StrAppend(result, "\n}");
  }
  return result;
}

//
// XdsRouteConfigResource::VirtualHost
//

std::string XdsRouteConfigResource::VirtualHost::ToString() const {
  std::string result = "vhost={\n  domains=[";
  bool is_first = true;
  for (const std::string& domain : domains) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, domain);
    is_first = false;
  }
  StrAppend(result, "]\n  routes=[\n");
  for (const XdsRouteConfigResource::Route& route : routes) {
    StrAppend(result, "    {\n");
    StrAppend(result, route.ToString());
    StrAppend(result, "\n    }\n");
  }
  StrAppend(result, "  ]\n  typed_per_filter_config={\n");
  for (const auto& [name, config] : typed_per_filter_config) {
    StrAppend(result, "    ");
    StrAppend(result, name);
    StrAppend(result, "=");
    StrAppend(result, config.ToString());
    StrAppend(result, "\n");
  }
  StrAppend(result, "  }\n}\n");
  return result;
}

//
// XdsRouteConfigResource
//

std::string XdsRouteConfigResource::ToString() const {
  std::string result;
  for (const VirtualHost& vhost : virtual_hosts) {
    StrAppend(result, vhost.ToString());
  }
  StrAppend(result, "cluster_specifier_plugins={\n");
  for (const auto& [name, plugin] : cluster_specifier_plugin_map) {
    StrAppend(result, name);
    StrAppend(result, "={");
    StrAppend(result, plugin);
    StrAppend(result, "}\n");
  }
  StrAppend(result, "}");
  return result;
}

}  // namespace grpc_core
