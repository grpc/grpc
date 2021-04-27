/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "envoy/admin/v3/config_dump.upb.h"
#include "envoy/config/cluster/v3/circuit_breaker.upb.h"
#include "envoy/config/cluster/v3/cluster.upb.h"
#include "envoy/config/cluster/v3/cluster.upbdefs.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/base.upbdefs.h"
#include "envoy/config/core/v3/config_source.upb.h"
#include "envoy/config/core/v3/health_check.upb.h"
#include "envoy/config/core/v3/protocol.upb.h"
#include "envoy/config/endpoint/v3/endpoint.upb.h"
#include "envoy/config/endpoint/v3/endpoint.upbdefs.h"
#include "envoy/config/endpoint/v3/endpoint_components.upb.h"
#include "envoy/config/endpoint/v3/load_report.upb.h"
#include "envoy/config/listener/v3/api_listener.upb.h"
#include "envoy/config/listener/v3/listener.upb.h"
#include "envoy/config/listener/v3/listener.upbdefs.h"
#include "envoy/config/listener/v3/listener_components.upb.h"
#include "envoy/config/route/v3/route.upb.h"
#include "envoy/config/route/v3/route.upbdefs.h"
#include "envoy/config/route/v3/route_components.upb.h"
#include "envoy/config/route/v3/route_components.upbdefs.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.upb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.upbdefs.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upbdefs.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.upb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upbdefs.h"
#include "envoy/service/cluster/v3/cds.upb.h"
#include "envoy/service/cluster/v3/cds.upbdefs.h"
#include "envoy/service/discovery/v3/discovery.upb.h"
#include "envoy/service/discovery/v3/discovery.upbdefs.h"
#include "envoy/service/endpoint/v3/eds.upb.h"
#include "envoy/service/endpoint/v3/eds.upbdefs.h"
#include "envoy/service/listener/v3/lds.upb.h"
#include "envoy/service/load_stats/v3/lrs.upb.h"
#include "envoy/service/load_stats/v3/lrs.upbdefs.h"
#include "envoy/service/route/v3/rds.upb.h"
#include "envoy/service/route/v3/rds.upbdefs.h"
#include "envoy/service/status/v3/csds.upb.h"
#include "envoy/service/status/v3/csds.upbdefs.h"
#include "envoy/type/matcher/v3/regex.upb.h"
#include "envoy/type/matcher/v3/string.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "envoy/type/v3/range.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "google/rpc/status.upb.h"
#include "udpa/type/v1/typed_struct.upb.h"
#include "upb/text_encode.h"
#include "upb/upb.h"
#include "upb/upb.hpp"

#include <grpc/impl/codegen/log.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/xds/xds_api.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/slice/slice_utils.h"

namespace grpc_core {

// TODO(donnadionne): Check to see if cluster types aggregate_cluster and
// logical_dns are enabled, this will be
// removed once the cluster types are fully integration-tested and enabled by
// default.
bool XdsAggregateAndLogicalDnsClusterEnabled() {
  char* value = gpr_getenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value, &parsed_value);
  gpr_free(value);
  return parse_succeeded && parsed_value;
}

// TODO(donnadionne): Check to see if ring hash policy is enabled, this will be
// removed once ring hash policy is fully integration-tested and enabled by
// default.
bool XdsRingHashEnabled() {
  char* value = gpr_getenv("GRPC_XDS_EXPERIMENTAL_ENABLE_RING_HASH");
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value, &parsed_value);
  gpr_free(value);
  return parse_succeeded && parsed_value;
}

// TODO(yashykt): Check to see if xDS security is enabled. This will be
// removed once this feature is fully integration-tested and enabled by
// default.
bool XdsSecurityEnabled() {
  char* value = gpr_getenv("GRPC_XDS_EXPERIMENTAL_SECURITY_SUPPORT");
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value, &parsed_value);
  gpr_free(value);
  return parse_succeeded && parsed_value;
}

//
// XdsApi::Route::HashPolicy
//

XdsApi::Route::HashPolicy::HashPolicy(const HashPolicy& other)
    : type(other.type),
      header_name(other.header_name),
      regex_substitution(other.regex_substitution) {
  if (other.regex != nullptr) {
    regex =
        absl::make_unique<RE2>(other.regex->pattern(), other.regex->options());
  }
}

