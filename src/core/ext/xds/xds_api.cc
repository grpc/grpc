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

#include "upb/upb.hpp"

#include <grpc/impl/codegen/log.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/xds/xds_api.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/slice/slice_utils.h"

#include "envoy/config/cluster/v3/circuit_breaker.upb.h"
#include "envoy/config/cluster/v3/cluster.upb.h"
#include "envoy/config/cluster/v3/cluster.upbdefs.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/config_source.upb.h"
#include "envoy/config/core/v3/health_check.upb.h"
#include "envoy/config/core/v3/protocol.upb.h"
#include "envoy/config/endpoint/v3/endpoint.upb.h"
#include "envoy/config/endpoint/v3/endpoint.upbdefs.h"
#include "envoy/config/endpoint/v3/endpoint_components.upb.h"
#include "envoy/config/endpoint/v3/load_report.upb.h"
#include "envoy/config/listener/v3/api_listener.upb.h"
#include "envoy/config/listener/v3/listener.upb.h"
#include "envoy/config/route/v3/route.upb.h"
#include "envoy/config/route/v3/route.upbdefs.h"
#include "envoy/config/route/v3/route_components.upb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.upb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upb.h"
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
#include "envoy/type/matcher/v3/regex.upb.h"
#include "envoy/type/matcher/v3/string.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "envoy/type/v3/range.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "google/rpc/status.upb.h"
#include "upb/text_encode.h"
#include "upb/upb.h"

