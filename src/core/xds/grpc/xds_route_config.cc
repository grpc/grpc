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
  std::string result = "{base_interval=";
  StrAppend(result, base_interval.ToString());
  StrAppend(result, ", max_interval=");
  StrAppend(result, max_interval.ToString());
  StrAppend(result, "}");
  return result;
}

std::string XdsRouteConfigResource::RetryPolicy::ToString() const {
  std::string result = "{num_retries=";
  StrAppend(result, std::to_string(num_retries));
  StrAppend(result, ", retry_back_off=");
  StrAppend(result, retry_back_off.ToString());
  StrAppend(result, "}");
  return result;
}

//
// XdsRouteConfigResource::Route::Matchers
//

std::string XdsRouteConfigResource::Route::Matchers::ToString() const {
  std::string result = "{path_matcher={";
  StrAppend(result, path_matcher.ToString());
  StrAppend(result, "}");
  if (!header_matchers.empty()) {
    StrAppend(result, ", header_matchers=[");
    bool is_first = true;
    for (const HeaderMatcher& header_matcher : header_matchers) {
      if (!is_first) StrAppend(result, ", ");
      StrAppend(result, header_matcher.ToString());
      is_first = false;
    }
    StrAppend(result, "]");
  }
  if (fraction_per_million.has_value()) {
    StrAppend(result, ", fraction_per_million=");
    StrAppend(result, std::to_string(*fraction_per_million));
  }
  StrAppend(result, "}");
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
  std::string result = "header{name=";
  StrAppend(result, header_name);
  if (regex != nullptr) {
    StrAppend(result, ", regex=");
    StrAppend(result, regex->pattern());
  }
  if (!regex_substitution.empty()) {
    StrAppend(result, ", regex_substitution=");
    StrAppend(result, regex_substitution);
  }
  StrAppend(result, "}");
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
        StrAppend(result, "cluster_name=");
        StrAppend(result, cluster_name.cluster_name);
        is_first = false;
      },
      [&](const std::vector<ClusterWeight>& weighted_clusters) {
        if (!is_first) StrAppend(result, ", ");
        StrAppend(result, "weighted_clusters=[");
        for (size_t i = 0; i < weighted_clusters.size(); ++i) {
          if (i > 0) StrAppend(result, ", ");
          StrAppend(result, weighted_clusters[i].ToString());
        }
        StrAppend(result, "]");
        is_first = false;
      },
      [&](const ClusterSpecifierPluginName& cluster_specifier_plugin_name) {
        if (!is_first) StrAppend(result, ", ");
        StrAppend(result, "cluster_specifier_plugin_name=");
        StrAppend(result,
                  cluster_specifier_plugin_name.cluster_specifier_plugin_name);
        is_first = false;
      });
  if (max_stream_duration.has_value()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, ", max_stream_duration=");
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
  std::string result = "{matchers=";
  StrAppend(result, matchers.ToString());
  Match(
      action,
      [&](const UnknownAction&) {
        StrAppend(result, ",\n  unknown_action={}");
      },
      [&](const RouteAction& route_action) {
        StrAppend(result, ",\n  route_action=");
        StrAppend(result, route_action.ToString());
      },
      [&](const NonForwardingAction&) {
        StrAppend(result, ",\n  non_forwarding_action={}");
      });
  if (!typed_per_filter_config.empty()) {
    StrAppend(result, ",\n  typed_per_filter_config={");
    for (const auto& [name, config] : typed_per_filter_config) {
      StrAppend(result, "\n    ");
      StrAppend(result, name);
      StrAppend(result, "=");
      StrAppend(result, config.ToString());
    }
    StrAppend(result, "\n  }");
  }
  StrAppend(result, "}");
  return result;
}

//
// XdsRouteConfigResource::VirtualHost
//

std::string XdsRouteConfigResource::VirtualHost::ToString() const {
  std::string result = "{domains=[";
  bool is_first = true;
  for (const std::string& domain : domains) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, domain);
    is_first = false;
  }
  StrAppend(result, "]\n  routes=[");
  for (const XdsRouteConfigResource::Route& route : routes) {
    StrAppend(result, "\n    ");
    StrAppend(result, route.ToString());
  }
  StrAppend(result, "  ]\n  typed_per_filter_config={");
  for (const auto& [name, config] : typed_per_filter_config) {
    StrAppend(result, "\n    ");
    StrAppend(result, name);
    StrAppend(result, "=");
    StrAppend(result, config.ToString());
  }
  StrAppend(result, "\n  }}");
  return result;
}

//
// XdsRouteConfigResource
//

std::string XdsRouteConfigResource::ToString() const {
  std::string result = "{vhosts=[";
  for (size_t i = 0; i < virtual_hosts.size(); ++i) {
    StrAppend(result, i > 0 ? ",\n  " : "\n  ");
    StrAppend(result, virtual_hosts[i].ToString());
  }
  StrAppend(result, "\n  ]");
  if (!cluster_specifier_plugin_map.empty()) {
    if (!virtual_hosts.empty()) StrAppend(result, ",");
    StrAppend(result, "\n  cluster_specifier_plugins={");
    bool is_first = true;
    for (const auto& [name, plugin] : cluster_specifier_plugin_map) {
      StrAppend(result, is_first ? "\n    " : ",\n    ");
      StrAppend(result, name);
      StrAppend(result, "={");
      StrAppend(result, plugin);
      StrAppend(result, "}\n");
      is_first = false;
    }
    StrAppend(result, "}");
  }
  StrAppend(result, "}");
  return result;
}

}  // namespace grpc_core