XdsApi::Route::HashPolicy& XdsApi::Route::HashPolicy::operator=(
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

XdsApi::Route::HashPolicy::HashPolicy(HashPolicy&& other) noexcept
    : type(other.type),
      header_name(std::move(other.header_name)),
      regex(std::move(other.regex)),
      regex_substitution(std::move(other.regex_substitution)) {}

XdsApi::Route::HashPolicy& XdsApi::Route::HashPolicy::operator=(
    HashPolicy&& other) noexcept {
  type = other.type;
  header_name = std::move(other.header_name);
  regex = std::move(other.regex);
  regex_substitution = std::move(other.regex_substitution);
  return *this;
}

bool XdsApi::Route::HashPolicy::HashPolicy::operator==(
    const HashPolicy& other) const {
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

std::string XdsApi::Route::HashPolicy::ToString() const {
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
// XdsApi::Route
//

std::string XdsApi::Route::Matchers::ToString() const {
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

std::string XdsApi::Route::ClusterWeight::ToString() const {
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

std::string XdsApi::Route::ToString() const {
  std::vector<std::string> contents;
  contents.push_back(matchers.ToString());
  for (const HashPolicy& hash_policy : hash_policies) {
    contents.push_back(absl::StrCat("hash_policy=", hash_policy.ToString()));
  }
  if (!cluster_name.empty()) {
    contents.push_back(absl::StrFormat("Cluster name: %s", cluster_name));
  }
  for (const ClusterWeight& cluster_weight : weighted_clusters) {
    contents.push_back(cluster_weight.ToString());
  }
  if (max_stream_duration.has_value()) {
    contents.push_back(max_stream_duration->ToString());
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
// XdsApi::RdsUpdate
//

std::string XdsApi::RdsUpdate::ToString() const {
  std::vector<std::string> vhosts;
  for (const VirtualHost& vhost : virtual_hosts) {
    vhosts.push_back(
        absl::StrCat("vhost={\n"
                     "  domains=[",
                     absl::StrJoin(vhost.domains, ", "),
                     "]\n"
                     "  routes=[\n"));
    for (const XdsApi::Route& route : vhost.routes) {
      vhosts.push_back("    {\n");
      vhosts.push_back(route.ToString());
      vhosts.push_back("\n    }\n");
    }
    vhosts.push_back("  ]\n");
    vhosts.push_back("  typed_per_filter_config={\n");
    for (const auto& p : vhost.typed_per_filter_config) {
      const std::string& name = p.first;
      const auto& config = p.second;
      vhosts.push_back(
          absl::StrCat("    ", name, "=", config.ToString(), "\n"));
    }
    vhosts.push_back("  }\n");
    vhosts.push_back("]\n");
  }
  return absl::StrJoin(vhosts, "");
}

namespace {

// Better match type has smaller value.
enum MatchType {
  EXACT_MATCH,
  SUFFIX_MATCH,
  PREFIX_MATCH,
  UNIVERSE_MATCH,
  INVALID_MATCH,
};

// Returns true if match succeeds.
bool DomainMatch(MatchType match_type, const std::string& domain_pattern_in,
                 const std::string& expected_host_name_in) {
  // Normalize the args to lower-case. Domain matching is case-insensitive.
  std::string domain_pattern = domain_pattern_in;
  std::string expected_host_name = expected_host_name_in;
  std::transform(domain_pattern.begin(), domain_pattern.end(),
                 domain_pattern.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  std::transform(expected_host_name.begin(), expected_host_name.end(),
                 expected_host_name.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (match_type == EXACT_MATCH) {
    return domain_pattern == expected_host_name;
  } else if (match_type == SUFFIX_MATCH) {
    // Asterisk must match at least one char.
    if (expected_host_name.size() < domain_pattern.size()) return false;
    absl::string_view pattern_suffix(domain_pattern.c_str() + 1);
    absl::string_view host_suffix(expected_host_name.c_str() +
                                  expected_host_name.size() -
                                  pattern_suffix.size());
    return pattern_suffix == host_suffix;
  } else if (match_type == PREFIX_MATCH) {
    // Asterisk must match at least one char.
    if (expected_host_name.size() < domain_pattern.size()) return false;
    absl::string_view pattern_prefix(domain_pattern.c_str(),
                                     domain_pattern.size() - 1);
    absl::string_view host_prefix(expected_host_name.c_str(),
                                  pattern_prefix.size());
    return pattern_prefix == host_prefix;
  } else {
    return match_type == UNIVERSE_MATCH;
  }
}

MatchType DomainPatternMatchType(const std::string& domain_pattern) {
  if (domain_pattern.empty()) return INVALID_MATCH;
  if (domain_pattern.find('*') == std::string::npos) return EXACT_MATCH;
  if (domain_pattern == "*") return UNIVERSE_MATCH;
  if (domain_pattern[0] == '*') return SUFFIX_MATCH;
  if (domain_pattern[domain_pattern.size() - 1] == '*') return PREFIX_MATCH;
  return INVALID_MATCH;
}

}  // namespace

XdsApi::RdsUpdate::VirtualHost* XdsApi::RdsUpdate::FindVirtualHostForDomain(
    const std::string& domain) {
  // Find the best matched virtual host.
  // The search order for 4 groups of domain patterns:
  //   1. Exact match.
  //   2. Suffix match (e.g., "*ABC").
  //   3. Prefix match (e.g., "ABC*").
  //   4. Universe match (i.e., "*").
  // Within each group, longest match wins.
  // If the same best matched domain pattern appears in multiple virtual hosts,
  // the first matched virtual host wins.
  VirtualHost* target_vhost = nullptr;
  MatchType best_match_type = INVALID_MATCH;
  size_t longest_match = 0;
  // Check each domain pattern in each virtual host to determine the best
  // matched virtual host.
  for (VirtualHost& vhost : virtual_hosts) {
    for (const std::string& domain_pattern : vhost.domains) {
      // Check the match type first. Skip the pattern if it's not better than
      // current match.
      const MatchType match_type = DomainPatternMatchType(domain_pattern);
      // This should be caught by RouteConfigParse().
      GPR_ASSERT(match_type != INVALID_MATCH);
      if (match_type > best_match_type) continue;
      if (match_type == best_match_type &&
          domain_pattern.size() <= longest_match) {
        continue;
      }
      // Skip if match fails.
      if (!DomainMatch(match_type, domain_pattern, domain)) continue;
      // Choose this match.
      target_vhost = &vhost;
      best_match_type = match_type;
      longest_match = domain_pattern.size();
      if (best_match_type == EXACT_MATCH) break;
    }
    if (best_match_type == EXACT_MATCH) break;
  }
  return target_vhost;
}

//
// XdsApi::CommonTlsContext::CertificateValidationContext
//

std::string XdsApi::CommonTlsContext::CertificateValidationContext::ToString()
    const {
  std::vector<std::string> contents;
  for (const auto& match : match_subject_alt_names) {
    contents.push_back(match.ToString());
  }
  return absl::StrFormat("{match_subject_alt_names=[%s]}",
                         absl::StrJoin(contents, ", "));
}

bool XdsApi::CommonTlsContext::CertificateValidationContext::Empty() const {
  return match_subject_alt_names.empty();
}

//
// XdsApi::CommonTlsContext::CertificateValidationContext
//

std::string XdsApi::CommonTlsContext::CertificateProviderInstance::ToString()
    const {
  absl::InlinedVector<std::string, 2> contents;
  if (!instance_name.empty()) {
    contents.push_back(absl::StrFormat("instance_name=%s", instance_name));
  }
  if (!certificate_name.empty()) {
    contents.push_back(
        absl::StrFormat("certificate_name=%s", certificate_name));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

bool XdsApi::CommonTlsContext::CertificateProviderInstance::Empty() const {
  return instance_name.empty() && certificate_name.empty();
}

//
// XdsApi::CommonTlsContext::CombinedCertificateValidationContext
//

std::string
XdsApi::CommonTlsContext::CombinedCertificateValidationContext::ToString()
    const {
  absl::InlinedVector<std::string, 2> contents;
  if (!default_validation_context.Empty()) {
    contents.push_back(absl::StrFormat("default_validation_context=%s",
                                       default_validation_context.ToString()));
  }
  if (!validation_context_certificate_provider_instance.Empty()) {
    contents.push_back(absl::StrFormat(
        "validation_context_certificate_provider_instance=%s",
        validation_context_certificate_provider_instance.ToString()));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

bool XdsApi::CommonTlsContext::CombinedCertificateValidationContext::Empty()
    const {
  return default_validation_context.Empty() &&
         validation_context_certificate_provider_instance.Empty();
}

//
// XdsApi::CommonTlsContext
//

std::string XdsApi::CommonTlsContext::ToString() const {
  absl::InlinedVector<std::string, 2> contents;
  if (!tls_certificate_certificate_provider_instance.Empty()) {
    contents.push_back(absl::StrFormat(
        "tls_certificate_certificate_provider_instance=%s",
        tls_certificate_certificate_provider_instance.ToString()));
  }
  if (!combined_validation_context.Empty()) {
    contents.push_back(absl::StrFormat("combined_validation_context=%s",
                                       combined_validation_context.ToString()));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

bool XdsApi::CommonTlsContext::Empty() const {
  return tls_certificate_certificate_provider_instance.Empty() &&
         combined_validation_context.Empty();
}

//
// XdsApi::DownstreamTlsContext
//

std::string XdsApi::DownstreamTlsContext::ToString() const {
  return absl::StrFormat("common_tls_context=%s, require_client_certificate=%s",
                         common_tls_context.ToString(),
                         require_client_certificate ? "true" : "false");
}

bool XdsApi::DownstreamTlsContext::Empty() const {
  return common_tls_context.Empty();
}

//
// XdsApi::LdsUpdate::HttpConnectionManager
//

std::string XdsApi::LdsUpdate::HttpConnectionManager::ToString() const {
  absl::InlinedVector<std::string, 4> contents;
  contents.push_back(absl::StrFormat(
      "route_config_name=%s",
      !route_config_name.empty() ? route_config_name.c_str() : "<inlined>"));
  contents.push_back(absl::StrFormat("http_max_stream_duration=%s",
                                     http_max_stream_duration.ToString()));
  if (rds_update.has_value()) {
    contents.push_back(
        absl::StrFormat("rds_update=%s", rds_update->ToString()));
  }
  if (!http_filters.empty()) {
    std::vector<std::string> filter_strings;
    for (const auto& http_filter : http_filters) {
      filter_strings.push_back(http_filter.ToString());
    }
    contents.push_back(absl::StrCat("http_filters=[",
                                    absl::StrJoin(filter_strings, ", "), "]"));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsApi::LdsUpdate::HttpFilter
//

std::string XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter::ToString()
    const {
  return absl::StrCat("{name=", name, ", config=", config.ToString(), "}");
}

//
// XdsApi::LdsUpdate::FilterChainData
//

std::string XdsApi::LdsUpdate::FilterChainData::ToString() const {
  return absl::StrCat(
      "{downstream_tls_context=", downstream_tls_context.ToString(),
      " http_connection_manager=", http_connection_manager.ToString(), "}");
}

//
// XdsApi::LdsUpdate::FilterChainMap::CidrRange
//

std::string XdsApi::LdsUpdate::FilterChainMap::CidrRange::ToString() const {
  return absl::StrCat(
      "{address_prefix=", grpc_sockaddr_to_string(&address, false),
      ", prefix_len=", prefix_len, "}");
}

//
// FilterChain
//

struct FilterChain {
  struct FilterChainMatch {
    uint32_t destination_port = 0;
    std::vector<XdsApi::LdsUpdate::FilterChainMap::CidrRange> prefix_ranges;
    XdsApi::LdsUpdate::FilterChainMap::ConnectionSourceType source_type =
        XdsApi::LdsUpdate::FilterChainMap::ConnectionSourceType::kAny;
    std::vector<XdsApi::LdsUpdate::FilterChainMap::CidrRange>
        source_prefix_ranges;
    std::vector<uint32_t> source_ports;
    std::vector<std::string> server_names;
    std::string transport_protocol;
    std::vector<std::string> application_protocols;

    std::string ToString() const;
  } filter_chain_match;

  std::shared_ptr<XdsApi::LdsUpdate::FilterChainData> filter_chain_data;
};

std::string FilterChain::FilterChainMatch::ToString() const {
  absl::InlinedVector<std::string, 8> contents;
  if (destination_port != 0) {
    contents.push_back(absl::StrCat("destination_port=", destination_port));
  }
  if (!prefix_ranges.empty()) {
    std::vector<std::string> prefix_ranges_content;
    for (const auto& range : prefix_ranges) {
      prefix_ranges_content.push_back(range.ToString());
    }
    contents.push_back(absl::StrCat(
        "prefix_ranges={", absl::StrJoin(prefix_ranges_content, ", "), "}"));
  }
  if (source_type == XdsApi::LdsUpdate::FilterChainMap::ConnectionSourceType::
                         kSameIpOrLoopback) {
    contents.push_back("source_type=SAME_IP_OR_LOOPBACK");
  } else if (source_type == XdsApi::LdsUpdate::FilterChainMap::
                                ConnectionSourceType::kExternal) {
    contents.push_back("source_type=EXTERNAL");
  }
  if (!source_prefix_ranges.empty()) {
    std::vector<std::string> source_prefix_ranges_content;
    for (const auto& range : source_prefix_ranges) {
      source_prefix_ranges_content.push_back(range.ToString());
    }
    contents.push_back(
        absl::StrCat("source_prefix_ranges={",
                     absl::StrJoin(source_prefix_ranges_content, ", "), "}"));
  }
  if (!source_ports.empty()) {
    contents.push_back(
        absl::StrCat("source_ports={", absl::StrJoin(source_ports, ", "), "}"));
  }
  if (!server_names.empty()) {
    contents.push_back(
        absl::StrCat("server_names={", absl::StrJoin(server_names, ", "), "}"));
  }
  if (!transport_protocol.empty()) {
    contents.push_back(absl::StrCat("transport_protocol=", transport_protocol));
  }
  if (!application_protocols.empty()) {
    contents.push_back(absl::StrCat("application_protocols={",
                                    absl::StrJoin(application_protocols, ", "),
                                    "}"));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsApi::LdsUpdate::FilterChainMap
//

std::string XdsApi::LdsUpdate::FilterChainMap::ToString() const {
  std::vector<std::string> contents;
  for (const auto& destination_ip : destination_ip_vector) {
    for (int source_type = 0; source_type < 3; ++source_type) {
      for (const auto& source_ip :
           destination_ip.source_types_array[source_type]) {
        for (const auto& source_port_pair : source_ip.ports_map) {
          FilterChain::FilterChainMatch filter_chain_match;
          if (destination_ip.prefix_range.has_value()) {
            filter_chain_match.prefix_ranges.push_back(
                *destination_ip.prefix_range);
          }
          filter_chain_match.source_type = static_cast<
              XdsApi::LdsUpdate::FilterChainMap::ConnectionSourceType>(
              source_type);
          if (source_ip.prefix_range.has_value()) {
            filter_chain_match.source_prefix_ranges.push_back(
                *source_ip.prefix_range);
          }
          if (source_port_pair.first != 0) {
            filter_chain_match.source_ports.push_back(source_port_pair.first);
          }
          contents.push_back(absl::StrCat(
              "{filter_chain_match=", filter_chain_match.ToString(),
              ", filter_chain=", source_port_pair.second.data->ToString(),
              "}"));
        }
      }
    }
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsApi::LdsUpdate
//

std::string XdsApi::LdsUpdate::ToString() const {
  absl::InlinedVector<std::string, 4> contents;
  if (type == ListenerType::kTcpListener) {
    contents.push_back(absl::StrCat("address=", address));
    contents.push_back(
        absl::StrCat("filter_chain_map=", filter_chain_map.ToString()));
    if (default_filter_chain.has_value()) {
      contents.push_back(absl::StrCat("default_filter_chain=",
                                      default_filter_chain->ToString()));
    }
  } else if (type == ListenerType::kHttpApiListener) {
    contents.push_back(absl::StrFormat("http_connection_manager=%s",
                                       http_connection_manager.ToString()));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsApi::CdsUpdate
//

std::string XdsApi::CdsUpdate::ToString() const {
  absl::InlinedVector<std::string, 4> contents;
  if (!eds_service_name.empty()) {
    contents.push_back(
        absl::StrFormat("eds_service_name=%s", eds_service_name));
  }
  if (!common_tls_context.Empty()) {
    contents.push_back(absl::StrFormat("common_tls_context=%s",
                                       common_tls_context.ToString()));
  }
  if (lrs_load_reporting_server_name.has_value()) {
    contents.push_back(absl::StrFormat("lrs_load_reporting_server_name=%s",
                                       lrs_load_reporting_server_name.value()));
  }
  contents.push_back(
      absl::StrFormat("max_concurrent_requests=%d", max_concurrent_requests));
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsApi::EdsUpdate
//

std::string XdsApi::EdsUpdate::Priority::Locality::ToString() const {
  std::vector<std::string> endpoint_strings;
  for (const ServerAddress& endpoint : endpoints) {
    endpoint_strings.emplace_back(endpoint.ToString());
  }
  return absl::StrCat("{name=", name->AsHumanReadableString(),
                      ", lb_weight=", lb_weight, ", endpoints=[",
                      absl::StrJoin(endpoint_strings, ", "), "]}");
}

bool XdsApi::EdsUpdate::Priority::operator==(const Priority& other) const {
  if (localities.size() != other.localities.size()) return false;
  auto it1 = localities.begin();
  auto it2 = other.localities.begin();
  while (it1 != localities.end()) {
    if (*it1->first != *it2->first) return false;
    if (it1->second != it2->second) return false;
    ++it1;
    ++it2;
  }
  return true;
}

std::string XdsApi::EdsUpdate::Priority::ToString() const {
  std::vector<std::string> locality_strings;
  for (const auto& p : localities) {
    locality_strings.emplace_back(p.second.ToString());
  }
  return absl::StrCat("[", absl::StrJoin(locality_strings, ", "), "]");
}

bool XdsApi::EdsUpdate::DropConfig::ShouldDrop(
    const std::string** category_name) const {
  for (size_t i = 0; i < drop_category_list_.size(); ++i) {
    const auto& drop_category = drop_category_list_[i];
    // Generate a random number in [0, 1000000).
    const uint32_t random = static_cast<uint32_t>(rand()) % 1000000;
    if (random < drop_category.parts_per_million) {
      *category_name = &drop_category.name;
      return true;
    }
  }
  return false;
}

std::string XdsApi::EdsUpdate::DropConfig::ToString() const {
  std::vector<std::string> category_strings;
  for (const DropCategory& category : drop_category_list_) {
    category_strings.emplace_back(
        absl::StrCat(category.name, "=", category.parts_per_million));
  }
  return absl::StrCat("{[", absl::StrJoin(category_strings, ", "),
                      "], drop_all=", drop_all_, "}");
}

std::string XdsApi::EdsUpdate::ToString() const {
  std::vector<std::string> priority_strings;
  for (size_t i = 0; i < priorities.size(); ++i) {
    const Priority& priority = priorities[i];
    priority_strings.emplace_back(
        absl::StrCat("priority ", i, ": ", priority.ToString()));
  }
  return absl::StrCat("priorities=[", absl::StrJoin(priority_strings, ", "),
                      "], drop_config=", drop_config->ToString());
}

//
// XdsApi
//

const char* XdsApi::kLdsTypeUrl =
    "type.googleapis.com/envoy.config.listener.v3.Listener";
const char* XdsApi::kRdsTypeUrl =
    "type.googleapis.com/envoy.config.route.v3.RouteConfiguration";
const char* XdsApi::kCdsTypeUrl =
    "type.googleapis.com/envoy.config.cluster.v3.Cluster";
const char* XdsApi::kEdsTypeUrl =
    "type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment";

namespace {

const char* kLdsV2TypeUrl = "type.googleapis.com/envoy.api.v2.Listener";
const char* kRdsV2TypeUrl =
    "type.googleapis.com/envoy.api.v2.RouteConfiguration";
const char* kCdsV2TypeUrl = "type.googleapis.com/envoy.api.v2.Cluster";
const char* kEdsV2TypeUrl =
    "type.googleapis.com/envoy.api.v2.ClusterLoadAssignment";

bool IsLds(absl::string_view type_url, bool* is_v2 = nullptr) {
  if (type_url == XdsApi::kLdsTypeUrl) return true;
  if (type_url == kLdsV2TypeUrl) {
    if (is_v2 != nullptr) *is_v2 = true;
    return true;
  }
  return false;
}

bool IsRds(absl::string_view type_url) {
  return type_url == XdsApi::kRdsTypeUrl || type_url == kRdsV2TypeUrl;
}

bool IsCds(absl::string_view type_url) {
  return type_url == XdsApi::kCdsTypeUrl || type_url == kCdsV2TypeUrl;
}

bool IsEds(absl::string_view type_url) {
  return type_url == XdsApi::kEdsTypeUrl || type_url == kEdsV2TypeUrl;
}

}  // namespace

XdsApi::XdsApi(XdsClient* client, TraceFlag* tracer,
               const XdsBootstrap::Node* node)
    : client_(client),
      tracer_(tracer),
      node_(node),
      build_version_(absl::StrCat("gRPC C-core ", GPR_PLATFORM_STRING, " ",
                                  grpc_version_string())),
      user_agent_name_(absl::StrCat("gRPC C-core ", GPR_PLATFORM_STRING)) {
  // Populate upb symtab with xDS proto messages that we want to print
  // properly in logs.
  // Note: This won't actually work properly until upb adds support for
  // Any fields in textproto printing (internal b/178821188).
  envoy_config_listener_v3_Listener_getmsgdef(symtab_.ptr());
  envoy_config_route_v3_RouteConfiguration_getmsgdef(symtab_.ptr());
  envoy_config_cluster_v3_Cluster_getmsgdef(symtab_.ptr());
  envoy_extensions_clusters_aggregate_v3_ClusterConfig_getmsgdef(symtab_.ptr());
  envoy_config_cluster_v3_Cluster_getmsgdef(symtab_.ptr());
  envoy_config_endpoint_v3_ClusterLoadAssignment_getmsgdef(symtab_.ptr());
  envoy_extensions_transport_sockets_tls_v3_UpstreamTlsContext_getmsgdef(
      symtab_.ptr());
  envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_getmsgdef(
      symtab_.ptr());
  // Load HTTP filter proto messages into the upb symtab.
  XdsHttpFilterRegistry::PopulateSymtab(symtab_.ptr());
}

namespace {

struct EncodingContext {
  XdsClient* client;
  TraceFlag* tracer;
  upb_symtab* symtab;
  upb_arena* arena;
  bool use_v3;
};

// Works for both std::string and absl::string_view.
template <typename T>
inline upb_strview StdStringToUpbString(const T& str) {
  return upb_strview_make(str.data(), str.size());
}

void PopulateMetadataValue(const EncodingContext& context,
                           google_protobuf_Value* value_pb, const Json& value);

void PopulateListValue(const EncodingContext& context,
                       google_protobuf_ListValue* list_value,
                       const Json::Array& values) {
  for (const auto& value : values) {
    auto* value_pb =
        google_protobuf_ListValue_add_values(list_value, context.arena);
    PopulateMetadataValue(context, value_pb, value);
  }
}

void PopulateMetadata(const EncodingContext& context,
                      google_protobuf_Struct* metadata_pb,
                      const Json::Object& metadata) {
  for (const auto& p : metadata) {
    google_protobuf_Value* value = google_protobuf_Value_new(context.arena);
    PopulateMetadataValue(context, value, p.second);
    google_protobuf_Struct_fields_set(
        metadata_pb, StdStringToUpbString(p.first), value, context.arena);
  }
}

void PopulateMetadataValue(const EncodingContext& context,
                           google_protobuf_Value* value_pb, const Json& value) {
  switch (value.type()) {
    case Json::Type::JSON_NULL:
      google_protobuf_Value_set_null_value(value_pb, 0);
      break;
    case Json::Type::NUMBER:
      google_protobuf_Value_set_number_value(
          value_pb, strtod(value.string_value().c_str(), nullptr));
      break;
    case Json::Type::STRING:
      google_protobuf_Value_set_string_value(
          value_pb, StdStringToUpbString(value.string_value()));
      break;
    case Json::Type::JSON_TRUE:
      google_protobuf_Value_set_bool_value(value_pb, true);
      break;
    case Json::Type::JSON_FALSE:
      google_protobuf_Value_set_bool_value(value_pb, false);
      break;
    case Json::Type::OBJECT: {
      google_protobuf_Struct* struct_value =
          google_protobuf_Value_mutable_struct_value(value_pb, context.arena);
      PopulateMetadata(context, struct_value, value.object_value());
      break;
    }
    case Json::Type::ARRAY: {
      google_protobuf_ListValue* list_value =
          google_protobuf_Value_mutable_list_value(value_pb, context.arena);
      PopulateListValue(context, list_value, value.array_value());
      break;
    }
  }
}

// Helper functions to manually do protobuf string encoding, so that we
// can populate the node build_version field that was removed in v3.
std::string EncodeVarint(uint64_t val) {
  std::string data;
  do {
    uint8_t byte = val & 0x7fU;
    val >>= 7;
    if (val) byte |= 0x80U;
    data += byte;
  } while (val);
  return data;
}
std::string EncodeTag(uint32_t field_number, uint8_t wire_type) {
  return EncodeVarint((field_number << 3) | wire_type);
}
std::string EncodeStringField(uint32_t field_number, const std::string& str) {
  static const uint8_t kDelimitedWireType = 2;
  return EncodeTag(field_number, kDelimitedWireType) +
         EncodeVarint(str.size()) + str;
}

void PopulateBuildVersion(const EncodingContext& context,
                          envoy_config_core_v3_Node* node_msg,
                          const std::string& build_version) {
  std::string encoded_build_version = EncodeStringField(5, build_version);
  // TODO(roth): This should use upb_msg_addunknown(), but that API is
  // broken in the current version of upb, so we're using the internal
  // API for now.  Change this once we upgrade to a version of upb that
  // fixes this bug.
  _upb_msg_addunknown(node_msg, encoded_build_version.data(),
                      encoded_build_version.size(), context.arena);
}

void PopulateNode(const EncodingContext& context,
                  const XdsBootstrap::Node* node,
                  const std::string& build_version,
                  const std::string& user_agent_name,
                  envoy_config_core_v3_Node* node_msg) {
  if (node != nullptr) {
    if (!node->id.empty()) {
      envoy_config_core_v3_Node_set_id(node_msg,
                                       StdStringToUpbString(node->id));
    }
    if (!node->cluster.empty()) {
      envoy_config_core_v3_Node_set_cluster(
          node_msg, StdStringToUpbString(node->cluster));
    }
    if (!node->metadata.object_value().empty()) {
      google_protobuf_Struct* metadata =
          envoy_config_core_v3_Node_mutable_metadata(node_msg, context.arena);
      PopulateMetadata(context, metadata, node->metadata.object_value());
    }
    if (!node->locality_region.empty() || !node->locality_zone.empty() ||
        !node->locality_sub_zone.empty()) {
      envoy_config_core_v3_Locality* locality =
          envoy_config_core_v3_Node_mutable_locality(node_msg, context.arena);
      if (!node->locality_region.empty()) {
        envoy_config_core_v3_Locality_set_region(
            locality, StdStringToUpbString(node->locality_region));
      }
      if (!node->locality_zone.empty()) {
        envoy_config_core_v3_Locality_set_zone(
            locality, StdStringToUpbString(node->locality_zone));
      }
      if (!node->locality_sub_zone.empty()) {
        envoy_config_core_v3_Locality_set_sub_zone(
            locality, StdStringToUpbString(node->locality_sub_zone));
      }
    }
  }
  if (!context.use_v3) {
    PopulateBuildVersion(context, node_msg, build_version);
  }
  envoy_config_core_v3_Node_set_user_agent_name(
      node_msg, StdStringToUpbString(user_agent_name));
  envoy_config_core_v3_Node_set_user_agent_version(
      node_msg, upb_strview_makez(grpc_version_string()));
  envoy_config_core_v3_Node_add_client_features(
      node_msg, upb_strview_makez("envoy.lb.does_not_support_overprovisioning"),
      context.arena);
}

inline absl::string_view UpbStringToAbsl(const upb_strview& str) {
  return absl::string_view(str.data, str.size);
}

inline std::string UpbStringToStdString(const upb_strview& str) {
  return std::string(str.data, str.size);
}

void MaybeLogDiscoveryRequest(
    const EncodingContext& context,
    const envoy_service_discovery_v3_DiscoveryRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_service_discovery_v3_DiscoveryRequest_getmsgdef(context.symtab);
    char buf[10240];
    upb_text_encode(request, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] constructed ADS request: %s",
            context.client, buf);
  }
}

grpc_slice SerializeDiscoveryRequest(
    const EncodingContext& context,
    envoy_service_discovery_v3_DiscoveryRequest* request) {
  size_t output_length;
  char* output = envoy_service_discovery_v3_DiscoveryRequest_serialize(
      request, context.arena, &output_length);
  return grpc_slice_from_copied_buffer(output, output_length);
}

absl::string_view TypeUrlExternalToInternal(bool use_v3,
                                            const std::string& type_url) {
  if (!use_v3) {
    if (type_url == XdsApi::kLdsTypeUrl) {
      return kLdsV2TypeUrl;
    }
    if (type_url == XdsApi::kRdsTypeUrl) {
      return kRdsV2TypeUrl;
    }
    if (type_url == XdsApi::kCdsTypeUrl) {
      return kCdsV2TypeUrl;
    }
    if (type_url == XdsApi::kEdsTypeUrl) {
      return kEdsV2TypeUrl;
    }
  }
  return type_url;
}

}  // namespace

grpc_slice XdsApi::CreateAdsRequest(
    const XdsBootstrap::XdsServer& server, const std::string& type_url,
    const std::set<absl::string_view>& resource_names,
    const std::string& version, const std::string& nonce,
    grpc_error_handle error, bool populate_node) {
  upb::Arena arena;
  const EncodingContext context = {client_, tracer_, symtab_.ptr(), arena.ptr(),
                                   server.ShouldUseV3()};
  // Create a request.
  envoy_service_discovery_v3_DiscoveryRequest* request =
      envoy_service_discovery_v3_DiscoveryRequest_new(arena.ptr());
  // Set type_url.
  absl::string_view real_type_url =
      TypeUrlExternalToInternal(server.ShouldUseV3(), type_url);
  envoy_service_discovery_v3_DiscoveryRequest_set_type_url(
      request, StdStringToUpbString(real_type_url));
  // Set version_info.
  if (!version.empty()) {
    envoy_service_discovery_v3_DiscoveryRequest_set_version_info(
        request, StdStringToUpbString(version));
  }
  // Set nonce.
  if (!nonce.empty()) {
    envoy_service_discovery_v3_DiscoveryRequest_set_response_nonce(
        request, StdStringToUpbString(nonce));
  }
  // Set error_detail if it's a NACK.
  std::string error_string_storage;
  if (error != GRPC_ERROR_NONE) {
    google_rpc_Status* error_detail =
        envoy_service_discovery_v3_DiscoveryRequest_mutable_error_detail(
            request, arena.ptr());
    // Hard-code INVALID_ARGUMENT as the status code.
    // TODO(roth): If at some point we decide we care about this value,
    // we could attach a status code to the individual errors where we
    // generate them in the parsing code, and then use that here.
    google_rpc_Status_set_code(error_detail, GRPC_STATUS_INVALID_ARGUMENT);
    // Error description comes from the error that was passed in.
    error_string_storage = grpc_error_std_string(error);
    upb_strview error_description = StdStringToUpbString(error_string_storage);
    google_rpc_Status_set_message(error_detail, error_description);
    GRPC_ERROR_UNREF(error);
  }
  // Populate node.
  if (populate_node) {
    envoy_config_core_v3_Node* node_msg =
        envoy_service_discovery_v3_DiscoveryRequest_mutable_node(request,
                                                                 arena.ptr());
    PopulateNode(context, node_, build_version_, user_agent_name_, node_msg);
  }
  // Add resource_names.
  for (const auto& resource_name : resource_names) {
    envoy_service_discovery_v3_DiscoveryRequest_add_resource_names(
        request, StdStringToUpbString(resource_name), arena.ptr());
  }
  MaybeLogDiscoveryRequest(context, request);
  return SerializeDiscoveryRequest(context, request);
}

namespace {

void MaybeLogDiscoveryResponse(
    const EncodingContext& context,
    const envoy_service_discovery_v3_DiscoveryResponse* response) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_service_discovery_v3_DiscoveryResponse_getmsgdef(context.symtab);
    char buf[10240];
    upb_text_encode(response, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] received response: %s", context.client,
            buf);
  }
}

void MaybeLogHttpConnectionManager(
    const EncodingContext& context,
    const envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager*
        http_connection_manager_config) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_getmsgdef(
            context.symtab);
    char buf[10240];
    upb_text_encode(http_connection_manager_config, msg_type, nullptr, 0, buf,
                    sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] HttpConnectionManager: %s",
            context.client, buf);
  }
}

void MaybeLogRouteConfiguration(
    const EncodingContext& context,
    const envoy_config_route_v3_RouteConfiguration* route_config) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_config_route_v3_RouteConfiguration_getmsgdef(context.symtab);
    char buf[10240];
    upb_text_encode(route_config, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] RouteConfiguration: %s", context.client,
            buf);
  }
}

void MaybeLogCluster(const EncodingContext& context,
                     const envoy_config_cluster_v3_Cluster* cluster) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_config_cluster_v3_Cluster_getmsgdef(context.symtab);
    char buf[10240];
    upb_text_encode(cluster, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] Cluster: %s", context.client, buf);
  }
}

void MaybeLogClusterLoadAssignment(
    const EncodingContext& context,
    const envoy_config_endpoint_v3_ClusterLoadAssignment* cla) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_config_endpoint_v3_ClusterLoadAssignment_getmsgdef(
            context.symtab);
    char buf[10240];
    upb_text_encode(cla, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] ClusterLoadAssignment: %s",
            context.client, buf);
  }
}

grpc_error_handle RoutePathMatchParse(
    const envoy_config_route_v3_RouteMatch* match, XdsApi::Route* route,
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
        return GRPC_ERROR_NONE;
      }
      std::vector<absl::string_view> prefix_elements =
          absl::StrSplit(prefix.substr(1), absl::MaxSplits('/', 2));
      if (prefix_elements.size() > 2) {
        // Prefix cannot have more than 2 slashes.
        *ignore_route = true;
        return GRPC_ERROR_NONE;
      } else if (prefix_elements.size() == 2 && prefix_elements[0].empty()) {
        // Prefix contains empty string between the 2 slashes
        *ignore_route = true;
        return GRPC_ERROR_NONE;
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
      return GRPC_ERROR_NONE;
    }
    if (path[0] != '/') {
      // Path which does not start with a / will never match anything, so
      // ignore this route.
      *ignore_route = true;
      return GRPC_ERROR_NONE;
    }
    std::vector<absl::string_view> path_elements =
        absl::StrSplit(path.substr(1), absl::MaxSplits('/', 2));
    if (path_elements.size() != 2) {
      // Path not in the required format of /service/method will never match
      // anything, so ignore this route.
      *ignore_route = true;
      return GRPC_ERROR_NONE;
    } else if (path_elements[0].empty()) {
      // Path contains empty service name will never match anything, so ignore
      // this route.
      *ignore_route = true;
      return GRPC_ERROR_NONE;
    } else if (path_elements[1].empty()) {
      // Path contains empty method name will never match anything, so ignore
      // this route.
      *ignore_route = true;
      return GRPC_ERROR_NONE;
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
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Invalid route path specifier specified.");
  }
  absl::StatusOr<StringMatcher> string_matcher =
      StringMatcher::Create(type, match_string, case_sensitive);
  if (!string_matcher.ok()) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("path matcher: ", string_matcher.status().message())
            .c_str());
    ;
  }
  route->matchers.path_matcher = std::move(string_matcher.value());
  return GRPC_ERROR_NONE;
}

grpc_error_handle RouteHeaderMatchersParse(
    const envoy_config_route_v3_RouteMatch* match, XdsApi::Route* route) {
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
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Invalid route header matcher specified.");
    }
    bool invert_match =
        envoy_config_route_v3_HeaderMatcher_invert_match(header);
    absl::StatusOr<HeaderMatcher> header_matcher =
        HeaderMatcher::Create(name, type, match_string, range_start, range_end,
                              present_match, invert_match);
    if (!header_matcher.ok()) {
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("header matcher: ", header_matcher.status().message())
              .c_str());
    }
    route->matchers.header_matchers.emplace_back(
        std::move(header_matcher.value()));
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle RouteRuntimeFractionParse(
    const envoy_config_route_v3_RouteMatch* match, XdsApi::Route* route) {
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
          return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "Unknown denominator type");
      }
      route->matchers.fraction_per_million = numerator;
    }
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle ExtractHttpFilterTypeName(const EncodingContext& context,
                                            const google_protobuf_Any* any,
                                            absl::string_view* filter_type) {
  *filter_type = UpbStringToAbsl(google_protobuf_Any_type_url(any));
  if (*filter_type == "type.googleapis.com/udpa.type.v1.TypedStruct") {
    upb_strview any_value = google_protobuf_Any_value(any);
    const auto* typed_struct = udpa_type_v1_TypedStruct_parse(
        any_value.data, any_value.size, context.arena);
    if (typed_struct == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "could not parse TypedStruct from filter config");
    }
    *filter_type =
        UpbStringToAbsl(udpa_type_v1_TypedStruct_type_url(typed_struct));
  }
  *filter_type = absl::StripPrefix(*filter_type, "type.googleapis.com/");
  return GRPC_ERROR_NONE;
}

template <typename ParentType, typename EntryType>
grpc_error_handle ParseTypedPerFilterConfig(
    const EncodingContext& context, const ParentType* parent,
    const EntryType* (*entry_func)(const ParentType*, size_t*),
    upb_strview (*key_func)(const EntryType*),
    const google_protobuf_Any* (*value_func)(const EntryType*),
    XdsApi::TypedPerFilterConfig* typed_per_filter_config) {
  size_t filter_it = UPB_MAP_BEGIN;
  while (true) {
    const auto* filter_entry = entry_func(parent, &filter_it);
    if (filter_entry == nullptr) break;
    absl::string_view key = UpbStringToAbsl(key_func(filter_entry));
    if (key.empty()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("empty filter name in map");
    }
    const google_protobuf_Any* any = value_func(filter_entry);
    GPR_ASSERT(any != nullptr);
    absl::string_view filter_type =
        UpbStringToAbsl(google_protobuf_Any_type_url(any));
    if (filter_type.empty()) {
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("no filter config specified for filter name ", key)
              .c_str());
    }
    bool is_optional = false;
    if (filter_type ==
        "type.googleapis.com/envoy.config.route.v3.FilterConfig") {
      upb_strview any_value = google_protobuf_Any_value(any);
      const auto* filter_config = envoy_config_route_v3_FilterConfig_parse(
          any_value.data, any_value.size, context.arena);
      if (filter_config == nullptr) {
        return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("could not parse FilterConfig wrapper for ", key)
                .c_str());
      }
      is_optional =
          envoy_config_route_v3_FilterConfig_is_optional(filter_config);
      any = envoy_config_route_v3_FilterConfig_config(filter_config);
      if (any == nullptr) {
        if (is_optional) continue;
        return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("no filter config specified for filter name ", key)
                .c_str());
      }
    }
    grpc_error_handle error =
        ExtractHttpFilterTypeName(context, any, &filter_type);
    if (error != GRPC_ERROR_NONE) return error;
    const XdsHttpFilterImpl* filter_impl =
        XdsHttpFilterRegistry::GetFilterForType(filter_type);
    if (filter_impl == nullptr) {
      if (is_optional) continue;
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("no filter registered for config type ", filter_type)
              .c_str());
    }
    absl::StatusOr<XdsHttpFilterImpl::FilterConfig> filter_config =
        filter_impl->GenerateFilterConfigOverride(
            google_protobuf_Any_value(any), context.arena);
    if (!filter_config.ok()) {
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("filter config for type ", filter_type,
                       " failed to parse: ", filter_config.status().ToString())
              .c_str());
    }
    (*typed_per_filter_config)[std::string(key)] = std::move(*filter_config);
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle RouteActionParse(const EncodingContext& context,
                                   const envoy_config_route_v3_Route* route_msg,
                                   XdsApi::Route* route, bool* ignore_route) {
  if (!envoy_config_route_v3_Route_has_route(route_msg)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "No RouteAction found in route.");
  }
  const envoy_config_route_v3_RouteAction* route_action =
      envoy_config_route_v3_Route_route(route_msg);
  // Get the cluster or weighted_clusters in the RouteAction.
  if (envoy_config_route_v3_RouteAction_has_cluster(route_action)) {
    route->cluster_name = UpbStringToStdString(
        envoy_config_route_v3_RouteAction_cluster(route_action));
    if (route->cluster_name.empty()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "RouteAction cluster contains empty cluster name.");
    }
  } else if (envoy_config_route_v3_RouteAction_has_weighted_clusters(
                 route_action)) {
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
      XdsApi::Route::ClusterWeight cluster;
      cluster.name = UpbStringToStdString(
          envoy_config_route_v3_WeightedCluster_ClusterWeight_name(
              cluster_weight));
      if (cluster.name.empty()) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "RouteAction weighted_cluster cluster contains empty cluster "
            "name.");
      }
      const google_protobuf_UInt32Value* weight =
          envoy_config_route_v3_WeightedCluster_ClusterWeight_weight(
              cluster_weight);
      if (weight == nullptr) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "RouteAction weighted_cluster cluster missing weight");
      }
      cluster.weight = google_protobuf_UInt32Value_value(weight);
      if (cluster.weight == 0) continue;
      sum_of_weights += cluster.weight;
      if (context.use_v3) {
        grpc_error_handle error = ParseTypedPerFilterConfig<
            envoy_config_route_v3_WeightedCluster_ClusterWeight,
            envoy_config_route_v3_WeightedCluster_ClusterWeight_TypedPerFilterConfigEntry>(
            context, cluster_weight,
            envoy_config_route_v3_WeightedCluster_ClusterWeight_typed_per_filter_config_next,
            envoy_config_route_v3_WeightedCluster_ClusterWeight_TypedPerFilterConfigEntry_key,
            envoy_config_route_v3_WeightedCluster_ClusterWeight_TypedPerFilterConfigEntry_value,
            &cluster.typed_per_filter_config);
        if (error != GRPC_ERROR_NONE) return error;
      }
      route->weighted_clusters.emplace_back(std::move(cluster));
    }
    if (total_weight != sum_of_weights) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "RouteAction weighted_cluster has incorrect total weight");
    }
    if (route->weighted_clusters.empty()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "RouteAction weighted_cluster has no valid clusters specified.");
    }
  } else {
    // No cluster or weighted_clusters found in RouteAction, ignore this route.
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
        XdsApi::Duration duration_in_route;
        duration_in_route.seconds = google_protobuf_Duration_seconds(duration);
        duration_in_route.nanos = google_protobuf_Duration_nanos(duration);
        route->max_stream_duration = duration_in_route;
      }
    }
  }
  // Get HashPolicy from RouteAction
  if (XdsRingHashEnabled()) {
    size_t size = 0;
    const envoy_config_route_v3_RouteAction_HashPolicy* const* hash_policies =
        envoy_config_route_v3_RouteAction_hash_policy(route_action, &size);
    for (size_t i = 0; i < size; ++i) {
      const envoy_config_route_v3_RouteAction_HashPolicy* hash_policy =
          hash_policies[i];
      XdsApi::Route::HashPolicy policy;
      policy.terminal =
          envoy_config_route_v3_RouteAction_HashPolicy_terminal(hash_policy);
      const envoy_config_route_v3_RouteAction_HashPolicy_Header* header;
      const envoy_config_route_v3_RouteAction_HashPolicy_FilterState*
          filter_state;
      if ((header = envoy_config_route_v3_RouteAction_HashPolicy_header(
               hash_policy)) != nullptr) {
        policy.type = XdsApi::Route::HashPolicy::Type::HEADER;
        policy.header_name = UpbStringToStdString(
            envoy_config_route_v3_RouteAction_HashPolicy_Header_header_name(
                header));
        const struct envoy_type_matcher_v3_RegexMatchAndSubstitute*
            regex_rewrite =
                envoy_config_route_v3_RouteAction_HashPolicy_Header_regex_rewrite(
                    header);
        if (regex_rewrite == nullptr) {
          gpr_log(
              GPR_DEBUG,
              "RouteAction HashPolicy contains policy specifier Header with "
              "RegexMatchAndSubstitution but Regex is missing");
          continue;
        }
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
      } else if ((filter_state =
                      envoy_config_route_v3_RouteAction_HashPolicy_filter_state(
                          hash_policy)) != nullptr) {
        std::string key = UpbStringToStdString(
            envoy_config_route_v3_RouteAction_HashPolicy_FilterState_key(
                filter_state));
        if (key == "io.grpc.channel_id") {
          policy.type = XdsApi::Route::HashPolicy::Type::CHANNEL_ID;
        } else {
          gpr_log(GPR_DEBUG,
                  "RouteAction HashPolicy contains policy specifier "
                  "FilterState but "
                  "key is not io.grpc.channel_id.");
          continue;
        }
      } else {
        gpr_log(
            GPR_DEBUG,
            "RouteAction HashPolicy contains unsupported policy specifier.");
        continue;
      }
      route->hash_policies.emplace_back(std::move(policy));
    }
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle RouteConfigParse(
    const EncodingContext& context,
    const envoy_config_route_v3_RouteConfiguration* route_config,
    XdsApi::RdsUpdate* rds_update) {
  MaybeLogRouteConfiguration(context, route_config);
  // Get the virtual hosts.
  size_t num_virtual_hosts;
  const envoy_config_route_v3_VirtualHost* const* virtual_hosts =
      envoy_config_route_v3_RouteConfiguration_virtual_hosts(
          route_config, &num_virtual_hosts);
  for (size_t i = 0; i < num_virtual_hosts; ++i) {
    rds_update->virtual_hosts.emplace_back();
    XdsApi::RdsUpdate::VirtualHost& vhost = rds_update->virtual_hosts.back();
    // Parse domains.
    size_t domain_size;
    upb_strview const* domains = envoy_config_route_v3_VirtualHost_domains(
        virtual_hosts[i], &domain_size);
    for (size_t j = 0; j < domain_size; ++j) {
      std::string domain_pattern = UpbStringToStdString(domains[j]);
      const MatchType match_type = DomainPatternMatchType(domain_pattern);
      if (match_type == INVALID_MATCH) {
        return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("Invalid domain pattern \"", domain_pattern, "\".")
                .c_str());
      }
      vhost.domains.emplace_back(std::move(domain_pattern));
    }
    if (vhost.domains.empty()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("VirtualHost has no domains");
    }
    // Parse typed_per_filter_config.
    if (context.use_v3) {
      grpc_error_handle error = ParseTypedPerFilterConfig<
          envoy_config_route_v3_VirtualHost,
          envoy_config_route_v3_VirtualHost_TypedPerFilterConfigEntry>(
          context, virtual_hosts[i],
          envoy_config_route_v3_VirtualHost_typed_per_filter_config_next,
          envoy_config_route_v3_VirtualHost_TypedPerFilterConfigEntry_key,
          envoy_config_route_v3_VirtualHost_TypedPerFilterConfigEntry_value,
          &vhost.typed_per_filter_config);
      if (error != GRPC_ERROR_NONE) return error;
    }
    // Parse routes.
    size_t num_routes;
    const envoy_config_route_v3_Route* const* routes =
        envoy_config_route_v3_VirtualHost_routes(virtual_hosts[i], &num_routes);
    if (num_routes < 1) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "No route found in the virtual host.");
    }
    // Loop over the whole list of routes
    for (size_t j = 0; j < num_routes; ++j) {
      const envoy_config_route_v3_RouteMatch* match =
          envoy_config_route_v3_Route_match(routes[j]);
      if (match == nullptr) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Match can't be null.");
      }
      size_t query_parameters_size;
      static_cast<void>(envoy_config_route_v3_RouteMatch_query_parameters(
          match, &query_parameters_size));
      if (query_parameters_size > 0) {
        continue;
      }
      XdsApi::Route route;
      bool ignore_route = false;
      grpc_error_handle error =
          RoutePathMatchParse(match, &route, &ignore_route);
      if (error != GRPC_ERROR_NONE) return error;
      if (ignore_route) continue;
      error = RouteHeaderMatchersParse(match, &route);
      if (error != GRPC_ERROR_NONE) return error;
      error = RouteRuntimeFractionParse(match, &route);
      if (error != GRPC_ERROR_NONE) return error;
      error = RouteActionParse(context, routes[j], &route, &ignore_route);
      if (error != GRPC_ERROR_NONE) return error;
      if (ignore_route) continue;
      if (context.use_v3) {
        grpc_error_handle error = ParseTypedPerFilterConfig<
            envoy_config_route_v3_Route,
            envoy_config_route_v3_Route_TypedPerFilterConfigEntry>(
            context, routes[j],
            envoy_config_route_v3_Route_typed_per_filter_config_next,
            envoy_config_route_v3_Route_TypedPerFilterConfigEntry_key,
            envoy_config_route_v3_Route_TypedPerFilterConfigEntry_value,
            &route.typed_per_filter_config);
        if (error != GRPC_ERROR_NONE) return error;
      }
      vhost.routes.emplace_back(std::move(route));
    }
    if (vhost.routes.empty()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No valid routes specified.");
    }
  }
  return GRPC_ERROR_NONE;
}