namespace grpc_core {

// TODO (donnadionne): Check to see if timeout is enabled, this will be
// removed once timeout feature is fully integration-tested and enabled by
// default.
bool XdsTimeoutEnabled() {
  char* value = gpr_getenv("GRPC_XDS_EXPERIMENTAL_ENABLE_TIMEOUT");
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
// XdsApi::Route::Matchers::PathMatcher
//

XdsApi::Route::Matchers::PathMatcher::PathMatcher(const PathMatcher& other)
    : type(other.type), case_sensitive(other.case_sensitive) {
  if (type == PathMatcherType::REGEX) {
    RE2::Options options;
    options.set_case_sensitive(case_sensitive);
    regex_matcher =
        absl::make_unique<RE2>(other.regex_matcher->pattern(), options);
  } else {
    string_matcher = other.string_matcher;
  }
}

XdsApi::Route::Matchers::PathMatcher& XdsApi::Route::Matchers::PathMatcher::
operator=(const PathMatcher& other) {
  type = other.type;
  case_sensitive = other.case_sensitive;
  if (type == PathMatcherType::REGEX) {
    RE2::Options options;
    options.set_case_sensitive(case_sensitive);
    regex_matcher =
        absl::make_unique<RE2>(other.regex_matcher->pattern(), options);
  } else {
    string_matcher = other.string_matcher;
  }
  return *this;
}

bool XdsApi::Route::Matchers::PathMatcher::operator==(
    const PathMatcher& other) const {
  if (type != other.type) return false;
  if (case_sensitive != other.case_sensitive) return false;
  if (type == PathMatcherType::REGEX) {
    // Should never be null.
    if (regex_matcher == nullptr || other.regex_matcher == nullptr) {
      return false;
    }
    return regex_matcher->pattern() == other.regex_matcher->pattern();
  }
  return string_matcher == other.string_matcher;
}

std::string XdsApi::Route::Matchers::PathMatcher::ToString() const {
  std::string path_type_string;
  switch (type) {
    case PathMatcherType::PATH:
      path_type_string = "path match";
      break;
    case PathMatcherType::PREFIX:
      path_type_string = "prefix match";
      break;
    case PathMatcherType::REGEX:
      path_type_string = "regex match";
      break;
    default:
      break;
  }
  return absl::StrFormat("Path %s:%s%s", path_type_string,
                         type == PathMatcherType::REGEX
                             ? regex_matcher->pattern()
                             : string_matcher,
                         case_sensitive ? "" : "[case_sensitive=false]");
}

//
// XdsApi::Route::Matchers::HeaderMatcher
//

XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcher(
    const HeaderMatcher& other)
    : name(other.name), type(other.type), invert_match(other.invert_match) {
  switch (type) {
    case HeaderMatcherType::REGEX:
      regex_match = absl::make_unique<RE2>(other.regex_match->pattern());
      break;
    case HeaderMatcherType::RANGE:
      range_start = other.range_start;
      range_end = other.range_end;
      break;
    case HeaderMatcherType::PRESENT:
      present_match = other.present_match;
      break;
    default:
      string_matcher = other.string_matcher;
  }
}

XdsApi::Route::Matchers::HeaderMatcher& XdsApi::Route::Matchers::HeaderMatcher::
operator=(const HeaderMatcher& other) {
  name = other.name;
  type = other.type;
  invert_match = other.invert_match;
  switch (type) {
    case HeaderMatcherType::REGEX:
      regex_match = absl::make_unique<RE2>(other.regex_match->pattern());
      break;
    case HeaderMatcherType::RANGE:
      range_start = other.range_start;
      range_end = other.range_end;
      break;
    case HeaderMatcherType::PRESENT:
      present_match = other.present_match;
      break;
    default:
      string_matcher = other.string_matcher;
  }
  return *this;
}

bool XdsApi::Route::Matchers::HeaderMatcher::operator==(
    const HeaderMatcher& other) const {
  if (name != other.name) return false;
  if (type != other.type) return false;
  if (invert_match != other.invert_match) return false;
  switch (type) {
    case HeaderMatcherType::REGEX:
      return regex_match->pattern() != other.regex_match->pattern();
    case HeaderMatcherType::RANGE:
      return range_start != other.range_start && range_end != other.range_end;
    case HeaderMatcherType::PRESENT:
      return present_match != other.present_match;
    default:
      return string_matcher != other.string_matcher;
  }
}

std::string XdsApi::Route::Matchers::HeaderMatcher::ToString() const {
  switch (type) {
    case HeaderMatcherType::EXACT:
      return absl::StrFormat("Header exact match:%s %s:%s",
                             invert_match ? " not" : "", name, string_matcher);
    case HeaderMatcherType::REGEX:
      return absl::StrFormat("Header regex match:%s %s:%s",
                             invert_match ? " not" : "", name,
                             regex_match->pattern());
    case HeaderMatcherType::RANGE:
      return absl::StrFormat("Header range match:%s %s:[%d, %d)",
                             invert_match ? " not" : "", name, range_start,
                             range_end);
    case HeaderMatcherType::PRESENT:
      return absl::StrFormat("Header present match:%s %s:%s",
                             invert_match ? " not" : "", name,
                             present_match ? "true" : "false");
    case HeaderMatcherType::PREFIX:
      return absl::StrFormat("Header prefix match:%s %s:%s",
                             invert_match ? " not" : "", name, string_matcher);
    case HeaderMatcherType::SUFFIX:
      return absl::StrFormat("Header suffix match:%s %s:%s",
                             invert_match ? " not" : "", name, string_matcher);
    default:
      return "";
  }
}

//
// XdsApi::Route
//

std::string XdsApi::Route::Matchers::ToString() const {
  std::vector<std::string> contents;
  contents.push_back(path_matcher.ToString());
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
  return absl::StrFormat("{cluster=%s, weight=%d}", name, weight);
}

std::string XdsApi::Route::ToString() const {
  std::vector<std::string> contents;
  contents.push_back(matchers.ToString());
  if (!cluster_name.empty()) {
    contents.push_back(absl::StrFormat("Cluster name: %s", cluster_name));
  }
  for (const ClusterWeight& cluster_weight : weighted_clusters) {
    contents.push_back(cluster_weight.ToString());
  }
  if (max_stream_duration.has_value()) {
    contents.push_back(max_stream_duration->ToString());
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
// XdsApi::StringMatcher
//

XdsApi::StringMatcher::StringMatcher(const StringMatcher& other)
    : type(other.type) {
  switch (type) {
    case StringMatcherType::SAFE_REGEX:
      regex_match = absl::make_unique<RE2>(other.regex_match->pattern());
      break;
    default:
      string_matcher = other.string_matcher;
  }
}

XdsApi::StringMatcher& XdsApi::StringMatcher::operator=(
    const StringMatcher& other) {
  type = other.type;
  switch (type) {
    case StringMatcherType::SAFE_REGEX:
      regex_match = absl::make_unique<RE2>(other.regex_match->pattern());
      break;
    default:
      string_matcher = other.string_matcher;
  }
  return *this;
}

bool XdsApi::StringMatcher::operator==(const StringMatcher& other) const {
  if (type != other.type) return false;
  switch (type) {
    case StringMatcherType::SAFE_REGEX:
      return regex_match->pattern() != other.regex_match->pattern();
    default:
      return string_matcher != other.string_matcher;
  }
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

bool IsLds(absl::string_view type_url) {
  return type_url == XdsApi::kLdsTypeUrl || type_url == kLdsV2TypeUrl;
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
      user_agent_name_(absl::StrCat("gRPC C-core ", GPR_PLATFORM_STRING)) {}

namespace {

// Works for both std::string and absl::string_view.
template <typename T>
inline upb_strview StdStringToUpbString(const T& str) {
  return upb_strview_make(str.data(), str.size());
}

void PopulateMetadataValue(upb_arena* arena, google_protobuf_Value* value_pb,
                           const Json& value);

void PopulateListValue(upb_arena* arena, google_protobuf_ListValue* list_value,
                       const Json::Array& values) {
  for (const auto& value : values) {
    auto* value_pb = google_protobuf_ListValue_add_values(list_value, arena);
    PopulateMetadataValue(arena, value_pb, value);
  }
}

void PopulateMetadata(upb_arena* arena, google_protobuf_Struct* metadata_pb,
                      const Json::Object& metadata) {
  for (const auto& p : metadata) {
    google_protobuf_Value* value = google_protobuf_Value_new(arena);
    PopulateMetadataValue(arena, value, p.second);
    google_protobuf_Struct_fields_set(
        metadata_pb, StdStringToUpbString(p.first), value, arena);
  }
}

void PopulateMetadataValue(upb_arena* arena, google_protobuf_Value* value_pb,
                           const Json& value) {
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
          google_protobuf_Value_mutable_struct_value(value_pb, arena);
      PopulateMetadata(arena, struct_value, value.object_value());
      break;
    }
    case Json::Type::ARRAY: {
      google_protobuf_ListValue* list_value =
          google_protobuf_Value_mutable_list_value(value_pb, arena);
      PopulateListValue(arena, list_value, value.array_value());
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

void PopulateBuildVersion(upb_arena* arena, envoy_config_core_v3_Node* node_msg,
                          const std::string& build_version) {
  std::string encoded_build_version = EncodeStringField(5, build_version);
  // TODO(roth): This should use upb_msg_addunknown(), but that API is
  // broken in the current version of upb, so we're using the internal
  // API for now.  Change this once we upgrade to a version of upb that
  // fixes this bug.
  _upb_msg_addunknown(node_msg, encoded_build_version.data(),
                      encoded_build_version.size(), arena);
}

void PopulateNode(upb_arena* arena, const XdsBootstrap::Node* node, bool use_v3,
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
          envoy_config_core_v3_Node_mutable_metadata(node_msg, arena);
      PopulateMetadata(arena, metadata, node->metadata.object_value());
    }
    if (!node->locality_region.empty() || !node->locality_zone.empty() ||
        !node->locality_subzone.empty()) {
      envoy_config_core_v3_Locality* locality =
          envoy_config_core_v3_Node_mutable_locality(node_msg, arena);
      if (!node->locality_region.empty()) {
        envoy_config_core_v3_Locality_set_region(
            locality, StdStringToUpbString(node->locality_region));
      }
      if (!node->locality_zone.empty()) {
        envoy_config_core_v3_Locality_set_zone(
            locality, StdStringToUpbString(node->locality_zone));
      }
      if (!node->locality_subzone.empty()) {
        envoy_config_core_v3_Locality_set_sub_zone(
            locality, StdStringToUpbString(node->locality_subzone));
      }
    }
  }
  if (!use_v3) {
    PopulateBuildVersion(arena, node_msg, build_version);
  }
  envoy_config_core_v3_Node_set_user_agent_name(
      node_msg, StdStringToUpbString(user_agent_name));
  envoy_config_core_v3_Node_set_user_agent_version(
      node_msg, upb_strview_makez(grpc_version_string()));
  envoy_config_core_v3_Node_add_client_features(
      node_msg, upb_strview_makez("envoy.lb.does_not_support_overprovisioning"),
      arena);
}

inline absl::string_view UpbStringToAbsl(const upb_strview& str) {
  return absl::string_view(str.data, str.size);
}

inline std::string UpbStringToStdString(const upb_strview& str) {
  return std::string(str.data, str.size);
}

void MaybeLogDiscoveryRequest(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_service_discovery_v3_DiscoveryRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_service_discovery_v3_DiscoveryRequest_getmsgdef(symtab);
    char buf[10240];
    upb_text_encode(request, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] constructed ADS request: %s", client,
            buf);
  }
}

grpc_slice SerializeDiscoveryRequest(
    upb_arena* arena, envoy_service_discovery_v3_DiscoveryRequest* request) {
  size_t output_length;
  char* output = envoy_service_discovery_v3_DiscoveryRequest_serialize(
      request, arena, &output_length);
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
    const std::string& version, const std::string& nonce, grpc_error* error,
    bool populate_node) {
  upb::Arena arena;
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
    grpc_slice error_description_slice;
    GPR_ASSERT(grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION,
                                  &error_description_slice));
    upb_strview error_description_strview =
        StdStringToUpbString(StringViewFromSlice(error_description_slice));
    google_rpc_Status_set_message(error_detail, error_description_strview);
    GRPC_ERROR_UNREF(error);
  }
  // Populate node.
  if (populate_node) {
    envoy_config_core_v3_Node* node_msg =
        envoy_service_discovery_v3_DiscoveryRequest_mutable_node(request,
                                                                 arena.ptr());
    PopulateNode(arena.ptr(), node_, server.ShouldUseV3(), build_version_,
                 user_agent_name_, node_msg);
  }
  // Add resource_names.
  for (const auto& resource_name : resource_names) {
    envoy_service_discovery_v3_DiscoveryRequest_add_resource_names(
        request, StdStringToUpbString(resource_name), arena.ptr());
  }
  MaybeLogDiscoveryRequest(client_, tracer_, symtab_.ptr(), request);
  return SerializeDiscoveryRequest(arena.ptr(), request);
}

namespace {

void MaybeLogDiscoveryResponse(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_service_discovery_v3_DiscoveryResponse* response) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_service_discovery_v3_DiscoveryResponse_getmsgdef(symtab);
    char buf[10240];
    upb_text_encode(response, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] received response: %s", client, buf);
  }
}

void MaybeLogRouteConfiguration(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_config_route_v3_RouteConfiguration* route_config) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_config_route_v3_RouteConfiguration_getmsgdef(symtab);
    char buf[10240];
    upb_text_encode(route_config, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] RouteConfiguration: %s", client, buf);
  }
}