XdsApi::CommonTlsContext::CertificateProviderInstance
CertificateProviderInstanceParse(
    const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance*
        certificate_provider_instance_proto) {
  return {
      UpbStringToStdString(
          envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance_instance_name(
              certificate_provider_instance_proto)),
      UpbStringToStdString(
          envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance_certificate_name(
              certificate_provider_instance_proto))};
}

grpc_error_handle CommonTlsContextParse(
    const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext*
        common_tls_context_proto,
    XdsApi::CommonTlsContext* common_tls_context) GRPC_MUST_USE_RESULT;
grpc_error_handle CommonTlsContextParse(
    const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext*
        common_tls_context_proto,
    XdsApi::CommonTlsContext* common_tls_context) {
  auto* combined_validation_context =
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_combined_validation_context(
          common_tls_context_proto);
  if (combined_validation_context != nullptr) {
    auto* default_validation_context =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CombinedCertificateValidationContext_default_validation_context(
            combined_validation_context);
    if (default_validation_context != nullptr) {
      size_t len = 0;
      auto* subject_alt_names_matchers =
          envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_match_subject_alt_names(
              default_validation_context, &len);
      for (size_t i = 0; i < len; ++i) {
        StringMatcher::Type type;
        std::string matcher;
        if (envoy_type_matcher_v3_StringMatcher_has_exact(
                subject_alt_names_matchers[i])) {
          type = StringMatcher::Type::kExact;
          matcher =
              UpbStringToStdString(envoy_type_matcher_v3_StringMatcher_exact(
                  subject_alt_names_matchers[i]));
        } else if (envoy_type_matcher_v3_StringMatcher_has_prefix(
                       subject_alt_names_matchers[i])) {
          type = StringMatcher::Type::kPrefix;
          matcher =
              UpbStringToStdString(envoy_type_matcher_v3_StringMatcher_prefix(
                  subject_alt_names_matchers[i]));
        } else if (envoy_type_matcher_v3_StringMatcher_has_suffix(
                       subject_alt_names_matchers[i])) {
          type = StringMatcher::Type::kSuffix;
          matcher =
              UpbStringToStdString(envoy_type_matcher_v3_StringMatcher_suffix(
                  subject_alt_names_matchers[i]));
        } else if (envoy_type_matcher_v3_StringMatcher_has_contains(
                       subject_alt_names_matchers[i])) {
          type = StringMatcher::Type::kContains;
          matcher =
              UpbStringToStdString(envoy_type_matcher_v3_StringMatcher_contains(
                  subject_alt_names_matchers[i]));
        } else if (envoy_type_matcher_v3_StringMatcher_has_safe_regex(
                       subject_alt_names_matchers[i])) {
          type = StringMatcher::Type::kSafeRegex;
          auto* regex_matcher = envoy_type_matcher_v3_StringMatcher_safe_regex(
              subject_alt_names_matchers[i]);
          matcher = UpbStringToStdString(
              envoy_type_matcher_v3_RegexMatcher_regex(regex_matcher));
        } else {
          return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "Invalid StringMatcher specified");
        }
        bool ignore_case = envoy_type_matcher_v3_StringMatcher_ignore_case(
            subject_alt_names_matchers[i]);
        absl::StatusOr<StringMatcher> string_matcher =
            StringMatcher::Create(type, matcher,
                                  /*case_sensitive=*/!ignore_case);
        if (!string_matcher.ok()) {
          return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("string matcher: ",
                           string_matcher.status().message())
                  .c_str());
        }
        if (type == StringMatcher::Type::kSafeRegex && ignore_case) {
          return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "StringMatcher: ignore_case has no effect for SAFE_REGEX.");
        }
        common_tls_context->combined_validation_context
            .default_validation_context.match_subject_alt_names.push_back(
                std::move(string_matcher.value()));
      }
    }
    auto* validation_context_certificate_provider_instance =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CombinedCertificateValidationContext_validation_context_certificate_provider_instance(
            combined_validation_context);
    if (validation_context_certificate_provider_instance != nullptr) {
      common_tls_context->combined_validation_context
          .validation_context_certificate_provider_instance =
          CertificateProviderInstanceParse(
              validation_context_certificate_provider_instance);
    }
  }
  auto* tls_certificate_certificate_provider_instance =
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_tls_certificate_certificate_provider_instance(
          common_tls_context_proto);
  if (tls_certificate_certificate_provider_instance != nullptr) {
    common_tls_context->tls_certificate_certificate_provider_instance =
        CertificateProviderInstanceParse(
            tls_certificate_certificate_provider_instance);
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle HttpConnectionManagerParse(
    bool is_client, const EncodingContext& context,
    const envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager*
        http_connection_manager_proto,
    bool is_v2,
    XdsApi::LdsUpdate::HttpConnectionManager* http_connection_manager) {
  MaybeLogHttpConnectionManager(context, http_connection_manager_proto);
  // Obtain max_stream_duration from Http Protocol Options.
  const envoy_config_core_v3_HttpProtocolOptions* options =
      envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_common_http_protocol_options(
          http_connection_manager_proto);
  if (options != nullptr) {
    const google_protobuf_Duration* duration =
        envoy_config_core_v3_HttpProtocolOptions_max_stream_duration(options);
    if (duration != nullptr) {
      http_connection_manager->http_max_stream_duration.seconds =
          google_protobuf_Duration_seconds(duration);
      http_connection_manager->http_max_stream_duration.nanos =
          google_protobuf_Duration_nanos(duration);
    }
  }
  // Parse filters.
  if (!is_v2) {
    size_t num_filters = 0;
    const auto* http_filters =
        envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_http_filters(
            http_connection_manager_proto, &num_filters);
    std::set<absl::string_view> names_seen;
    for (size_t i = 0; i < num_filters; ++i) {
      const auto* http_filter = http_filters[i];
      absl::string_view name = UpbStringToAbsl(
          envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_name(
              http_filter));
      if (name.empty()) {
        return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("empty filter name at index ", i).c_str());
      }
      if (names_seen.find(name) != names_seen.end()) {
        return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("duplicate HTTP filter name: ", name).c_str());
      }
      names_seen.insert(name);
      const bool is_optional =
          envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_is_optional(
              http_filter);
      const google_protobuf_Any* any =
          envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_typed_config(
              http_filter);
      if (any == nullptr) {
        if (is_optional) continue;
        return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("no filter config specified for filter name ", name)
                .c_str());
      }
      absl::string_view filter_type;
      grpc_error_handle error =
          ExtractHttpFilterTypeName(context, any, &filter_type);
      if (error != GRPC_ERROR_NONE) return error;
      const XdsHttpFilterImpl* filter_impl =
          XdsHttpFilterRegistry::GetFilterForType(filter_type);
      if (filter_impl == nullptr) {
        if (is_optional) continue;
        return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("no filter registered for config type ", filter_type)
                .c_str());
      }
      if ((is_client && !filter_impl->IsSupportedOnClients()) ||
          (!is_client && !filter_impl->IsSupportedOnServers())) {
        if (is_optional) continue;
        return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrFormat("Filter %s is not supported on %s", filter_type,
                            is_client ? "clients" : "servers")
                .c_str());
      }
      absl::StatusOr<XdsHttpFilterImpl::FilterConfig> filter_config =
          filter_impl->GenerateFilterConfig(google_protobuf_Any_value(any),
                                            context.arena);
      if (!filter_config.ok()) {
        return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat(
                "filter config for type ", filter_type,
                " failed to parse: ", filter_config.status().ToString())
                .c_str());
      }
      http_connection_manager->http_filters.emplace_back(
          XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter{
              std::string(name), std::move(*filter_config)});
    }
  } else {
    // If using a v2 config, we just hard-code a list containing only the
    // router filter without actually looking at the config.  This ensures
    // that the right thing happens in the xds resolver without having
    // to expose whether the resource we received was v2 or v3.
    http_connection_manager->http_filters.emplace_back(
        XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter{
            "router", {kXdsHttpRouterFilterConfigName, Json()}});
  }
  if (is_client) {
    // Found inlined route_config. Parse it to find the cluster_name.
    if (envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_has_route_config(
            http_connection_manager_proto)) {
      const envoy_config_route_v3_RouteConfiguration* route_config =
          envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_route_config(
              http_connection_manager_proto);
      XdsApi::RdsUpdate rds_update;
      grpc_error_handle error =
          RouteConfigParse(context, route_config, &rds_update);
      if (error != GRPC_ERROR_NONE) return error;
      http_connection_manager->rds_update = std::move(rds_update);
      return GRPC_ERROR_NONE;
    }
    // Validate that RDS must be used to get the route_config dynamically.
    const envoy_extensions_filters_network_http_connection_manager_v3_Rds* rds =
        envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_rds(
            http_connection_manager_proto);
    if (rds == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "HttpConnectionManager neither has inlined route_config nor RDS.");
    }
    // Check that the ConfigSource specifies ADS.
    const envoy_config_core_v3_ConfigSource* config_source =
        envoy_extensions_filters_network_http_connection_manager_v3_Rds_config_source(
            rds);
    if (config_source == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "HttpConnectionManager missing config_source for RDS.");
    }
    if (!envoy_config_core_v3_ConfigSource_has_ads(config_source)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "HttpConnectionManager ConfigSource for RDS does not specify ADS.");
    }
    // Get the route_config_name.
    http_connection_manager->route_config_name = UpbStringToStdString(
        envoy_extensions_filters_network_http_connection_manager_v3_Rds_route_config_name(
            rds));
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle LdsResponseParseClient(
    const EncodingContext& context,
    const envoy_config_listener_v3_ApiListener* api_listener, bool is_v2,
    XdsApi::LdsUpdate* lds_update) {
  lds_update->type = XdsApi::LdsUpdate::ListenerType::kHttpApiListener;
  const upb_strview encoded_api_listener = google_protobuf_Any_value(
      envoy_config_listener_v3_ApiListener_api_listener(api_listener));
  const auto* http_connection_manager =
      envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_parse(
          encoded_api_listener.data, encoded_api_listener.size, context.arena);
  if (http_connection_manager == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Could not parse HttpConnectionManager config from ApiListener");
  }
  return HttpConnectionManagerParse(true /* is_client */, context,
                                    http_connection_manager, is_v2,
                                    &lds_update->http_connection_manager);
}