void MaybeLogCluster(XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
                     const envoy_config_cluster_v3_Cluster* cluster) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_config_cluster_v3_Cluster_getmsgdef(symtab);
    char buf[10240];
    upb_text_encode(cluster, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] Cluster: %s", client, buf);
  }
}

void MaybeLogClusterLoadAssignment(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_config_endpoint_v3_ClusterLoadAssignment* cla) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_config_endpoint_v3_ClusterLoadAssignment_getmsgdef(symtab);
    char buf[10240];
    upb_text_encode(cla, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] ClusterLoadAssignment: %s", client,
            buf);
  }
}

grpc_error* RoutePathMatchParse(const envoy_config_route_v3_RouteMatch* match,
                                XdsApi::Route* route, bool* ignore_route) {
  auto* case_sensitive = envoy_config_route_v3_RouteMatch_case_sensitive(match);
  if (case_sensitive != nullptr) {
    route->matchers.path_matcher.case_sensitive =
        google_protobuf_BoolValue_value(case_sensitive);
  }
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
    route->matchers.path_matcher.type =
        XdsApi::Route::Matchers::PathMatcher::PathMatcherType::PREFIX;
    route->matchers.path_matcher.string_matcher = std::string(prefix);
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
    route->matchers.path_matcher.type =
        XdsApi::Route::Matchers::PathMatcher::PathMatcherType::PATH;
    route->matchers.path_matcher.string_matcher = std::string(path);
  } else if (envoy_config_route_v3_RouteMatch_has_safe_regex(match)) {
    const envoy_type_matcher_v3_RegexMatcher* regex_matcher =
        envoy_config_route_v3_RouteMatch_safe_regex(match);
    GPR_ASSERT(regex_matcher != nullptr);
    std::string matcher = UpbStringToStdString(
        envoy_type_matcher_v3_RegexMatcher_regex(regex_matcher));
    RE2::Options options;
    options.set_case_sensitive(route->matchers.path_matcher.case_sensitive);
    auto regex = absl::make_unique<RE2>(std::move(matcher), options);
    if (!regex->ok()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Invalid regex string specified in path matcher.");
    }
    route->matchers.path_matcher.type =
        XdsApi::Route::Matchers::PathMatcher::PathMatcherType::REGEX;
    route->matchers.path_matcher.regex_matcher = std::move(regex);
  } else {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Invalid route path specifier specified.");
  }
  return GRPC_ERROR_NONE;
}

grpc_error* RouteHeaderMatchersParse(
    const envoy_config_route_v3_RouteMatch* match, XdsApi::Route* route) {
  size_t size;
  const envoy_config_route_v3_HeaderMatcher* const* headers =
      envoy_config_route_v3_RouteMatch_headers(match, &size);
  for (size_t i = 0; i < size; ++i) {
    const envoy_config_route_v3_HeaderMatcher* header = headers[i];
    XdsApi::Route::Matchers::HeaderMatcher header_matcher;
    header_matcher.name =
        UpbStringToStdString(envoy_config_route_v3_HeaderMatcher_name(header));
    if (envoy_config_route_v3_HeaderMatcher_has_exact_match(header)) {
      header_matcher.type =
          XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcherType::EXACT;
      header_matcher.string_matcher = UpbStringToStdString(
          envoy_config_route_v3_HeaderMatcher_exact_match(header));
    } else if (envoy_config_route_v3_HeaderMatcher_has_safe_regex_match(
                   header)) {
      const envoy_type_matcher_v3_RegexMatcher* regex_matcher =
          envoy_config_route_v3_HeaderMatcher_safe_regex_match(header);
      GPR_ASSERT(regex_matcher != nullptr);
      const std::string matcher = UpbStringToStdString(
          envoy_type_matcher_v3_RegexMatcher_regex(regex_matcher));
      std::unique_ptr<RE2> regex = absl::make_unique<RE2>(matcher);
      if (!regex->ok()) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Invalid regex string specified in header matcher.");
      }
      header_matcher.type =
          XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcherType::REGEX;
      header_matcher.regex_match = std::move(regex);
    } else if (envoy_config_route_v3_HeaderMatcher_has_range_match(header)) {
      header_matcher.type =
          XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcherType::RANGE;
      const envoy_type_v3_Int64Range* range_matcher =
          envoy_config_route_v3_HeaderMatcher_range_match(header);
      header_matcher.range_start =
          envoy_type_v3_Int64Range_start(range_matcher);
      header_matcher.range_end = envoy_type_v3_Int64Range_end(range_matcher);
      if (header_matcher.range_end < header_matcher.range_start) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Invalid range header matcher specifier specified: end "
            "cannot be smaller than start.");
      }
    } else if (envoy_config_route_v3_HeaderMatcher_has_present_match(header)) {
      header_matcher.type =
          XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcherType::PRESENT;
      header_matcher.present_match =
          envoy_config_route_v3_HeaderMatcher_present_match(header);
    } else if (envoy_config_route_v3_HeaderMatcher_has_prefix_match(header)) {
      header_matcher.type =
          XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcherType::PREFIX;
      header_matcher.string_matcher = UpbStringToStdString(
          envoy_config_route_v3_HeaderMatcher_prefix_match(header));
    } else if (envoy_config_route_v3_HeaderMatcher_has_suffix_match(header)) {
      header_matcher.type =
          XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcherType::SUFFIX;
      header_matcher.string_matcher = UpbStringToStdString(
          envoy_config_route_v3_HeaderMatcher_suffix_match(header));
    } else {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Invalid route header matcher specified.");
    }
    header_matcher.invert_match =
        envoy_config_route_v3_HeaderMatcher_invert_match(header);
    route->matchers.header_matchers.emplace_back(std::move(header_matcher));
  }
  return GRPC_ERROR_NONE;
}