grpc_error_handle DownstreamTlsContextParse(
    const EncodingContext& context,
    const envoy_config_core_v3_TransportSocket* transport_socket,
    XdsApi::DownstreamTlsContext* downstream_tls_context) {
  absl::string_view name = UpbStringToAbsl(
      envoy_config_core_v3_TransportSocket_name(transport_socket));
  if (name == "envoy.transport_sockets.tls") {
    auto* typed_config =
        envoy_config_core_v3_TransportSocket_typed_config(transport_socket);
    if (typed_config != nullptr) {
      const upb_strview encoded_downstream_tls_context =
          google_protobuf_Any_value(typed_config);
      auto* downstream_tls_context_proto =
          envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_parse(
              encoded_downstream_tls_context.data,
              encoded_downstream_tls_context.size, context.arena);
      if (downstream_tls_context_proto == nullptr) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Can't decode downstream tls context.");
      }
      auto* common_tls_context =
          envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_common_tls_context(
              downstream_tls_context_proto);
      if (common_tls_context != nullptr) {
        grpc_error_handle error = CommonTlsContextParse(
            common_tls_context, &downstream_tls_context->common_tls_context);
        if (error != GRPC_ERROR_NONE) return error;
      }
      auto* require_client_certificate =
          envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_require_client_certificate(
              downstream_tls_context_proto);
      if (require_client_certificate != nullptr) {
        downstream_tls_context->require_client_certificate =
            google_protobuf_BoolValue_value(require_client_certificate);
      }
    }
    if (downstream_tls_context->common_tls_context
            .tls_certificate_certificate_provider_instance.instance_name
            .empty()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "TLS configuration provided but no "
          "tls_certificate_certificate_provider_instance found.");
    }
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle CidrRangeParse(
    const envoy_config_core_v3_CidrRange* cidr_range_proto,
    XdsApi::LdsUpdate::FilterChainMap::CidrRange* cidr_range) {
  std::string address_prefix = UpbStringToStdString(
      envoy_config_core_v3_CidrRange_address_prefix(cidr_range_proto));
  grpc_error_handle error =
      grpc_string_to_sockaddr(&cidr_range->address, address_prefix.c_str(), 0);
  if (error != GRPC_ERROR_NONE) return error;
  cidr_range->prefix_len = 0;
  auto* prefix_len_proto =
      envoy_config_core_v3_CidrRange_prefix_len(cidr_range_proto);
  if (prefix_len_proto != nullptr) {
    cidr_range->prefix_len = std::min(
        google_protobuf_UInt32Value_value(prefix_len_proto),
        (reinterpret_cast<const grpc_sockaddr*>(cidr_range->address.addr))
                    ->sa_family == GRPC_AF_INET
            ? uint32_t(32)
            : uint32_t(128));
  }
  // Normalize the network address by masking it with prefix_len
  grpc_sockaddr_mask_bits(&cidr_range->address, cidr_range->prefix_len);
  return GRPC_ERROR_NONE;
}