grpc_error* RouteRuntimeFractionParse(
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

grpc_error* RouteActionParse(const envoy_config_route_v3_Route* route_msg,
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
      sum_of_weights += cluster.weight;
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
  if (XdsTimeoutEnabled() && !*ignore_route) {
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
  return GRPC_ERROR_NONE;
}

grpc_error* RouteConfigParse(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_config_route_v3_RouteConfiguration* route_config,
    XdsApi::RdsUpdate* rds_update) {
  MaybeLogRouteConfiguration(client, tracer, symtab, route_config);
  // Get the virtual hosts.
  size_t size;
  const envoy_config_route_v3_VirtualHost* const* virtual_hosts =
      envoy_config_route_v3_RouteConfiguration_virtual_hosts(route_config,
                                                             &size);
  for (size_t i = 0; i < size; ++i) {
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
      size_t query_parameters_size;
      static_cast<void>(envoy_config_route_v3_RouteMatch_query_parameters(
          match, &query_parameters_size));
      if (query_parameters_size > 0) {
        continue;
      }
      XdsApi::Route route;
      bool ignore_route = false;
      grpc_error* error = RoutePathMatchParse(match, &route, &ignore_route);
      if (error != GRPC_ERROR_NONE) return error;
      if (ignore_route) continue;
      error = RouteHeaderMatchersParse(match, &route);
      if (error != GRPC_ERROR_NONE) return error;
      error = RouteRuntimeFractionParse(match, &route);
      if (error != GRPC_ERROR_NONE) return error;
      error = RouteActionParse(routes[j], &route, &ignore_route);
      if (error != GRPC_ERROR_NONE) return error;
      if (ignore_route) continue;
      vhost.routes.emplace_back(std::move(route));
    }
    if (vhost.routes.empty()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No valid routes specified.");
    }
  }
  return GRPC_ERROR_NONE;
}

grpc_error* LdsResponseParse(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_service_discovery_v3_DiscoveryResponse* response,
    const std::set<absl::string_view>& expected_listener_names,
    XdsApi::LdsUpdateMap* lds_update_map, upb_arena* arena) {
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_service_discovery_v3_DiscoveryResponse_resources(response, &size);
  for (size_t i = 0; i < size; ++i) {
    // Check the type_url of the resource.
    absl::string_view type_url =
        UpbStringToAbsl(google_protobuf_Any_type_url(resources[i]));
    if (!IsLds(type_url)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource is not LDS.");
    }
    // Decode the listener.
    const upb_strview encoded_listener =
        google_protobuf_Any_value(resources[i]);
    const envoy_config_listener_v3_Listener* listener =
        envoy_config_listener_v3_Listener_parse(encoded_listener.data,
                                                encoded_listener.size, arena);
    if (listener == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Can't decode listener.");
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
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("duplicate listener name \"", listener_name, "\"")
              .c_str());
    }
    XdsApi::LdsUpdate& lds_update = (*lds_update_map)[listener_name];
    // Get api_listener and decode it to http_connection_manager.
    const envoy_config_listener_v3_ApiListener* api_listener =
        envoy_config_listener_v3_Listener_api_listener(listener);
    if (api_listener == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Listener has no ApiListener.");
    }
    const upb_strview encoded_api_listener = google_protobuf_Any_value(
        envoy_config_listener_v3_ApiListener_api_listener(api_listener));
    const envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager*
        http_connection_manager =
            envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_parse(
                encoded_api_listener.data, encoded_api_listener.size, arena);
    if (http_connection_manager == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Could not parse HttpConnectionManager config from ApiListener");
    }
    if (XdsTimeoutEnabled()) {
      // Obtain max_stream_duration from Http Protocol Options.
      const envoy_config_core_v3_HttpProtocolOptions* options =
          envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_common_http_protocol_options(
              http_connection_manager);
      if (options != nullptr) {
        const google_protobuf_Duration* duration =
            envoy_config_core_v3_HttpProtocolOptions_max_stream_duration(
                options);
        if (duration != nullptr) {
          lds_update.http_max_stream_duration.seconds =
              google_protobuf_Duration_seconds(duration);
          lds_update.http_max_stream_duration.nanos =
              google_protobuf_Duration_nanos(duration);
        }
      }
    }
    // Found inlined route_config. Parse it to find the cluster_name.
    if (envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_has_route_config(
            http_connection_manager)) {
      const envoy_config_route_v3_RouteConfiguration* route_config =
          envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_route_config(
              http_connection_manager);
      XdsApi::RdsUpdate rds_update;
      grpc_error* error =
          RouteConfigParse(client, tracer, symtab, route_config, &rds_update);
      if (error != GRPC_ERROR_NONE) return error;
      lds_update.rds_update = std::move(rds_update);
      continue;
    }
    // Validate that RDS must be used to get the route_config dynamically.
    if (!envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_has_rds(
            http_connection_manager)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "HttpConnectionManager neither has inlined route_config nor RDS.");
    }
    const envoy_extensions_filters_network_http_connection_manager_v3_Rds* rds =
        envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_rds(
            http_connection_manager);
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
    lds_update.route_config_name = UpbStringToStdString(
        envoy_extensions_filters_network_http_connection_manager_v3_Rds_route_config_name(
            rds));
  }
  return GRPC_ERROR_NONE;
}

grpc_error* RdsResponseParse(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_service_discovery_v3_DiscoveryResponse* response,
    const std::set<absl::string_view>& expected_route_configuration_names,
    XdsApi::RdsUpdateMap* rds_update_map, upb_arena* arena) {
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_service_discovery_v3_DiscoveryResponse_resources(response, &size);
  for (size_t i = 0; i < size; ++i) {
    // Check the type_url of the resource.
    absl::string_view type_url =
        UpbStringToAbsl(google_protobuf_Any_type_url(resources[i]));
    if (!IsRds(type_url)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource is not RDS.");
    }
    // Decode the route_config.
    const upb_strview encoded_route_config =
        google_protobuf_Any_value(resources[i]);
    const envoy_config_route_v3_RouteConfiguration* route_config =
        envoy_config_route_v3_RouteConfiguration_parse(
            encoded_route_config.data, encoded_route_config.size, arena);
    if (route_config == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Can't decode route_config.");
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
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("duplicate route config name \"", route_config_name,
                       "\"")
              .c_str());
    }
    // Parse the route_config.
    XdsApi::RdsUpdate& rds_update =
        (*rds_update_map)[std::move(route_config_name)];
    grpc_error* error =
        RouteConfigParse(client, tracer, symtab, route_config, &rds_update);
    if (error != GRPC_ERROR_NONE) return error;
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

grpc_error* CommonTlsContextParse(
    const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext*
        common_tls_context_proto,
    XdsApi::CommonTlsContext* common_tls_context) GRPC_MUST_USE_RESULT;
grpc_error* CommonTlsContextParse(
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
        XdsApi::StringMatcher matcher;
        if (envoy_type_matcher_v3_StringMatcher_has_exact(
                subject_alt_names_matchers[i])) {
          matcher.type = XdsApi::StringMatcher::StringMatcherType::EXACT;
          matcher.string_matcher =
              UpbStringToStdString(envoy_type_matcher_v3_StringMatcher_exact(
                  subject_alt_names_matchers[i]));
        } else if (envoy_type_matcher_v3_StringMatcher_has_prefix(
                       subject_alt_names_matchers[i])) {
          matcher.type = XdsApi::StringMatcher::StringMatcherType::PREFIX;
          matcher.string_matcher =
              UpbStringToStdString(envoy_type_matcher_v3_StringMatcher_prefix(
                  subject_alt_names_matchers[i]));
        } else if (envoy_type_matcher_v3_StringMatcher_has_suffix(
                       subject_alt_names_matchers[i])) {
          matcher.type = XdsApi::StringMatcher::StringMatcherType::SUFFIX;
          matcher.string_matcher =
              UpbStringToStdString(envoy_type_matcher_v3_StringMatcher_suffix(
                  subject_alt_names_matchers[i]));
        } else if (envoy_type_matcher_v3_StringMatcher_has_safe_regex(
                       subject_alt_names_matchers[i])) {
          matcher.type = XdsApi::StringMatcher::StringMatcherType::SAFE_REGEX;
          auto* regex_matcher = envoy_type_matcher_v3_StringMatcher_safe_regex(
              subject_alt_names_matchers[i]);
          std::unique_ptr<RE2> regex =
              absl::make_unique<RE2>(UpbStringToStdString(
                  envoy_type_matcher_v3_RegexMatcher_regex(regex_matcher)));
          if (!regex->ok()) {
            return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Invalid regex string specified in string matcher.");
          }
          matcher.regex_match = std::move(regex);
        } else {
          return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "Invalid StringMatcher specified");
        }
        matcher.ignore_case = envoy_type_matcher_v3_StringMatcher_ignore_case(
            subject_alt_names_matchers[i]);
        common_tls_context->combined_validation_context
            .default_validation_context.match_subject_alt_names.emplace_back(
                matcher);
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

grpc_error* CdsResponseParse(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_service_discovery_v3_DiscoveryResponse* response,
    const std::set<absl::string_view>& expected_cluster_names,
    XdsApi::CdsUpdateMap* cds_update_map, upb_arena* arena) {
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
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource is not CDS.");
    }
    // Decode the cluster.
    const upb_strview encoded_cluster = google_protobuf_Any_value(resources[i]);
    const envoy_config_cluster_v3_Cluster* cluster =
        envoy_config_cluster_v3_Cluster_parse(encoded_cluster.data,
                                              encoded_cluster.size, arena);
    if (cluster == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Can't decode cluster.");
    }
    MaybeLogCluster(client, tracer, symtab, cluster);
    // Ignore unexpected cluster names.
    std::string cluster_name =
        UpbStringToStdString(envoy_config_cluster_v3_Cluster_name(cluster));
    if (expected_cluster_names.find(cluster_name) ==
        expected_cluster_names.end()) {
      continue;
    }
    // Fail on duplicate resources.
    if (cds_update_map->find(cluster_name) != cds_update_map->end()) {
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("duplicate resource name \"", cluster_name, "\"")
              .c_str());
    }
    XdsApi::CdsUpdate& cds_update = (*cds_update_map)[std::move(cluster_name)];
    // Check the cluster_discovery_type.
    if (!envoy_config_cluster_v3_Cluster_has_type(cluster)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("DiscoveryType not found.");
    }
    if (envoy_config_cluster_v3_Cluster_type(cluster) !=
        envoy_config_cluster_v3_Cluster_EDS) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("DiscoveryType is not EDS.");
    }
    // Check the EDS config source.
    const envoy_config_cluster_v3_Cluster_EdsClusterConfig* eds_cluster_config =
        envoy_config_cluster_v3_Cluster_eds_cluster_config(cluster);
    const envoy_config_core_v3_ConfigSource* eds_config =
        envoy_config_cluster_v3_Cluster_EdsClusterConfig_eds_config(
            eds_cluster_config);
    if (!envoy_config_core_v3_ConfigSource_has_ads(eds_config)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "EDS ConfigSource is not ADS.");
    }
    // Record EDS service_name (if any).
    upb_strview service_name =
        envoy_config_cluster_v3_Cluster_EdsClusterConfig_service_name(
            eds_cluster_config);
    if (service_name.size != 0) {
      cds_update.eds_service_name = UpbStringToStdString(service_name);
    }
    // Check the LB policy.
    if (envoy_config_cluster_v3_Cluster_lb_policy(cluster) !=
        envoy_config_cluster_v3_Cluster_ROUND_ROBIN) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "LB policy is not ROUND_ROBIN.");
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
                    encoded_upstream_tls_context.size, arena);
            if (upstream_tls_context == nullptr) {
              return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                  "Can't decode upstream tls context.");
            }
            auto* common_tls_context =
                envoy_extensions_transport_sockets_tls_v3_UpstreamTlsContext_common_tls_context(
                    upstream_tls_context);
            if (common_tls_context != nullptr) {
              grpc_error* error = CommonTlsContextParse(
                  common_tls_context, &cds_update.common_tls_context);
              if (error != GRPC_ERROR_NONE) return error;
            }
          }
        }
      }
    }
    // Record LRS server name (if any).
    const envoy_config_core_v3_ConfigSource* lrs_server =
        envoy_config_cluster_v3_Cluster_lrs_server(cluster);
    if (lrs_server != nullptr) {
      if (!envoy_config_core_v3_ConfigSource_has_self(lrs_server)) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "LRS ConfigSource is not self.");
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
  return GRPC_ERROR_NONE;
}