grpc_error_handle FilterChainMatchParse(
    const envoy_config_listener_v3_FilterChainMatch* filter_chain_match_proto,
    FilterChain::FilterChainMatch* filter_chain_match) {
  auto* destination_port =
      envoy_config_listener_v3_FilterChainMatch_destination_port(
          filter_chain_match_proto);
  if (destination_port != nullptr) {
    filter_chain_match->destination_port =
        google_protobuf_UInt32Value_value(destination_port);
  }
  size_t size = 0;
  auto* prefix_ranges = envoy_config_listener_v3_FilterChainMatch_prefix_ranges(
      filter_chain_match_proto, &size);
  filter_chain_match->prefix_ranges.reserve(size);
  for (size_t i = 0; i < size; i++) {
    XdsApi::LdsUpdate::FilterChainMap::CidrRange cidr_range;
    grpc_error_handle error = CidrRangeParse(prefix_ranges[i], &cidr_range);
    if (error != GRPC_ERROR_NONE) return error;
    filter_chain_match->prefix_ranges.push_back(cidr_range);
  }
  filter_chain_match->source_type =
      static_cast<XdsApi::LdsUpdate::FilterChainMap::ConnectionSourceType>(
          envoy_config_listener_v3_FilterChainMatch_source_type(
              filter_chain_match_proto));
  auto* source_prefix_ranges =
      envoy_config_listener_v3_FilterChainMatch_source_prefix_ranges(
          filter_chain_match_proto, &size);
  filter_chain_match->source_prefix_ranges.reserve(size);
  for (size_t i = 0; i < size; i++) {
    XdsApi::LdsUpdate::FilterChainMap::CidrRange cidr_range;
    grpc_error_handle error =
        CidrRangeParse(source_prefix_ranges[i], &cidr_range);
    if (error != GRPC_ERROR_NONE) return error;
    filter_chain_match->source_prefix_ranges.push_back(cidr_range);
  }
  auto* source_ports = envoy_config_listener_v3_FilterChainMatch_source_ports(
      filter_chain_match_proto, &size);
  filter_chain_match->source_ports.reserve(size);
  for (size_t i = 0; i < size; i++) {
    filter_chain_match->source_ports.push_back(source_ports[i]);
  }
  auto* server_names = envoy_config_listener_v3_FilterChainMatch_server_names(
      filter_chain_match_proto, &size);
  for (size_t i = 0; i < size; i++) {
    filter_chain_match->server_names.push_back(
        UpbStringToStdString(server_names[i]));
  }
  filter_chain_match->transport_protocol = UpbStringToStdString(
      envoy_config_listener_v3_FilterChainMatch_transport_protocol(
          filter_chain_match_proto));
  auto* application_protocols =
      envoy_config_listener_v3_FilterChainMatch_application_protocols(
          filter_chain_match_proto, &size);
  for (size_t i = 0; i < size; i++) {
    filter_chain_match->application_protocols.push_back(
        UpbStringToStdString(application_protocols[i]));
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle FilterChainParse(
    const EncodingContext& context,
    const envoy_config_listener_v3_FilterChain* filter_chain_proto, bool is_v2,
    FilterChain* filter_chain) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto* filter_chain_match =
      envoy_config_listener_v3_FilterChain_filter_chain_match(
          filter_chain_proto);
  if (filter_chain_match != nullptr) {
    error = FilterChainMatchParse(filter_chain_match,
                                  &filter_chain->filter_chain_match);
    if (error != GRPC_ERROR_NONE) return error;
  }
  // Parse the filters list. Currently we only support HttpConnectionManager.
  size_t size = 0;
  auto* filters =
      envoy_config_listener_v3_FilterChain_filters(filter_chain_proto, &size);
  if (size != 1) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "FilterChain should have exactly one filter: HttpConnectionManager; no "
        "other filter is supported at the moment");
  }
  auto* typed_config = envoy_config_listener_v3_Filter_typed_config(filters[0]);
  if (typed_config == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "No typed_config found in filter.");
  }
  absl::string_view type_url =
      UpbStringToAbsl(google_protobuf_Any_type_url(typed_config));
  if (type_url !=
      "type.googleapis.com/"
      "envoy.extensions.filters.network.http_connection_manager.v3."
      "HttpConnectionManager") {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Unsupported filter type ", type_url).c_str());
  }
  const upb_strview encoded_http_connection_manager =
      google_protobuf_Any_value(typed_config);
  const auto* http_connection_manager =
      envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_parse(
          encoded_http_connection_manager.data,
          encoded_http_connection_manager.size, context.arena);
  if (http_connection_manager == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Could not parse HttpConnectionManager config from filter "
        "typed_config");
  }
  filter_chain->filter_chain_data =
      std::make_shared<XdsApi::LdsUpdate::FilterChainData>();
  error = HttpConnectionManagerParse(
      false /* is_client */, context, http_connection_manager, is_v2,
      &filter_chain->filter_chain_data->http_connection_manager);
  if (error != GRPC_ERROR_NONE) return error;
  // Get the DownstreamTlsContext for the filter chain
  if (XdsSecurityEnabled()) {
    auto* transport_socket =
        envoy_config_listener_v3_FilterChain_transport_socket(
            filter_chain_proto);
    if (transport_socket != nullptr) {
      error = DownstreamTlsContextParse(
          context, transport_socket,
          &filter_chain->filter_chain_data->downstream_tls_context);
    }
  }
  return error;
}

grpc_error_handle AddressParse(
    const envoy_config_core_v3_Address* address_proto, std::string* address) {
  const auto* socket_address =
      envoy_config_core_v3_Address_socket_address(address_proto);
  if (socket_address == nullptr) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        "Address does not have socket_address");
  }
  if (envoy_config_core_v3_SocketAddress_protocol(socket_address) !=
      envoy_config_core_v3_SocketAddress_TCP) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "SocketAddress protocol is not TCP");
  }
  uint32_t port = envoy_config_core_v3_SocketAddress_port_value(socket_address);
  if (port > 65535) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Invalid port");
  }
  *address = JoinHostPort(
      UpbStringToAbsl(
          envoy_config_core_v3_SocketAddress_address(socket_address)),
      port);
  return GRPC_ERROR_NONE;
}

// An intermediate map for filter chains that we create to validate the list of
// filter chains received from the control plane and to finally create
// XdsApi::LdsUpdate::FilterChainMap
struct InternalFilterChainMap {
  using SourceIpMap =
      std::map<std::string, XdsApi::LdsUpdate::FilterChainMap::SourceIp>;
  using ConnectionSourceTypesArray = std::array<SourceIpMap, 3>;
  struct DestinationIp {
    absl::optional<XdsApi::LdsUpdate::FilterChainMap::CidrRange> prefix_range;
    bool transport_protocol_raw_buffer_provided = false;
    ConnectionSourceTypesArray source_types_array;
  };
  using DestinationIpMap = std::map<std::string, DestinationIp>;
  DestinationIpMap destination_ip_map;
};

grpc_error_handle AddFilterChainDataForSourcePort(
    const FilterChain& filter_chain,
    XdsApi::LdsUpdate::FilterChainMap::SourcePortsMap* ports_map,
    uint32_t port) {
  auto insert_result = ports_map->emplace(
      port, XdsApi::LdsUpdate::FilterChainMap::FilterChainDataSharedPtr{
                filter_chain.filter_chain_data});
  if (!insert_result.second) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat(
            "Duplicate matching rules detected when adding filter chain: ",
            filter_chain.filter_chain_match.ToString())
            .c_str());
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle AddFilterChainDataForSourcePorts(
    const FilterChain& filter_chain,
    XdsApi::LdsUpdate::FilterChainMap::SourcePortsMap* ports_map) {
  if (filter_chain.filter_chain_match.source_ports.empty()) {
    return AddFilterChainDataForSourcePort(filter_chain, ports_map, 0);
  } else {
    for (uint32_t port : filter_chain.filter_chain_match.source_ports) {
      grpc_error_handle error =
          AddFilterChainDataForSourcePort(filter_chain, ports_map, port);
      if (error != GRPC_ERROR_NONE) return error;
    }
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle AddFilterChainDataForSourceIpRange(
    const FilterChain& filter_chain,
    InternalFilterChainMap::SourceIpMap* source_ip_map) {
  if (filter_chain.filter_chain_match.source_prefix_ranges.empty()) {
    auto insert_result = source_ip_map->emplace(
        "", XdsApi::LdsUpdate::FilterChainMap::SourceIp());
    return AddFilterChainDataForSourcePorts(
        filter_chain, &insert_result.first->second.ports_map);
  } else {
    for (const auto& prefix_range :
         filter_chain.filter_chain_match.source_prefix_ranges) {
      auto insert_result = source_ip_map->emplace(
          absl::StrCat(grpc_sockaddr_to_string(&prefix_range.address, false),
                       "/", prefix_range.prefix_len),
          XdsApi::LdsUpdate::FilterChainMap::SourceIp());
      if (insert_result.second) {
        insert_result.first->second.prefix_range.emplace(prefix_range);
      }
      grpc_error_handle error = AddFilterChainDataForSourcePorts(
          filter_chain, &insert_result.first->second.ports_map);
      if (error != GRPC_ERROR_NONE) return error;
    }
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle AddFilterChainDataForSourceType(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIp* destination_ip) {
  GPR_ASSERT(static_cast<unsigned int>(
                 filter_chain.filter_chain_match.source_type) < 3);
  return AddFilterChainDataForSourceIpRange(
      filter_chain, &destination_ip->source_types_array[static_cast<int>(
                        filter_chain.filter_chain_match.source_type)]);
}

grpc_error_handle AddFilterChainDataForApplicationProtocols(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIp* destination_ip) {
  // Only allow filter chains that do not mention application protocols
  if (!filter_chain.filter_chain_match.application_protocols.empty()) {
    return GRPC_ERROR_NONE;
  }
  return AddFilterChainDataForSourceType(filter_chain, destination_ip);
}

grpc_error_handle AddFilterChainDataForTransportProtocol(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIp* destination_ip) {
  const std::string& transport_protocol =
      filter_chain.filter_chain_match.transport_protocol;
  // Only allow filter chains with no transport protocol or "raw_buffer"
  if (!transport_protocol.empty() && transport_protocol != "raw_buffer") {
    return GRPC_ERROR_NONE;
  }
  // If for this configuration, we've already seen filter chains that mention
  // the transport protocol as "raw_buffer", we will never match filter chains
  // that do not mention it.
  if (destination_ip->transport_protocol_raw_buffer_provided &&
      transport_protocol.empty()) {
    return GRPC_ERROR_NONE;
  }
  if (!transport_protocol.empty() &&
      !destination_ip->transport_protocol_raw_buffer_provided) {
    destination_ip->transport_protocol_raw_buffer_provided = true;
    // Clear out the previous entries if any since those entries did not mention
    // "raw_buffer"
    destination_ip->source_types_array =
        InternalFilterChainMap::ConnectionSourceTypesArray();
  }
  return AddFilterChainDataForApplicationProtocols(filter_chain,
                                                   destination_ip);
}

grpc_error_handle AddFilterChainDataForServerNames(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIp* destination_ip) {
  // Don't continue adding filter chains with server names mentioned
  if (!filter_chain.filter_chain_match.server_names.empty()) {
    return GRPC_ERROR_NONE;
  }
  return AddFilterChainDataForTransportProtocol(filter_chain, destination_ip);
}

grpc_error_handle AddFilterChainDataForDestinationIpRange(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIpMap* destination_ip_map) {
  if (filter_chain.filter_chain_match.prefix_ranges.empty()) {
    auto insert_result = destination_ip_map->emplace(
        "", InternalFilterChainMap::DestinationIp());
    return AddFilterChainDataForServerNames(filter_chain,
                                            &insert_result.first->second);
  } else {
    for (const auto& prefix_range :
         filter_chain.filter_chain_match.prefix_ranges) {
      auto insert_result = destination_ip_map->emplace(
          absl::StrCat(grpc_sockaddr_to_string(&prefix_range.address, false),
                       "/", prefix_range.prefix_len),
          InternalFilterChainMap::DestinationIp());
      if (insert_result.second) {
        insert_result.first->second.prefix_range.emplace(prefix_range);
      }
      grpc_error_handle error = AddFilterChainDataForServerNames(
          filter_chain, &insert_result.first->second);
      if (error != GRPC_ERROR_NONE) return error;
    }
  }
  return GRPC_ERROR_NONE;
}

XdsApi::LdsUpdate::FilterChainMap BuildFromInternalFilterChainMap(
    InternalFilterChainMap* internal_filter_chain_map) {
  XdsApi::LdsUpdate::FilterChainMap filter_chain_map;
  for (auto& destination_ip_pair :
       internal_filter_chain_map->destination_ip_map) {
    XdsApi::LdsUpdate::FilterChainMap::DestinationIp destination_ip;
    destination_ip.prefix_range = destination_ip_pair.second.prefix_range;
    for (int i = 0; i < 3; i++) {
      auto& source_ip_map = destination_ip_pair.second.source_types_array[i];
      for (auto& source_ip_pair : source_ip_map) {
        destination_ip.source_types_array[i].push_back(
            std::move(source_ip_pair.second));
      }
    }
    filter_chain_map.destination_ip_vector.push_back(std::move(destination_ip));
  }
  return filter_chain_map;
}

grpc_error_handle BuildFilterChainMap(
    const std::vector<FilterChain>& filter_chains,
    XdsApi::LdsUpdate::FilterChainMap* filter_chain_map) {
  InternalFilterChainMap internal_filter_chain_map;
  for (const auto& filter_chain : filter_chains) {
    // Discard filter chain entries that specify destination port
    if (filter_chain.filter_chain_match.destination_port != 0) continue;
    grpc_error_handle error = AddFilterChainDataForDestinationIpRange(
        filter_chain, &internal_filter_chain_map.destination_ip_map);
    if (error != GRPC_ERROR_NONE) return error;
  }
  *filter_chain_map =
      BuildFromInternalFilterChainMap(&internal_filter_chain_map);
  return GRPC_ERROR_NONE;
}

grpc_error_handle LdsResponseParseServer(
    const EncodingContext& context,
    const envoy_config_listener_v3_Listener* listener, bool is_v2,
    XdsApi::LdsUpdate* lds_update) {
  lds_update->type = XdsApi::LdsUpdate::ListenerType::kTcpListener;
  grpc_error_handle error =
      AddressParse(envoy_config_listener_v3_Listener_address(listener),
                   &lds_update->address);
  if (error != GRPC_ERROR_NONE) return error;
  const auto* use_original_dst =
      envoy_config_listener_v3_Listener_use_original_dst(listener);
  if (use_original_dst != nullptr) {
    if (google_protobuf_BoolValue_value(use_original_dst)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Field \'use_original_dst\' is not supported.");
    }
  }
  size_t size = 0;
  auto* filter_chains =
      envoy_config_listener_v3_Listener_filter_chains(listener, &size);
  std::vector<FilterChain> parsed_filter_chains;
  parsed_filter_chains.reserve(size);
  for (size_t i = 0; i < size; i++) {
    FilterChain filter_chain;
    error = FilterChainParse(context, filter_chains[i], is_v2, &filter_chain);
    if (error != GRPC_ERROR_NONE) return error;
    parsed_filter_chains.push_back(std::move(filter_chain));
  }
  error =
      BuildFilterChainMap(parsed_filter_chains, &lds_update->filter_chain_map);
  if (error != GRPC_ERROR_NONE) return error;
  auto* default_filter_chain =
      envoy_config_listener_v3_Listener_default_filter_chain(listener);
  if (default_filter_chain != nullptr) {
    FilterChain filter_chain;
    error =
        FilterChainParse(context, default_filter_chain, is_v2, &filter_chain);
    if (error != GRPC_ERROR_NONE) return error;
    if (filter_chain.filter_chain_data != nullptr) {
      lds_update->default_filter_chain =
          std::move(*filter_chain.filter_chain_data);
    }
  }
  if (size == 0 && default_filter_chain == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No filter chain provided.");
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle LdsResponseParse(
    const EncodingContext& context,
    const envoy_service_discovery_v3_DiscoveryResponse* response,
    const std::set<absl::string_view>& expected_listener_names,
    XdsApi::LdsUpdateMap* lds_update_map,
    std::set<std::string>* resource_names_failed) {
  std::vector<grpc_error_handle> errors;
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_service_discovery_v3_DiscoveryResponse_resources(response, &size);
  for (size_t i = 0; i < size; ++i) {
    // Check the type_url of the resource.
    absl::string_view type_url =
        UpbStringToAbsl(google_protobuf_Any_type_url(resources[i]));
    bool is_v2 = false;
    if (!IsLds(type_url, &is_v2)) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("resource index ", i, ": Resource is not LDS.")
              .c_str()));
      continue;
    }
    // Decode the listener.
    const upb_strview encoded_listener =
        google_protobuf_Any_value(resources[i]);
    const envoy_config_listener_v3_Listener* listener =
        envoy_config_listener_v3_Listener_parse(
            encoded_listener.data, encoded_listener.size, context.arena);
    if (listener == nullptr) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("resource index ", i, ": Can't decode listener.")
              .c_str()));
      continue;
    }
    // Check listener name. Ignore unexpected listeners.
    std::string listener_name =
        UpbStringToStdString(envoy_config_listener_v3_Listener_name(listener));
    if (expected_listener_names.find(listener_name) ==
        expected_listener_names.end()) {
      continue;
    }
    // Fail if listener name is duplicated.
    if (lds_update_map->find(listener_name) != lds_update_map->end()) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("duplicate listener name \"", listener_name, "\"")
              .c_str()));
      resource_names_failed->insert(listener_name);
      continue;
    }
    // Serialize into JSON and store it in the LdsUpdateMap
    XdsApi::LdsResourceData& lds_resource_data =
        (*lds_update_map)[listener_name];
    XdsApi::LdsUpdate& lds_update = lds_resource_data.resource;
    lds_resource_data.serialized_proto = UpbStringToStdString(encoded_listener);
    // Check whether it's a client or server listener.
    const envoy_config_listener_v3_ApiListener* api_listener =
        envoy_config_listener_v3_Listener_api_listener(listener);
    const envoy_config_core_v3_Address* address =
        envoy_config_listener_v3_Listener_address(listener);
    if (api_listener != nullptr && address != nullptr) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat(listener_name,
                       ": Listener has both address and ApiListener")
              .c_str()));
      resource_names_failed->insert(listener_name);
      continue;
    }
    if (api_listener == nullptr && address == nullptr) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat(listener_name,
                       ": Listener has neither address nor ApiListener")
              .c_str()));
      resource_names_failed->insert(listener_name);
      continue;
    }
    grpc_error_handle error = GRPC_ERROR_NONE;
    if (api_listener != nullptr) {
      error = LdsResponseParseClient(context, api_listener, is_v2, &lds_update);
    } else {
      error = LdsResponseParseServer(context, listener, is_v2, &lds_update);
    }
    if (error != GRPC_ERROR_NONE) {
      errors.push_back(grpc_error_add_child(
          GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat(listener_name, ": validation error").c_str()),
          error));
      resource_names_failed->insert(listener_name);
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing LDS response", &errors);
}