grpc_error* ServerAddressParseAndAppend(
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
  grpc_string_to_sockaddr(&addr, address_str.c_str(), port);
  // Append the address to the list.
  list->emplace_back(addr, nullptr);
  return GRPC_ERROR_NONE;
}

grpc_error* LocalityParse(
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
    grpc_error* error = ServerAddressParseAndAppend(
        lb_endpoints[i], &output_locality->endpoints);
    if (error != GRPC_ERROR_NONE) return error;
  }
  // Parse the priority.
  *priority = envoy_config_endpoint_v3_LocalityLbEndpoints_priority(
      locality_lb_endpoints);
  return GRPC_ERROR_NONE;
}

grpc_error* DropParseAndAppend(
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

grpc_error* EdsResponseParse(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_service_discovery_v3_DiscoveryResponse* response,
    const std::set<absl::string_view>& expected_eds_service_names,
    XdsApi::EdsUpdateMap* eds_update_map, upb_arena* arena) {
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_service_discovery_v3_DiscoveryResponse_resources(response, &size);
  for (size_t i = 0; i < size; ++i) {
    // Check the type_url of the resource.
    absl::string_view type_url =
        UpbStringToAbsl(google_protobuf_Any_type_url(resources[i]));
    if (!IsEds(type_url)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource is not EDS.");
    }
    // Get the cluster_load_assignment.
    upb_strview encoded_cluster_load_assignment =
        google_protobuf_Any_value(resources[i]);
    envoy_config_endpoint_v3_ClusterLoadAssignment* cluster_load_assignment =
        envoy_config_endpoint_v3_ClusterLoadAssignment_parse(
            encoded_cluster_load_assignment.data,
            encoded_cluster_load_assignment.size, arena);
    if (cluster_load_assignment == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Can't parse cluster_load_assignment.");
    }
    MaybeLogClusterLoadAssignment(client, tracer, symtab,
                                  cluster_load_assignment);
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
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("duplicate resource name \"", eds_service_name, "\"")
              .c_str());
    }
    XdsApi::EdsUpdate& eds_update =
        (*eds_update_map)[std::move(eds_service_name)];
    // Get the endpoints.
    size_t locality_size;
    const envoy_config_endpoint_v3_LocalityLbEndpoints* const* endpoints =
        envoy_config_endpoint_v3_ClusterLoadAssignment_endpoints(
            cluster_load_assignment, &locality_size);
    for (size_t j = 0; j < locality_size; ++j) {
      size_t priority;
      XdsApi::EdsUpdate::Priority::Locality locality;
      grpc_error* error = LocalityParse(endpoints[j], &locality, &priority);
      if (error != GRPC_ERROR_NONE) return error;
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
    for (const auto& priority : eds_update.priorities) {
      if (priority.localities.empty()) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "EDS update includes sparse priority list");
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
        grpc_error* error =
            DropParseAndAppend(drop_overload[j], eds_update.drop_config.get());
        if (error != GRPC_ERROR_NONE) return error;
      }
    }
  }
  return GRPC_ERROR_NONE;
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

}  // namespace