grpc_error_handle RdsResponseParse(
    const EncodingContext& context,
    const envoy_service_discovery_v3_DiscoveryResponse* response,
    const std::set<absl::string_view>& expected_route_configuration_names,
    XdsApi::RdsUpdateMap* rds_update_map,
    std::set<std::string>* resource_names_failed) {
  std::vector<grpc_error_handle> errors;
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_service_discovery_v3_DiscoveryResponse_resources(response, &size);
  for (size_t i = 0; i < size; ++i) {
    // Check the type_url of the resource.
    absl::string_view type_url =
        UpbStringToAbsl(google_protobuf_Any_type_url(resources[i]));
    if (!IsRds(type_url)) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("resource index ", i, ": Resource is not RDS.")
              .c_str()));
      continue;
    }
    // Decode the route_config.
    const upb_strview encoded_route_config =
        google_protobuf_Any_value(resources[i]);
    const envoy_config_route_v3_RouteConfiguration* route_config =
        envoy_config_route_v3_RouteConfiguration_parse(
            encoded_route_config.data, encoded_route_config.size,
            context.arena);
    if (route_config == nullptr) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("resource index ", i, ": Can't decode route_config.")
              .c_str()));
      continue;
    }
    // Check route_config_name. Ignore unexpected route_config.
    std::string route_config_name = UpbStringToStdString(
        envoy_config_route_v3_RouteConfiguration_name(route_config));
    if (expected_route_configuration_names.find(route_config_name) ==
        expected_route_configuration_names.end()) {
      continue;
    }
    // Fail if route config name is duplicated.
    if (rds_update_map->find(route_config_name) != rds_update_map->end()) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("duplicate route config name \"", route_config_name,
                       "\"")
              .c_str()));
      resource_names_failed->insert(route_config_name);
      continue;
    }
    // Serialize into JSON and store it in the RdsUpdateMap
    XdsApi::RdsResourceData& rds_resource_data =
        (*rds_update_map)[route_config_name];
    XdsApi::RdsUpdate& rds_update = rds_resource_data.resource;
    rds_resource_data.serialized_proto =
        UpbStringToStdString(encoded_route_config);
    // Parse the route_config.
    grpc_error_handle error =
        RouteConfigParse(context, route_config, &rds_update);
    if (error != GRPC_ERROR_NONE) {
      errors.push_back(grpc_error_add_child(
          GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat(route_config_name, ": validation error").c_str()),
          error));
      resource_names_failed->insert(route_config_name);
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing RDS response", &errors);
}

grpc_error_handle CdsResponseParse(
    const EncodingContext& context,
    const envoy_service_discovery_v3_DiscoveryResponse* response,
    const std::set<absl::string_view>& expected_cluster_names,
    XdsApi::CdsUpdateMap* cds_update_map,
    std::set<std::string>* resource_names_failed) {
  std::vector<grpc_error_handle> errors;
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_service_discovery_v3_DiscoveryResponse_resources(response, &size);
  // Parse all the resources in the CDS response.
  for (size_t i = 0; i < size; ++i) {
    // Check the type_url of the resource.
    absl::string_view type_url =
        UpbStringToAbsl(google_protobuf_Any_type_url(resources[i]));
    if (!IsCds(type_url)) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("resource index ", i, ": Resource is not CDS.")
              .c_str()));
      continue;
    }
    // Decode the cluster.
    const upb_strview encoded_cluster = google_protobuf_Any_value(resources[i]);
    const envoy_config_cluster_v3_Cluster* cluster =
        envoy_config_cluster_v3_Cluster_parse(
            encoded_cluster.data, encoded_cluster.size, context.arena);
    if (cluster == nullptr) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("resource index ", i, ": Can't decode cluster.")
              .c_str()));
      continue;
    }
    MaybeLogCluster(context, cluster);
    // Ignore unexpected cluster names.
    std::string cluster_name =
        UpbStringToStdString(envoy_config_cluster_v3_Cluster_name(cluster));
    if (expected_cluster_names.find(cluster_name) ==
        expected_cluster_names.end()) {
      continue;
    }
    // Fail on duplicate resources.
    if (cds_update_map->find(cluster_name) != cds_update_map->end()) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("duplicate resource name \"", cluster_name, "\"")
              .c_str()));
      resource_names_failed->insert(cluster_name);
      continue;
    }
    // Serialize into JSON and store it in the CdsUpdateMap
    XdsApi::CdsResourceData& cds_resource_data =
        (*cds_update_map)[cluster_name];
    XdsApi::CdsUpdate& cds_update = cds_resource_data.resource;
    cds_resource_data.serialized_proto = UpbStringToStdString(encoded_cluster);
    // Check the cluster_discovery_type.
    if (!envoy_config_cluster_v3_Cluster_has_type(cluster) &&
        !envoy_config_cluster_v3_Cluster_has_cluster_type(cluster)) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat(cluster_name, ": DiscoveryType not found.").c_str()));
      resource_names_failed->insert(cluster_name);
      continue;
    }
    if (envoy_config_cluster_v3_Cluster_type(cluster) ==
        envoy_config_cluster_v3_Cluster_EDS) {
      cds_update.cluster_type = XdsApi::CdsUpdate::ClusterType::EDS;
      // Check the EDS config source.
      const envoy_config_cluster_v3_Cluster_EdsClusterConfig*
          eds_cluster_config =
              envoy_config_cluster_v3_Cluster_eds_cluster_config(cluster);
      const envoy_config_core_v3_ConfigSource* eds_config =
          envoy_config_cluster_v3_Cluster_EdsClusterConfig_eds_config(
              eds_cluster_config);
      if (!envoy_config_core_v3_ConfigSource_has_ads(eds_config)) {
        errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat(cluster_name, ": EDS ConfigSource is not ADS.")
                .c_str()));
        resource_names_failed->insert(cluster_name);
        continue;
      }
      // Record EDS service_name (if any).
      upb_strview service_name =
          envoy_config_cluster_v3_Cluster_EdsClusterConfig_service_name(
              eds_cluster_config);
      if (service_name.size != 0) {
        cds_update.eds_service_name = UpbStringToStdString(service_name);
      }
    } else if (!XdsAggregateAndLogicalDnsClusterEnabled()) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat(cluster_name, ": DiscoveryType is not valid.").c_str()));
      resource_names_failed->insert(cluster_name);
      continue;
    } else if (envoy_config_cluster_v3_Cluster_type(cluster) ==
               envoy_config_cluster_v3_Cluster_LOGICAL_DNS) {
      cds_update.cluster_type = XdsApi::CdsUpdate::ClusterType::LOGICAL_DNS;
    } else {
      if (envoy_config_cluster_v3_Cluster_has_cluster_type(cluster)) {
        const envoy_config_cluster_v3_Cluster_CustomClusterType*
            custom_cluster_type =
                envoy_config_cluster_v3_Cluster_cluster_type(cluster);
        upb_strview type_name =
            envoy_config_cluster_v3_Cluster_CustomClusterType_name(
                custom_cluster_type);
        if (UpbStringToAbsl(type_name) == "envoy.clusters.aggregate") {
          cds_update.cluster_type = XdsApi::CdsUpdate::ClusterType::AGGREGATE;
          // Retrieve aggregate clusters.
          const google_protobuf_Any* typed_config =
              envoy_config_cluster_v3_Cluster_CustomClusterType_typed_config(
                  custom_cluster_type);
          const upb_strview aggregate_cluster_config_upb_strview =
              google_protobuf_Any_value(typed_config);
          const envoy_extensions_clusters_aggregate_v3_ClusterConfig*
              aggregate_cluster_config =
                  envoy_extensions_clusters_aggregate_v3_ClusterConfig_parse(
                      aggregate_cluster_config_upb_strview.data,
                      aggregate_cluster_config_upb_strview.size, context.arena);
          if (aggregate_cluster_config == nullptr) {
            errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                absl::StrCat(cluster_name, ": Can't parse aggregate cluster.")
                    .c_str()));
            resource_names_failed->insert(cluster_name);
            continue;
          }
          size_t size;
          const upb_strview* clusters =
              envoy_extensions_clusters_aggregate_v3_ClusterConfig_clusters(
                  aggregate_cluster_config, &size);
          for (size_t i = 0; i < size; ++i) {
            const upb_strview cluster = clusters[i];
            cds_update.prioritized_cluster_names.emplace_back(
                UpbStringToStdString(cluster));
          }
        } else {
          errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat(cluster_name, ": DiscoveryType is not valid.")
                  .c_str()));
          resource_names_failed->insert(cluster_name);
          continue;
        }
      } else {
        errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat(cluster_name, ": DiscoveryType is not valid.")
                .c_str()));
        resource_names_failed->insert(cluster_name);
        continue;
      }
    }
    // Check the LB policy.
    if (envoy_config_cluster_v3_Cluster_lb_policy(cluster) ==
        envoy_config_cluster_v3_Cluster_ROUND_ROBIN) {
      cds_update.lb_policy = "ROUND_ROBIN";
    } else if (XdsRingHashEnabled() &&
               envoy_config_cluster_v3_Cluster_lb_policy(cluster) ==
                   envoy_config_cluster_v3_Cluster_RING_HASH) {
      cds_update.lb_policy = "RING_HASH";
      // Record ring hash lb config
      auto* ring_hash_config =
          envoy_config_cluster_v3_Cluster_ring_hash_lb_config(cluster);
      if (ring_hash_config == nullptr) {
        errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat(cluster_name,
                         ": ring hash lb config required but not present.")
                .c_str()));
        resource_names_failed->insert(cluster_name);
        continue;
      }
      const google_protobuf_UInt64Value* max_ring_size =
          envoy_config_cluster_v3_Cluster_RingHashLbConfig_maximum_ring_size(
              ring_hash_config);
      if (max_ring_size != nullptr) {
        cds_update.max_ring_size =
            google_protobuf_UInt64Value_value(max_ring_size);
        if (cds_update.max_ring_size > 8388608 ||
            cds_update.max_ring_size == 0) {
          errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat(
                  cluster_name,
                  ": max_ring_size is not in the range of 1 to 8388608.")
                  .c_str()));
          resource_names_failed->insert(cluster_name);
          continue;
        }
      }
      const google_protobuf_UInt64Value* min_ring_size =
          envoy_config_cluster_v3_Cluster_RingHashLbConfig_minimum_ring_size(
              ring_hash_config);
      if (min_ring_size != nullptr) {
        cds_update.min_ring_size =
            google_protobuf_UInt64Value_value(min_ring_size);
        if (cds_update.min_ring_size > 8388608 ||
            cds_update.min_ring_size == 0) {
          errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat(
                  cluster_name,
                  ": min_ring_size is not in the range of 1 to 8388608.")
                  .c_str()));
          resource_names_failed->insert(cluster_name);
          continue;
        }
        if (cds_update.min_ring_size > cds_update.max_ring_size) {
          errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat(
                  cluster_name,
                  ": min_ring_size cannot be greater than max_ring_size.")
                  .c_str()));
          resource_names_failed->insert(cluster_name);
          continue;
        }
      }
      if (envoy_config_cluster_v3_Cluster_RingHashLbConfig_hash_function(
              ring_hash_config) ==
          envoy_config_cluster_v3_Cluster_RingHashLbConfig_XX_HASH) {
        cds_update.hash_function = XdsApi::CdsUpdate::HashFunction::XX_HASH;
      } else if (
          envoy_config_cluster_v3_Cluster_RingHashLbConfig_hash_function(
              ring_hash_config) ==
          envoy_config_cluster_v3_Cluster_RingHashLbConfig_MURMUR_HASH_2) {
        cds_update.hash_function =
            XdsApi::CdsUpdate::HashFunction::MURMUR_HASH_2;
      } else {
        errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat(cluster_name,
                         ": ring hash lb config has invalid hash function.")
                .c_str()));
        resource_names_failed->insert(cluster_name);
        continue;
      }
    } else {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat(cluster_name, ": LB policy is not supported.").c_str()));
      resource_names_failed->insert(cluster_name);
      continue;
    }
    if (XdsSecurityEnabled()) {
      // Record Upstream tls context
      auto* transport_socket =
          envoy_config_cluster_v3_Cluster_transport_socket(cluster);
      if (transport_socket != nullptr) {
        absl::string_view name = UpbStringToAbsl(
            envoy_config_core_v3_TransportSocket_name(transport_socket));
        if (name == "envoy.transport_sockets.tls") {
          auto* typed_config =
              envoy_config_core_v3_TransportSocket_typed_config(
                  transport_socket);
          if (typed_config != nullptr) {
            const upb_strview encoded_upstream_tls_context =
                google_protobuf_Any_value(typed_config);
            auto* upstream_tls_context =
                envoy_extensions_transport_sockets_tls_v3_UpstreamTlsContext_parse(
                    encoded_upstream_tls_context.data,
                    encoded_upstream_tls_context.size, context.arena);
            if (upstream_tls_context == nullptr) {
              errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                  absl::StrCat(cluster_name,
                               ": Can't decode upstream tls context.")
                      .c_str()));
              resource_names_failed->insert(cluster_name);
              continue;
            }
            auto* common_tls_context =
                envoy_extensions_transport_sockets_tls_v3_UpstreamTlsContext_common_tls_context(
                    upstream_tls_context);
            if (common_tls_context != nullptr) {
              grpc_error_handle error = CommonTlsContextParse(
                  common_tls_context, &cds_update.common_tls_context);
              if (error != GRPC_ERROR_NONE) {
                errors.push_back(grpc_error_add_child(
                    GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                        absl::StrCat(cluster_name, ": error in TLS context")
                            .c_str()),
                    error));
                resource_names_failed->insert(cluster_name);
                continue;
              }
            }
          }
          if (cds_update.common_tls_context.combined_validation_context
                  .validation_context_certificate_provider_instance
                  .instance_name.empty()) {
            errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                absl::StrCat(cluster_name,
                             "TLS configuration provided but no "
                             "validation_context_certificate_provider_instance "
                             "found.")
                    .c_str()));
            resource_names_failed->insert(cluster_name);
            continue;
          }
        }
      }
    }
    // Record LRS server name (if any).
    const envoy_config_core_v3_ConfigSource* lrs_server =
        envoy_config_cluster_v3_Cluster_lrs_server(cluster);
    if (lrs_server != nullptr) {
      if (!envoy_config_core_v3_ConfigSource_has_self(lrs_server)) {
        errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat(cluster_name, ": LRS ConfigSource is not self.")
                .c_str()));
        resource_names_failed->insert(cluster_name);
        continue;
      }
      cds_update.lrs_load_reporting_server_name.emplace("");
    }
    // The Cluster resource encodes the circuit breaking parameters in a list of
    // Thresholds messages, where each message specifies the parameters for a
    // particular RoutingPriority. we will look only at the first entry in the
    // list for priority DEFAULT and default to 1024 if not found.
    if (envoy_config_cluster_v3_Cluster_has_circuit_breakers(cluster)) {
      const envoy_config_cluster_v3_CircuitBreakers* circuit_breakers =
          envoy_config_cluster_v3_Cluster_circuit_breakers(cluster);
      size_t num_thresholds;
      const envoy_config_cluster_v3_CircuitBreakers_Thresholds* const*
          thresholds = envoy_config_cluster_v3_CircuitBreakers_thresholds(
              circuit_breakers, &num_thresholds);
      for (size_t i = 0; i < num_thresholds; ++i) {
        const auto* threshold = thresholds[i];
        if (envoy_config_cluster_v3_CircuitBreakers_Thresholds_priority(
                threshold) == envoy_config_core_v3_DEFAULT) {
          const google_protobuf_UInt32Value* max_requests =
              envoy_config_cluster_v3_CircuitBreakers_Thresholds_max_requests(
                  threshold);
          if (max_requests != nullptr) {
            cds_update.max_concurrent_requests =
                google_protobuf_UInt32Value_value(max_requests);
          }
          break;
        }
      }
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing CDS response", &errors);
}

grpc_error_handle ServerAddressParseAndAppend(
    const envoy_config_endpoint_v3_LbEndpoint* lb_endpoint,
    ServerAddressList* list) {
  // If health_status is not HEALTHY or UNKNOWN, skip this endpoint.
  const int32_t health_status =
      envoy_config_endpoint_v3_LbEndpoint_health_status(lb_endpoint);
  if (health_status != envoy_config_core_v3_UNKNOWN &&
      health_status != envoy_config_core_v3_HEALTHY) {
    return GRPC_ERROR_NONE;
  }
  // Find the ip:port.
  const envoy_config_endpoint_v3_Endpoint* endpoint =
      envoy_config_endpoint_v3_LbEndpoint_endpoint(lb_endpoint);
  const envoy_config_core_v3_Address* address =
      envoy_config_endpoint_v3_Endpoint_address(endpoint);
  const envoy_config_core_v3_SocketAddress* socket_address =
      envoy_config_core_v3_Address_socket_address(address);
  std::string address_str = UpbStringToStdString(
      envoy_config_core_v3_SocketAddress_address(socket_address));
  uint32_t port = envoy_config_core_v3_SocketAddress_port_value(socket_address);
  if (GPR_UNLIKELY(port >> 16) != 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Invalid port.");
  }
  // Populate grpc_resolved_address.
  grpc_resolved_address addr;
  grpc_error_handle error =
      grpc_string_to_sockaddr(&addr, address_str.c_str(), port);
  if (error != GRPC_ERROR_NONE) return error;
  // Append the address to the list.
  list->emplace_back(addr, nullptr);
  return GRPC_ERROR_NONE;
}

grpc_error_handle LocalityParse(
    const envoy_config_endpoint_v3_LocalityLbEndpoints* locality_lb_endpoints,
    XdsApi::EdsUpdate::Priority::Locality* output_locality, size_t* priority) {
  // Parse LB weight.
  const google_protobuf_UInt32Value* lb_weight =
      envoy_config_endpoint_v3_LocalityLbEndpoints_load_balancing_weight(
          locality_lb_endpoints);
  // If LB weight is not specified, it means this locality is assigned no load.
  // TODO(juanlishen): When we support CDS to configure the inter-locality
  // policy, we should change the LB weight handling.
  output_locality->lb_weight =
      lb_weight != nullptr ? google_protobuf_UInt32Value_value(lb_weight) : 0;
  if (output_locality->lb_weight == 0) return GRPC_ERROR_NONE;
  // Parse locality name.
  const envoy_config_core_v3_Locality* locality =
      envoy_config_endpoint_v3_LocalityLbEndpoints_locality(
          locality_lb_endpoints);
  if (locality == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty locality.");
  }
  std::string region =
      UpbStringToStdString(envoy_config_core_v3_Locality_region(locality));
  std::string zone =
      UpbStringToStdString(envoy_config_core_v3_Locality_region(locality));
  std::string sub_zone =
      UpbStringToStdString(envoy_config_core_v3_Locality_sub_zone(locality));
  output_locality->name = MakeRefCounted<XdsLocalityName>(
      std::move(region), std::move(zone), std::move(sub_zone));
  // Parse the addresses.
  size_t size;
  const envoy_config_endpoint_v3_LbEndpoint* const* lb_endpoints =
      envoy_config_endpoint_v3_LocalityLbEndpoints_lb_endpoints(
          locality_lb_endpoints, &size);
  for (size_t i = 0; i < size; ++i) {
    grpc_error_handle error = ServerAddressParseAndAppend(
        lb_endpoints[i], &output_locality->endpoints);
    if (error != GRPC_ERROR_NONE) return error;
  }
  // Parse the priority.
  *priority = envoy_config_endpoint_v3_LocalityLbEndpoints_priority(
      locality_lb_endpoints);
  return GRPC_ERROR_NONE;
}

grpc_error_handle DropParseAndAppend(
    const envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload*
        drop_overload,
    XdsApi::EdsUpdate::DropConfig* drop_config) {
  // Get the category.
  std::string category = UpbStringToStdString(
      envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload_category(
          drop_overload));
  if (category.empty()) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty drop category name");
  }
  // Get the drop rate (per million).
  const envoy_type_v3_FractionalPercent* drop_percentage =
      envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload_drop_percentage(
          drop_overload);
  uint32_t numerator =
      envoy_type_v3_FractionalPercent_numerator(drop_percentage);
  const auto denominator =
      static_cast<envoy_type_v3_FractionalPercent_DenominatorType>(
          envoy_type_v3_FractionalPercent_denominator(drop_percentage));
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
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unknown denominator type");
  }
  // Cap numerator to 1000000.
  numerator = GPR_MIN(numerator, 1000000);
  drop_config->AddCategory(std::move(category), numerator);
  return GRPC_ERROR_NONE;
}

grpc_error_handle EdsResponseParse(
    const EncodingContext& context,
    const envoy_service_discovery_v3_DiscoveryResponse* response,
    const std::set<absl::string_view>& expected_eds_service_names,
    XdsApi::EdsUpdateMap* eds_update_map,
    std::set<std::string>* resource_names_failed) {
  std::vector<grpc_error_handle> errors;
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_service_discovery_v3_DiscoveryResponse_resources(response, &size);
  for (size_t i = 0; i < size; ++i) {
    // Check the type_url of the resource.
    absl::string_view type_url =
        UpbStringToAbsl(google_protobuf_Any_type_url(resources[i]));
    if (!IsEds(type_url)) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("resource index ", i, ": Resource is not EDS.")
              .c_str()));
      continue;
    }
    // Get the cluster_load_assignment.
    upb_strview encoded_cluster_load_assignment =
        google_protobuf_Any_value(resources[i]);
    envoy_config_endpoint_v3_ClusterLoadAssignment* cluster_load_assignment =
        envoy_config_endpoint_v3_ClusterLoadAssignment_parse(
            encoded_cluster_load_assignment.data,
            encoded_cluster_load_assignment.size, context.arena);
    if (cluster_load_assignment == nullptr) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("resource index ", i,
                       ": Can't parse cluster_load_assignment.")
              .c_str()));
      continue;
    }
    MaybeLogClusterLoadAssignment(context, cluster_load_assignment);
    // Check the EDS service name.  Ignore unexpected names.
    std::string eds_service_name = UpbStringToStdString(
        envoy_config_endpoint_v3_ClusterLoadAssignment_cluster_name(
            cluster_load_assignment));
    if (expected_eds_service_names.find(eds_service_name) ==
        expected_eds_service_names.end()) {
      continue;
    }
    // Fail on duplicate resources.
    if (eds_update_map->find(eds_service_name) != eds_update_map->end()) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("duplicate resource name \"", eds_service_name, "\"")
              .c_str()));
      resource_names_failed->insert(eds_service_name);
      continue;
    }
    // Serialize into JSON and store it in the EdsUpdateMap
    XdsApi::EdsResourceData& eds_resource_data =
        (*eds_update_map)[eds_service_name];
    XdsApi::EdsUpdate& eds_update = eds_resource_data.resource;
    eds_resource_data.serialized_proto =
        UpbStringToStdString(encoded_cluster_load_assignment);
    // Get the endpoints.
    size_t locality_size;
    const envoy_config_endpoint_v3_LocalityLbEndpoints* const* endpoints =
        envoy_config_endpoint_v3_ClusterLoadAssignment_endpoints(
            cluster_load_assignment, &locality_size);
    grpc_error_handle error = GRPC_ERROR_NONE;
    for (size_t j = 0; j < locality_size; ++j) {
      size_t priority;
      XdsApi::EdsUpdate::Priority::Locality locality;
      error = LocalityParse(endpoints[j], &locality, &priority);
      if (error != GRPC_ERROR_NONE) break;
      // Filter out locality with weight 0.
      if (locality.lb_weight == 0) continue;
      // Make sure prorities is big enough. Note that they might not
      // arrive in priority order.
      while (eds_update.priorities.size() < priority + 1) {
        eds_update.priorities.emplace_back();
      }
      eds_update.priorities[priority].localities.emplace(locality.name.get(),
                                                         std::move(locality));
    }
    if (error != GRPC_ERROR_NONE) {
      errors.push_back(grpc_error_add_child(
          GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat(eds_service_name, ": locality validation error")
                  .c_str()),
          error));
      resource_names_failed->insert(eds_service_name);
      continue;
    }
    for (const auto& priority : eds_update.priorities) {
      if (priority.localities.empty()) {
        errors.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat(eds_service_name, ": sparse priority list").c_str()));
        resource_names_failed->insert(eds_service_name);
        continue;
      }
    }
    // Get the drop config.
    eds_update.drop_config = MakeRefCounted<XdsApi::EdsUpdate::DropConfig>();
    const envoy_config_endpoint_v3_ClusterLoadAssignment_Policy* policy =
        envoy_config_endpoint_v3_ClusterLoadAssignment_policy(
            cluster_load_assignment);
    if (policy != nullptr) {
      size_t drop_size;
      const envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload* const*
          drop_overload =
              envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_drop_overloads(
                  policy, &drop_size);
      for (size_t j = 0; j < drop_size; ++j) {
        error =
            DropParseAndAppend(drop_overload[j], eds_update.drop_config.get());
        if (error != GRPC_ERROR_NONE) break;
      }
      if (error != GRPC_ERROR_NONE) {
        errors.push_back(grpc_error_add_child(
            GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                absl::StrCat(eds_service_name, ": drop config validation error")
                    .c_str()),
            error));
        resource_names_failed->insert(eds_service_name);
        continue;
      }
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing EDS response", &errors);
}

std::string TypeUrlInternalToExternal(absl::string_view type_url) {
  if (type_url == kLdsV2TypeUrl) {
    return XdsApi::kLdsTypeUrl;
  } else if (type_url == kRdsV2TypeUrl) {
    return XdsApi::kRdsTypeUrl;
  } else if (type_url == kCdsV2TypeUrl) {
    return XdsApi::kCdsTypeUrl;
  } else if (type_url == kEdsV2TypeUrl) {
    return XdsApi::kEdsTypeUrl;
  }
  return std::string(type_url);
}

template <typename UpdateMap>
void MoveUpdatesToFailedSet(UpdateMap* update_map,
                            std::set<std::string>* resource_names_failed) {
  for (const auto& p : *update_map) {
    resource_names_failed->insert(p.first);
  }
  update_map->clear();
}

}  // namespace

XdsApi::AdsParseResult XdsApi::ParseAdsResponse(
    const XdsBootstrap::XdsServer& server, const grpc_slice& encoded_response,
    const std::set<absl::string_view>& expected_listener_names,
    const std::set<absl::string_view>& expected_route_configuration_names,
    const std::set<absl::string_view>& expected_cluster_names,
    const std::set<absl::string_view>& expected_eds_service_names) {
  AdsParseResult result;
  upb::Arena arena;
  const EncodingContext context = {client_, tracer_, symtab_.ptr(), arena.ptr(),
                                   server.ShouldUseV3()};
  // Decode the response.
  const envoy_service_discovery_v3_DiscoveryResponse* response =
      envoy_service_discovery_v3_DiscoveryResponse_parse(
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(encoded_response)),
          GRPC_SLICE_LENGTH(encoded_response), arena.ptr());
  // If decoding fails, output an empty type_url and return.
  if (response == nullptr) {
    result.parse_error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Can't decode DiscoveryResponse.");
    return result;
  }
  MaybeLogDiscoveryResponse(context, response);
  // Record the type_url, the version_info, and the nonce of the response.
  result.type_url = TypeUrlInternalToExternal(UpbStringToAbsl(
      envoy_service_discovery_v3_DiscoveryResponse_type_url(response)));
  result.version = UpbStringToStdString(
      envoy_service_discovery_v3_DiscoveryResponse_version_info(response));
  result.nonce = UpbStringToStdString(
      envoy_service_discovery_v3_DiscoveryResponse_nonce(response));
  // Parse the response according to the resource type.
  if (IsLds(result.type_url)) {
    result.parse_error =
        LdsResponseParse(context, response, expected_listener_names,
                         &result.lds_update_map, &result.resource_names_failed);
    if (result.parse_error != GRPC_ERROR_NONE) {
      MoveUpdatesToFailedSet(&result.lds_update_map,
                             &result.resource_names_failed);
    }
  } else if (IsRds(result.type_url)) {
    result.parse_error =
        RdsResponseParse(context, response, expected_route_configuration_names,
                         &result.rds_update_map, &result.resource_names_failed);
    if (result.parse_error != GRPC_ERROR_NONE) {
      MoveUpdatesToFailedSet(&result.rds_update_map,
                             &result.resource_names_failed);
    }
  } else if (IsCds(result.type_url)) {
    result.parse_error =
        CdsResponseParse(context, response, expected_cluster_names,
                         &result.cds_update_map, &result.resource_names_failed);
    if (result.parse_error != GRPC_ERROR_NONE) {
      MoveUpdatesToFailedSet(&result.cds_update_map,
                             &result.resource_names_failed);
    }
  } else if (IsEds(result.type_url)) {
    result.parse_error =
        EdsResponseParse(context, response, expected_eds_service_names,
                         &result.eds_update_map, &result.resource_names_failed);
    if (result.parse_error != GRPC_ERROR_NONE) {
      MoveUpdatesToFailedSet(&result.eds_update_map,
                             &result.resource_names_failed);
    }
  }
  return result;
}

namespace {

void MaybeLogLrsRequest(
    const EncodingContext& context,
    const envoy_service_load_stats_v3_LoadStatsRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_service_load_stats_v3_LoadStatsRequest_getmsgdef(context.symtab);
    char buf[10240];
    upb_text_encode(request, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] constructed LRS request: %s",
            context.client, buf);
  }
}

grpc_slice SerializeLrsRequest(
    const EncodingContext& context,
    const envoy_service_load_stats_v3_LoadStatsRequest* request) {
  size_t output_length;
  char* output = envoy_service_load_stats_v3_LoadStatsRequest_serialize(
      request, context.arena, &output_length);
  return grpc_slice_from_copied_buffer(output, output_length);
}

}  // namespace

grpc_slice XdsApi::CreateLrsInitialRequest(
    const XdsBootstrap::XdsServer& server) {
  upb::Arena arena;
  const EncodingContext context = {client_, tracer_, symtab_.ptr(), arena.ptr(),
                                   server.ShouldUseV3()};
  // Create a request.
  envoy_service_load_stats_v3_LoadStatsRequest* request =
      envoy_service_load_stats_v3_LoadStatsRequest_new(arena.ptr());
  // Populate node.
  envoy_config_core_v3_Node* node_msg =
      envoy_service_load_stats_v3_LoadStatsRequest_mutable_node(request,
                                                                arena.ptr());
  PopulateNode(context, node_, build_version_, user_agent_name_, node_msg);
  envoy_config_core_v3_Node_add_client_features(
      node_msg, upb_strview_makez("envoy.lrs.supports_send_all_clusters"),
      arena.ptr());
  MaybeLogLrsRequest(context, request);
  return SerializeLrsRequest(context, request);
}

namespace {

void LocalityStatsPopulate(
    const EncodingContext& context,
    envoy_config_endpoint_v3_UpstreamLocalityStats* output,
    const XdsLocalityName& locality_name,
    const XdsClusterLocalityStats::Snapshot& snapshot) {
  // Set locality.
  envoy_config_core_v3_Locality* locality =
      envoy_config_endpoint_v3_UpstreamLocalityStats_mutable_locality(
          output, context.arena);
  if (!locality_name.region().empty()) {
    envoy_config_core_v3_Locality_set_region(
        locality, StdStringToUpbString(locality_name.region()));
  }
  if (!locality_name.zone().empty()) {
    envoy_config_core_v3_Locality_set_zone(
        locality, StdStringToUpbString(locality_name.zone()));
  }
  if (!locality_name.sub_zone().empty()) {
    envoy_config_core_v3_Locality_set_sub_zone(
        locality, StdStringToUpbString(locality_name.sub_zone()));
  }
  // Set total counts.
  envoy_config_endpoint_v3_UpstreamLocalityStats_set_total_successful_requests(
      output, snapshot.total_successful_requests);
  envoy_config_endpoint_v3_UpstreamLocalityStats_set_total_requests_in_progress(
      output, snapshot.total_requests_in_progress);
  envoy_config_endpoint_v3_UpstreamLocalityStats_set_total_error_requests(
      output, snapshot.total_error_requests);
  envoy_config_endpoint_v3_UpstreamLocalityStats_set_total_issued_requests(
      output, snapshot.total_issued_requests);
  // Add backend metrics.
  for (const auto& p : snapshot.backend_metrics) {
    const std::string& metric_name = p.first;
    const XdsClusterLocalityStats::BackendMetric& metric_value = p.second;
    envoy_config_endpoint_v3_EndpointLoadMetricStats* load_metric =
        envoy_config_endpoint_v3_UpstreamLocalityStats_add_load_metric_stats(
            output, context.arena);
    envoy_config_endpoint_v3_EndpointLoadMetricStats_set_metric_name(
        load_metric, StdStringToUpbString(metric_name));
    envoy_config_endpoint_v3_EndpointLoadMetricStats_set_num_requests_finished_with_metric(
        load_metric, metric_value.num_requests_finished_with_metric);
    envoy_config_endpoint_v3_EndpointLoadMetricStats_set_total_metric_value(
        load_metric, metric_value.total_metric_value);
  }
}

}  // namespace

grpc_slice XdsApi::CreateLrsRequest(
    ClusterLoadReportMap cluster_load_report_map) {
  upb::Arena arena;
  const EncodingContext context = {client_, tracer_, symtab_.ptr(), arena.ptr(),
                                   false};
  // Create a request.
  envoy_service_load_stats_v3_LoadStatsRequest* request =
      envoy_service_load_stats_v3_LoadStatsRequest_new(arena.ptr());
  for (auto& p : cluster_load_report_map) {
    const std::string& cluster_name = p.first.first;
    const std::string& eds_service_name = p.first.second;
    const ClusterLoadReport& load_report = p.second;
    // Add cluster stats.
    envoy_config_endpoint_v3_ClusterStats* cluster_stats =
        envoy_service_load_stats_v3_LoadStatsRequest_add_cluster_stats(
            request, arena.ptr());
    // Set the cluster name.
    envoy_config_endpoint_v3_ClusterStats_set_cluster_name(
        cluster_stats, StdStringToUpbString(cluster_name));
    // Set EDS service name, if non-empty.
    if (!eds_service_name.empty()) {
      envoy_config_endpoint_v3_ClusterStats_set_cluster_service_name(
          cluster_stats, StdStringToUpbString(eds_service_name));
    }
    // Add locality stats.
    for (const auto& p : load_report.locality_stats) {
      const XdsLocalityName& locality_name = *p.first;
      const auto& snapshot = p.second;
      envoy_config_endpoint_v3_UpstreamLocalityStats* locality_stats =
          envoy_config_endpoint_v3_ClusterStats_add_upstream_locality_stats(
              cluster_stats, arena.ptr());
      LocalityStatsPopulate(context, locality_stats, locality_name, snapshot);
    }
    // Add dropped requests.
    uint64_t total_dropped_requests = 0;
    for (const auto& p : load_report.dropped_requests.categorized_drops) {
      const std::string& category = p.first;
      const uint64_t count = p.second;
      envoy_config_endpoint_v3_ClusterStats_DroppedRequests* dropped_requests =
          envoy_config_endpoint_v3_ClusterStats_add_dropped_requests(
              cluster_stats, arena.ptr());
      envoy_config_endpoint_v3_ClusterStats_DroppedRequests_set_category(
          dropped_requests, StdStringToUpbString(category));
      envoy_config_endpoint_v3_ClusterStats_DroppedRequests_set_dropped_count(
          dropped_requests, count);
      total_dropped_requests += count;
    }
    total_dropped_requests += load_report.dropped_requests.uncategorized_drops;
    // Set total dropped requests.
    envoy_config_endpoint_v3_ClusterStats_set_total_dropped_requests(
        cluster_stats, total_dropped_requests);
    // Set real load report interval.
    gpr_timespec timespec =
        grpc_millis_to_timespec(load_report.load_report_interval, GPR_TIMESPAN);
    google_protobuf_Duration* load_report_interval =
        envoy_config_endpoint_v3_ClusterStats_mutable_load_report_interval(
            cluster_stats, arena.ptr());
    google_protobuf_Duration_set_seconds(load_report_interval, timespec.tv_sec);
    google_protobuf_Duration_set_nanos(load_report_interval, timespec.tv_nsec);
  }
  MaybeLogLrsRequest(context, request);
  return SerializeLrsRequest(context, request);
}