XdsApi::AdsParseResult XdsApi::ParseAdsResponse(
    const grpc_slice& encoded_response,
    const std::set<absl::string_view>& expected_listener_names,
    const std::set<absl::string_view>& expected_route_configuration_names,
    const std::set<absl::string_view>& expected_cluster_names,
    const std::set<absl::string_view>& expected_eds_service_names) {
  AdsParseResult result;
  upb::Arena arena;
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
  MaybeLogDiscoveryResponse(client_, tracer_, symtab_.ptr(), response);
  // Record the type_url, the version_info, and the nonce of the response.
  result.type_url = TypeUrlInternalToExternal(UpbStringToAbsl(
      envoy_service_discovery_v3_DiscoveryResponse_type_url(response)));
  result.version = UpbStringToStdString(
      envoy_service_discovery_v3_DiscoveryResponse_version_info(response));
  result.nonce = UpbStringToStdString(
      envoy_service_discovery_v3_DiscoveryResponse_nonce(response));
  // Parse the response according to the resource type.
  if (IsLds(result.type_url)) {
    result.parse_error = LdsResponseParse(client_, tracer_, symtab_.ptr(),
                                          response, expected_listener_names,
                                          &result.lds_update_map, arena.ptr());
  } else if (IsRds(result.type_url)) {
    result.parse_error =
        RdsResponseParse(client_, tracer_, symtab_.ptr(), response,
                         expected_route_configuration_names,
                         &result.rds_update_map, arena.ptr());
  } else if (IsCds(result.type_url)) {
    result.parse_error = CdsResponseParse(client_, tracer_, symtab_.ptr(),
                                          response, expected_cluster_names,
                                          &result.cds_update_map, arena.ptr());
  } else if (IsEds(result.type_url)) {
    result.parse_error = EdsResponseParse(client_, tracer_, symtab_.ptr(),
                                          response, expected_eds_service_names,
                                          &result.eds_update_map, arena.ptr());
  }
  return result;
}

namespace {

void MaybeLogLrsRequest(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_service_load_stats_v3_LoadStatsRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_service_load_stats_v3_LoadStatsRequest_getmsgdef(symtab);
    char buf[10240];
    upb_text_encode(request, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] constructed LRS request: %s", client,
            buf);
  }
}

grpc_slice SerializeLrsRequest(
    const envoy_service_load_stats_v3_LoadStatsRequest* request,
    upb_arena* arena) {
  size_t output_length;
  char* output = envoy_service_load_stats_v3_LoadStatsRequest_serialize(
      request, arena, &output_length);
  return grpc_slice_from_copied_buffer(output, output_length);
}

}  // namespace

grpc_slice XdsApi::CreateLrsInitialRequest(
    const XdsBootstrap::XdsServer& server) {
  upb::Arena arena;
  // Create a request.
  envoy_service_load_stats_v3_LoadStatsRequest* request =
      envoy_service_load_stats_v3_LoadStatsRequest_new(arena.ptr());
  // Populate node.
  envoy_config_core_v3_Node* node_msg =
      envoy_service_load_stats_v3_LoadStatsRequest_mutable_node(request,
                                                                arena.ptr());
  PopulateNode(arena.ptr(), node_, server.ShouldUseV3(), build_version_,
               user_agent_name_, node_msg);
  envoy_config_core_v3_Node_add_client_features(
      node_msg, upb_strview_makez("envoy.lrs.supports_send_all_clusters"),
      arena.ptr());
  MaybeLogLrsRequest(client_, tracer_, symtab_.ptr(), request);
  return SerializeLrsRequest(request, arena.ptr());
}

namespace {

void LocalityStatsPopulate(
    envoy_config_endpoint_v3_UpstreamLocalityStats* output,
    const XdsLocalityName& locality_name,
    const XdsClusterLocalityStats::Snapshot& snapshot, upb_arena* arena) {
  // Set locality.
  envoy_config_core_v3_Locality* locality =
      envoy_config_endpoint_v3_UpstreamLocalityStats_mutable_locality(output,
                                                                      arena);
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
            output, arena);
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
      LocalityStatsPopulate(locality_stats, locality_name, snapshot,
                            arena.ptr());
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
  MaybeLogLrsRequest(client_, tracer_, symtab_.ptr(), request);
  return SerializeLrsRequest(request, arena.ptr());
}

grpc_error* XdsApi::ParseLrsResponse(const grpc_slice& encoded_response,
                                     bool* send_all_clusters,
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

}  // namespace grpc_core