grpc_error_handle XdsApi::ParseLrsResponse(
    const grpc_slice& encoded_response, bool* send_all_clusters,
    std::set<std::string>* cluster_names,
    grpc_millis* load_reporting_interval) {
  upb::Arena arena;
  // Decode the response.
  const envoy_service_load_stats_v3_LoadStatsResponse* decoded_response =
      envoy_service_load_stats_v3_LoadStatsResponse_parse(
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(encoded_response)),
          GRPC_SLICE_LENGTH(encoded_response), arena.ptr());
  // Parse the response.
  if (decoded_response == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Can't decode response.");
  }
  // Check send_all_clusters.
  if (envoy_service_load_stats_v3_LoadStatsResponse_send_all_clusters(
          decoded_response)) {
    *send_all_clusters = true;
  } else {
    // Store the cluster names.
    size_t size;
    const upb_strview* clusters =
        envoy_service_load_stats_v3_LoadStatsResponse_clusters(decoded_response,
                                                               &size);
    for (size_t i = 0; i < size; ++i) {
      cluster_names->emplace(UpbStringToStdString(clusters[i]));
    }
  }
  // Get the load report interval.
  const google_protobuf_Duration* load_reporting_interval_duration =
      envoy_service_load_stats_v3_LoadStatsResponse_load_reporting_interval(
          decoded_response);
  gpr_timespec timespec{
      google_protobuf_Duration_seconds(load_reporting_interval_duration),
      google_protobuf_Duration_nanos(load_reporting_interval_duration),
      GPR_TIMESPAN};
  *load_reporting_interval = gpr_time_to_millis(timespec);
  return GRPC_ERROR_NONE;
}

namespace {
google_protobuf_Timestamp* GrpcMillisToTimestamp(const EncodingContext& context,
                                                 grpc_millis value) {
  google_protobuf_Timestamp* timestamp =
      google_protobuf_Timestamp_new(context.arena);
  gpr_timespec timespec = grpc_millis_to_timespec(value, GPR_CLOCK_REALTIME);
  google_protobuf_Timestamp_set_seconds(timestamp, timespec.tv_sec);
  google_protobuf_Timestamp_set_nanos(timestamp, timespec.tv_nsec);
  return timestamp;
}

envoy_admin_v3_UpdateFailureState* CreateUpdateFailureStateUpb(
    const EncodingContext& context,
    const XdsApi::ResourceMetadata* resource_metadata) {
  auto* update_failure_state =
      envoy_admin_v3_UpdateFailureState_new(context.arena);
  envoy_admin_v3_UpdateFailureState_set_details(
      update_failure_state,
      StdStringToUpbString(resource_metadata->failed_details));
  envoy_admin_v3_UpdateFailureState_set_version_info(
      update_failure_state,
      StdStringToUpbString(resource_metadata->failed_version));
  envoy_admin_v3_UpdateFailureState_set_last_update_attempt(
      update_failure_state,
      GrpcMillisToTimestamp(context, resource_metadata->failed_update_time));
  return update_failure_state;
}

void DumpLdsConfig(const EncodingContext& context,
                   const XdsApi::ResourceTypeMetadata& resource_type_metadata,
                   envoy_service_status_v3_PerXdsConfig* per_xds_config) {
  upb_strview kLdsTypeUrlUpb = upb_strview_makez(XdsApi::kLdsTypeUrl);
  auto* listener_config_dump =
      envoy_service_status_v3_PerXdsConfig_mutable_listener_config(
          per_xds_config, context.arena);
  envoy_admin_v3_ListenersConfigDump_set_version_info(
      listener_config_dump,
      StdStringToUpbString(resource_type_metadata.version));
  for (auto& p : resource_type_metadata.resource_metadata_map) {
    absl::string_view name = p.first;
    const XdsApi::ResourceMetadata* meta = p.second;
    const upb_strview name_upb = StdStringToUpbString(name);
    auto* dynamic_listener =
        envoy_admin_v3_ListenersConfigDump_add_dynamic_listeners(
            listener_config_dump, context.arena);
    envoy_admin_v3_ListenersConfigDump_DynamicListener_set_name(
        dynamic_listener, name_upb);
    envoy_admin_v3_ListenersConfigDump_DynamicListener_set_client_status(
        dynamic_listener, meta->client_status);
    if (!meta->serialized_proto.empty()) {
      // Set in-effective listeners
      auto* dynamic_listener_state =
          envoy_admin_v3_ListenersConfigDump_DynamicListener_mutable_active_state(
              dynamic_listener, context.arena);
      envoy_admin_v3_ListenersConfigDump_DynamicListenerState_set_version_info(
          dynamic_listener_state, StdStringToUpbString(meta->version));
      envoy_admin_v3_ListenersConfigDump_DynamicListenerState_set_last_updated(
          dynamic_listener_state,
          GrpcMillisToTimestamp(context, meta->update_time));
      auto* listener_any =
          envoy_admin_v3_ListenersConfigDump_DynamicListenerState_mutable_listener(
              dynamic_listener_state, context.arena);
      google_protobuf_Any_set_type_url(listener_any, kLdsTypeUrlUpb);
      google_protobuf_Any_set_value(
          listener_any, StdStringToUpbString(meta->serialized_proto));
    }
    if (meta->client_status == XdsApi::ResourceMetadata::NACKED) {
      // Set error_state if NACKED
      envoy_admin_v3_ListenersConfigDump_DynamicListener_set_error_state(
          dynamic_listener, CreateUpdateFailureStateUpb(context, meta));
    }
  }
}

void DumpRdsConfig(const EncodingContext& context,
                   const XdsApi::ResourceTypeMetadata& resource_type_metadata,
                   envoy_service_status_v3_PerXdsConfig* per_xds_config) {
  upb_strview kRdsTypeUrlUpb = upb_strview_makez(XdsApi::kRdsTypeUrl);
  auto* route_config_dump =
      envoy_service_status_v3_PerXdsConfig_mutable_route_config(per_xds_config,
                                                                context.arena);
  for (auto& p : resource_type_metadata.resource_metadata_map) {
    absl::string_view name = p.first;
    const XdsApi::ResourceMetadata* meta = p.second;
    const upb_strview name_upb = StdStringToUpbString(name);
    auto* dynamic_route_config =
        envoy_admin_v3_RoutesConfigDump_add_dynamic_route_configs(
            route_config_dump, context.arena);
    envoy_admin_v3_RoutesConfigDump_DynamicRouteConfig_set_client_status(
        dynamic_route_config, meta->client_status);
    auto* route_config_any =
        envoy_admin_v3_RoutesConfigDump_DynamicRouteConfig_mutable_route_config(
            dynamic_route_config, context.arena);
    if (!meta->serialized_proto.empty()) {
      // Set in-effective route configs
      envoy_admin_v3_RoutesConfigDump_DynamicRouteConfig_set_version_info(
          dynamic_route_config, StdStringToUpbString(meta->version));
      envoy_admin_v3_RoutesConfigDump_DynamicRouteConfig_set_last_updated(
          dynamic_route_config,
          GrpcMillisToTimestamp(context, meta->update_time));
      google_protobuf_Any_set_type_url(route_config_any, kRdsTypeUrlUpb);
      google_protobuf_Any_set_value(
          route_config_any, StdStringToUpbString(meta->serialized_proto));
    } else {
      // If there isn't a working route config, we still need to print the
      // name.
      auto* route_config =
          envoy_config_route_v3_RouteConfiguration_new(context.arena);
      envoy_config_route_v3_RouteConfiguration_set_name(route_config, name_upb);
      size_t length;
      char* bytes = envoy_config_route_v3_RouteConfiguration_serialize(
          route_config, context.arena, &length);
      google_protobuf_Any_set_type_url(route_config_any, kRdsTypeUrlUpb);
      google_protobuf_Any_set_value(route_config_any,
                                    upb_strview_make(bytes, length));
    }
    if (meta->client_status == XdsApi::ResourceMetadata::NACKED) {
      // Set error_state if NACKED
      envoy_admin_v3_RoutesConfigDump_DynamicRouteConfig_set_error_state(
          dynamic_route_config, CreateUpdateFailureStateUpb(context, meta));
    }
  }
}

void DumpCdsConfig(const EncodingContext& context,
                   const XdsApi::ResourceTypeMetadata& resource_type_metadata,
                   envoy_service_status_v3_PerXdsConfig* per_xds_config) {
  upb_strview kCdsTypeUrlUpb = upb_strview_makez(XdsApi::kCdsTypeUrl);
  auto* cluster_config_dump =
      envoy_service_status_v3_PerXdsConfig_mutable_cluster_config(
          per_xds_config, context.arena);
  envoy_admin_v3_ClustersConfigDump_set_version_info(
      cluster_config_dump,
      StdStringToUpbString(resource_type_metadata.version));
  for (auto& p : resource_type_metadata.resource_metadata_map) {
    absl::string_view name = p.first;
    const XdsApi::ResourceMetadata* meta = p.second;
    const upb_strview name_upb = StdStringToUpbString(name);
    auto* dynamic_cluster =
        envoy_admin_v3_ClustersConfigDump_add_dynamic_active_clusters(
            cluster_config_dump, context.arena);
    envoy_admin_v3_ClustersConfigDump_DynamicCluster_set_client_status(
        dynamic_cluster, meta->client_status);
    auto* cluster_any =
        envoy_admin_v3_ClustersConfigDump_DynamicCluster_mutable_cluster(
            dynamic_cluster, context.arena);
    if (!meta->serialized_proto.empty()) {
      // Set in-effective clusters
      envoy_admin_v3_ClustersConfigDump_DynamicCluster_set_version_info(
          dynamic_cluster, StdStringToUpbString(meta->version));
      envoy_admin_v3_ClustersConfigDump_DynamicCluster_set_last_updated(
          dynamic_cluster, GrpcMillisToTimestamp(context, meta->update_time));
      google_protobuf_Any_set_type_url(cluster_any, kCdsTypeUrlUpb);
      google_protobuf_Any_set_value(
          cluster_any, StdStringToUpbString(meta->serialized_proto));
    } else {
      // If there isn't a working cluster, we still need to print the name.
      auto* cluster = envoy_config_cluster_v3_Cluster_new(context.arena);
      envoy_config_cluster_v3_Cluster_set_name(cluster, name_upb);
      size_t length;
      char* bytes = envoy_config_cluster_v3_Cluster_serialize(
          cluster, context.arena, &length);
      google_protobuf_Any_set_type_url(cluster_any, kCdsTypeUrlUpb);
      google_protobuf_Any_set_value(cluster_any,
                                    upb_strview_make(bytes, length));
    }
    if (meta->client_status == XdsApi::ResourceMetadata::NACKED) {
      // Set error_state if NACKED
      envoy_admin_v3_ClustersConfigDump_DynamicCluster_set_error_state(
          dynamic_cluster, CreateUpdateFailureStateUpb(context, meta));
    }
  }
}

void DumpEdsConfig(const EncodingContext& context,
                   const XdsApi::ResourceTypeMetadata& resource_type_metadata,
                   envoy_service_status_v3_PerXdsConfig* per_xds_config) {
  upb_strview kEdsTypeUrlUpb = upb_strview_makez(XdsApi::kEdsTypeUrl);
  auto* endpoint_config_dump =
      envoy_service_status_v3_PerXdsConfig_mutable_endpoint_config(
          per_xds_config, context.arena);
  for (auto& p : resource_type_metadata.resource_metadata_map) {
    absl::string_view name = p.first;
    const XdsApi::ResourceMetadata* meta = p.second;
    const upb_strview name_upb = StdStringToUpbString(name);
    auto* dynamic_endpoint =
        envoy_admin_v3_EndpointsConfigDump_add_dynamic_endpoint_configs(
            endpoint_config_dump, context.arena);
    envoy_admin_v3_EndpointsConfigDump_DynamicEndpointConfig_set_client_status(
        dynamic_endpoint, meta->client_status);
    auto* endpoint_any =
        envoy_admin_v3_EndpointsConfigDump_DynamicEndpointConfig_mutable_endpoint_config(
            dynamic_endpoint, context.arena);
    if (!meta->serialized_proto.empty()) {
      // Set in-effective endpoints
      envoy_admin_v3_EndpointsConfigDump_DynamicEndpointConfig_set_version_info(
          dynamic_endpoint, StdStringToUpbString(meta->version));
      envoy_admin_v3_EndpointsConfigDump_DynamicEndpointConfig_set_last_updated(
          dynamic_endpoint, GrpcMillisToTimestamp(context, meta->update_time));
      google_protobuf_Any_set_type_url(endpoint_any, kEdsTypeUrlUpb);
      google_protobuf_Any_set_value(
          endpoint_any, StdStringToUpbString(meta->serialized_proto));
    } else {
      // If there isn't a working endpoint, we still need to print the name.
      auto* cluster_load_assignment =
          envoy_config_endpoint_v3_ClusterLoadAssignment_new(context.arena);
      envoy_config_endpoint_v3_ClusterLoadAssignment_set_cluster_name(
          cluster_load_assignment, name_upb);
      size_t length;
      char* bytes = envoy_config_endpoint_v3_ClusterLoadAssignment_serialize(
          cluster_load_assignment, context.arena, &length);
      google_protobuf_Any_set_type_url(endpoint_any, kEdsTypeUrlUpb);
      google_protobuf_Any_set_value(endpoint_any,
                                    upb_strview_make(bytes, length));
    }
    if (meta->client_status == XdsApi::ResourceMetadata::NACKED) {
      // Set error_state if NACKED
      envoy_admin_v3_EndpointsConfigDump_DynamicEndpointConfig_set_error_state(
          dynamic_endpoint, CreateUpdateFailureStateUpb(context, meta));
    }
  }
}

}  // namespace

std::string XdsApi::AssembleClientConfig(
    const ResourceTypeMetadataMap& resource_type_metadata_map) {
  upb::Arena arena;
  // Create the ClientConfig for resource metadata from XdsClient
  auto* client_config = envoy_service_status_v3_ClientConfig_new(arena.ptr());
  // Fill-in the node information
  auto* node = envoy_service_status_v3_ClientConfig_mutable_node(client_config,
                                                                 arena.ptr());
  const EncodingContext context = {client_, tracer_, symtab_.ptr(), arena.ptr(),
                                   true};
  PopulateNode(context, node_, build_version_, user_agent_name_, node);
  // Dump each xDS-type config into PerXdsConfig
  for (auto& p : resource_type_metadata_map) {
    absl::string_view type_url = p.first;
    const ResourceTypeMetadata& resource_type_metadata = p.second;
    if (type_url == kLdsTypeUrl) {
      auto* per_xds_config =
          envoy_service_status_v3_ClientConfig_add_xds_config(client_config,
                                                              context.arena);
      DumpLdsConfig(context, resource_type_metadata, per_xds_config);
    } else if (type_url == kRdsTypeUrl) {
      auto* per_xds_config =
          envoy_service_status_v3_ClientConfig_add_xds_config(client_config,
                                                              context.arena);
      DumpRdsConfig(context, resource_type_metadata, per_xds_config);
    } else if (type_url == kCdsTypeUrl) {
      auto* per_xds_config =
          envoy_service_status_v3_ClientConfig_add_xds_config(client_config,
                                                              context.arena);
      DumpCdsConfig(context, resource_type_metadata, per_xds_config);
    } else if (type_url == kEdsTypeUrl) {
      auto* per_xds_config =
          envoy_service_status_v3_ClientConfig_add_xds_config(client_config,
                                                              context.arena);
      DumpEdsConfig(context, resource_type_metadata, per_xds_config);
    } else {
      gpr_log(GPR_ERROR, "invalid type_url %s", std::string(type_url).c_str());
      return "";
    }
  }
  // Serialize the upb message to bytes
  size_t output_length;
  char* output = envoy_service_status_v3_ClientConfig_serialize(
      client_config, arena.ptr(), &output_length);
  return std::string(output, output_length);
}

}  // namespace grpc_core
