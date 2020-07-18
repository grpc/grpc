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
#include <cstdlib>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "upb/upb.hpp"

#include <grpc/impl/codegen/log.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/xds/xds_api.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

#include "envoy/api/v2/cds.upb.h"
#include "envoy/api/v2/cds.upbdefs.h"
#include "envoy/api/v2/cluster.upbdefs.h"
#include "envoy/api/v2/core/address.upb.h"
#include "envoy/api/v2/core/base.upb.h"
#include "envoy/api/v2/core/config_source.upb.h"
#include "envoy/api/v2/core/health_check.upb.h"
#include "envoy/api/v2/discovery.upb.h"
#include "envoy/api/v2/discovery.upbdefs.h"
#include "envoy/api/v2/eds.upb.h"
#include "envoy/api/v2/eds.upbdefs.h"
#include "envoy/api/v2/endpoint.upbdefs.h"
#include "envoy/api/v2/endpoint/endpoint.upb.h"
#include "envoy/api/v2/endpoint/load_report.upb.h"
#include "envoy/api/v2/lds.upb.h"
#include "envoy/api/v2/rds.upb.h"
#include "envoy/api/v2/rds.upbdefs.h"
#include "envoy/api/v2/route.upbdefs.h"
#include "envoy/api/v2/route/route.upb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.upb.h"
#include "envoy/config/listener/v2/api_listener.upb.h"
#include "envoy/service/load_stats/v2/lrs.upb.h"
#include "envoy/service/load_stats/v2/lrs.upbdefs.h"
#include "envoy/type/matcher/regex.upb.h"
#include "envoy/type/percent.upb.h"
#include "envoy/type/range.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "google/rpc/status.upb.h"
#include "upb/text_encode.h"
#include "upb/upb.h"

namespace grpc_core {

//
// XdsApi::PriorityListUpdate
//

bool XdsApi::PriorityListUpdate::operator==(
    const XdsApi::PriorityListUpdate& other) const {
  if (priorities_.size() != other.priorities_.size()) return false;
  for (size_t i = 0; i < priorities_.size(); ++i) {
    if (priorities_[i].localities != other.priorities_[i].localities) {
      return false;
    }
  }
  return true;
}

void XdsApi::PriorityListUpdate::Add(
    XdsApi::PriorityListUpdate::LocalityMap::Locality locality) {
  // Pad the missing priorities in case the localities are not ordered by
  // priority.
  if (!Contains(locality.priority)) priorities_.resize(locality.priority + 1);
  LocalityMap& locality_map = priorities_[locality.priority];
  locality_map.localities.emplace(locality.name, std::move(locality));
}

const XdsApi::PriorityListUpdate::LocalityMap* XdsApi::PriorityListUpdate::Find(
    uint32_t priority) const {
  if (!Contains(priority)) return nullptr;
  return &priorities_[priority];
}

bool XdsApi::PriorityListUpdate::Contains(
    const RefCountedPtr<XdsLocalityName>& name) {
  for (size_t i = 0; i < priorities_.size(); ++i) {
    const LocalityMap& locality_map = priorities_[i];
    if (locality_map.Contains(name)) return true;
  }
  return false;
}

//
// XdsApi::DropConfig
//

bool XdsApi::DropConfig::ShouldDrop(const std::string** category_name) const {
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

//
// XdsApi
//

const char* XdsApi::kLdsTypeUrl = "type.googleapis.com/envoy.api.v2.Listener";
const char* XdsApi::kRdsTypeUrl =
    "type.googleapis.com/envoy.api.v2.RouteConfiguration";
const char* XdsApi::kCdsTypeUrl = "type.googleapis.com/envoy.api.v2.Cluster";
const char* XdsApi::kEdsTypeUrl =
    "type.googleapis.com/envoy.api.v2.ClusterLoadAssignment";

namespace {

bool XdsRoutingEnabled() {
  char* value = gpr_getenv("GRPC_XDS_EXPERIMENTAL_ROUTING");
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value, &parsed_value);
  gpr_free(value);
  return parse_succeeded && parsed_value;
}

}  // namespace

std::string XdsApi::RdsUpdate::RdsRoute::Matchers::PathMatcher::ToString()
    const {
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
  return absl::StrFormat("Path %s:/%s/", path_type_string,
                         type == PathMatcherType::REGEX
                             ? regex_matcher->pattern()
                             : string_matcher);
}

std::string XdsApi::RdsUpdate::RdsRoute::Matchers::HeaderMatcher::ToString()
    const {
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

std::string XdsApi::RdsUpdate::RdsRoute::Matchers::ToString() const {
  std::vector<std::string> contents;
  contents.push_back(path_matcher.ToString());
  for (const auto& header_it : header_matchers) {
    contents.push_back(header_it.ToString());
  }
  if (fraction_per_million.has_value()) {
    contents.push_back(absl::StrFormat("Fraction Per Million %d",
                                       fraction_per_million.value()));
  }
  return absl::StrJoin(contents, "\n");
}

std::string XdsApi::RdsUpdate::RdsRoute::ClusterWeight::ToString() const {
  return absl::StrFormat("{cluster=%s, weight=%d}", name, weight);
}

std::string XdsApi::RdsUpdate::RdsRoute::ToString() const {
  std::vector<std::string> contents;
  contents.push_back(matchers.ToString());
  if (!cluster_name.empty()) {
    contents.push_back(absl::StrFormat("Cluster name: %s", cluster_name));
  }
  for (const auto& weighted_it : weighted_clusters) {
    contents.push_back(weighted_it.ToString());
  }
  return absl::StrJoin(contents, "\n");
}

std::string XdsApi::RdsUpdate::ToString() const {
  std::vector<std::string> contents;
  for (const auto& route_it : routes) {
    contents.push_back(route_it.ToString());
  }
  return absl::StrJoin(contents, ",\n");
}

XdsApi::XdsApi(XdsClient* client, TraceFlag* tracer,
               const XdsBootstrap::Node* node)
    : client_(client),
      tracer_(tracer),
      xds_routing_enabled_(XdsRoutingEnabled()),
      node_(node),
      build_version_(absl::StrCat("gRPC C-core ", GPR_PLATFORM_STRING, " ",
                                  grpc_version_string())),
      user_agent_name_(absl::StrCat("gRPC C-core ", GPR_PLATFORM_STRING)) {}

namespace {

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
        metadata_pb, upb_strview_makez(p.first.c_str()), value, arena);
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
          value_pb, upb_strview_makez(value.string_value().c_str()));
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

void PopulateNode(upb_arena* arena, const XdsBootstrap::Node* node,
                  const std::string& build_version,
                  const std::string& user_agent_name,
                  const std::string& server_name,
                  envoy_api_v2_core_Node* node_msg) {
  if (node != nullptr) {
    if (!node->id.empty()) {
      envoy_api_v2_core_Node_set_id(node_msg,
                                    upb_strview_makez(node->id.c_str()));
    }
    if (!node->cluster.empty()) {
      envoy_api_v2_core_Node_set_cluster(
          node_msg, upb_strview_makez(node->cluster.c_str()));
    }
    if (!node->metadata.object_value().empty()) {
      google_protobuf_Struct* metadata =
          envoy_api_v2_core_Node_mutable_metadata(node_msg, arena);
      PopulateMetadata(arena, metadata, node->metadata.object_value());
    }
    if (!server_name.empty()) {
      google_protobuf_Struct* metadata =
          envoy_api_v2_core_Node_mutable_metadata(node_msg, arena);
      google_protobuf_Value* value = google_protobuf_Value_new(arena);
      google_protobuf_Value_set_string_value(
          value, upb_strview_make(server_name.data(), server_name.size()));
      google_protobuf_Struct_fields_set(
          metadata, upb_strview_makez("PROXYLESS_CLIENT_HOSTNAME"), value,
          arena);
    }
    if (!node->locality_region.empty() || !node->locality_zone.empty() ||
        !node->locality_subzone.empty()) {
      envoy_api_v2_core_Locality* locality =
          envoy_api_v2_core_Node_mutable_locality(node_msg, arena);
      if (!node->locality_region.empty()) {
        envoy_api_v2_core_Locality_set_region(
            locality, upb_strview_makez(node->locality_region.c_str()));
      }
      if (!node->locality_zone.empty()) {
        envoy_api_v2_core_Locality_set_zone(
            locality, upb_strview_makez(node->locality_zone.c_str()));
      }
      if (!node->locality_subzone.empty()) {
        envoy_api_v2_core_Locality_set_sub_zone(
            locality, upb_strview_makez(node->locality_subzone.c_str()));
      }
    }
  }
  envoy_api_v2_core_Node_set_build_version(
      node_msg, upb_strview_make(build_version.data(), build_version.size()));
  envoy_api_v2_core_Node_set_user_agent_name(
      node_msg,
      upb_strview_make(user_agent_name.data(), user_agent_name.size()));
  envoy_api_v2_core_Node_set_user_agent_version(
      node_msg, upb_strview_makez(grpc_version_string()));
  envoy_api_v2_core_Node_add_client_features(
      node_msg, upb_strview_makez("envoy.lb.does_not_support_overprovisioning"),
      arena);
}

inline std::string UpbStringToStdString(const upb_strview& str) {
  return std::string(str.data, str.size);
}

void MaybeLogDiscoveryRequest(XdsClient* client, TraceFlag* tracer,
                              upb_symtab* symtab,
                              const envoy_api_v2_DiscoveryRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_api_v2_DiscoveryRequest_getmsgdef(symtab);
    char buf[10240];
    upb_text_encode(request, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] constructed ADS request: %s", client,
            buf);
  }
}

grpc_slice SerializeDiscoveryRequest(upb_arena* arena,
                                     envoy_api_v2_DiscoveryRequest* request) {
  size_t output_length;
  char* output =
      envoy_api_v2_DiscoveryRequest_serialize(request, arena, &output_length);
  return grpc_slice_from_copied_buffer(output, output_length);
}

}  // namespace

grpc_slice XdsApi::CreateAdsRequest(
    const std::string& type_url,
    const std::set<absl::string_view>& resource_names,
    const std::string& version, const std::string& nonce, grpc_error* error,
    bool populate_node) {
  upb::Arena arena;
  // Create a request.
  envoy_api_v2_DiscoveryRequest* request =
      envoy_api_v2_DiscoveryRequest_new(arena.ptr());
  // Set type_url.
  envoy_api_v2_DiscoveryRequest_set_type_url(
      request, upb_strview_make(type_url.data(), type_url.size()));
  // Set version_info.
  if (!version.empty()) {
    envoy_api_v2_DiscoveryRequest_set_version_info(
        request, upb_strview_make(version.data(), version.size()));
  }
  // Set nonce.
  if (!nonce.empty()) {
    envoy_api_v2_DiscoveryRequest_set_response_nonce(
        request, upb_strview_make(nonce.data(), nonce.size()));
  }
  // Set error_detail if it's a NACK.
  if (error != GRPC_ERROR_NONE) {
    grpc_slice error_description_slice;
    GPR_ASSERT(grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION,
                                  &error_description_slice));
    upb_strview error_description_strview =
        upb_strview_make(reinterpret_cast<const char*>(
                             GPR_SLICE_START_PTR(error_description_slice)),
                         GPR_SLICE_LENGTH(error_description_slice));
    google_rpc_Status* error_detail =
        envoy_api_v2_DiscoveryRequest_mutable_error_detail(request,
                                                           arena.ptr());
    google_rpc_Status_set_message(error_detail, error_description_strview);
    GRPC_ERROR_UNREF(error);
  }
  // Populate node.
  if (populate_node) {
    envoy_api_v2_core_Node* node_msg =
        envoy_api_v2_DiscoveryRequest_mutable_node(request, arena.ptr());
    PopulateNode(arena.ptr(), node_, build_version_, user_agent_name_, "",
                 node_msg);
  }
  // Add resource_names.
  for (const auto& resource_name : resource_names) {
    envoy_api_v2_DiscoveryRequest_add_resource_names(
        request, upb_strview_make(resource_name.data(), resource_name.size()),
        arena.ptr());
  }
  MaybeLogDiscoveryRequest(client_, tracer_, symtab_.ptr(), request);
  return SerializeDiscoveryRequest(arena.ptr(), request);
}

namespace {

void MaybeLogDiscoveryResponse(XdsClient* client, TraceFlag* tracer,
                               upb_symtab* symtab,
                               const envoy_api_v2_DiscoveryResponse* response) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_api_v2_DiscoveryResponse_getmsgdef(symtab);
    char buf[10240];
    upb_text_encode(response, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] received response: %s", client, buf);
  }
}

void MaybeLogRouteConfiguration(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_api_v2_RouteConfiguration* route_config) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_api_v2_RouteConfiguration_getmsgdef(symtab);
    char buf[10240];
    upb_text_encode(route_config, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] RouteConfiguration: %s", client, buf);
  }
}

void MaybeLogCluster(XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
                     const envoy_api_v2_Cluster* cluster) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type = envoy_api_v2_Cluster_getmsgdef(symtab);
    char buf[10240];
    upb_text_encode(cluster, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] Cluster: %s", client, buf);
  }
}

void MaybeLogClusterLoadAssignment(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_api_v2_ClusterLoadAssignment* cla) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_api_v2_ClusterLoadAssignment_getmsgdef(symtab);
    char buf[10240];
    upb_text_encode(cla, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] ClusterLoadAssignment: %s", client,
            buf);
  }
}

// Better match type has smaller value.
enum MatchType {
  EXACT_MATCH,
  SUFFIX_MATCH,
  PREFIX_MATCH,
  UNIVERSE_MATCH,
  INVALID_MATCH,
};

// Returns true if match succeeds.
bool DomainMatch(MatchType match_type, std::string domain_pattern,
                 std::string expected_host_name) {
  // Normalize the args to lower-case. Domain matching is case-insensitive.
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

grpc_error* RoutePathMatchParse(const envoy_api_v2_route_RouteMatch* match,
                                XdsApi::RdsUpdate::RdsRoute* rds_route,
                                bool* ignore_route) {
  if (envoy_api_v2_route_RouteMatch_has_prefix(match)) {
    upb_strview prefix = envoy_api_v2_route_RouteMatch_prefix(match);
    // Empty prefix "" is accepted.
    if (prefix.size > 0) {
      // Prefix "/" is accepted.
      if (prefix.data[0] != '/') {
        // Prefix which does not start with a / will never match anything, so
        // ignore this route.
        *ignore_route = true;
        return GRPC_ERROR_NONE;
      }
      std::vector<absl::string_view> prefix_elements =
          absl::StrSplit(absl::string_view(prefix.data, prefix.size).substr(1),
                         absl::MaxSplits('/', 2));
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
    rds_route->matchers.path_matcher.type = XdsApi::RdsUpdate::RdsRoute::
        Matchers::PathMatcher::PathMatcherType::PREFIX;
    rds_route->matchers.path_matcher.string_matcher =
        UpbStringToStdString(prefix);
  } else if (envoy_api_v2_route_RouteMatch_has_path(match)) {
    upb_strview path = envoy_api_v2_route_RouteMatch_path(match);
    if (path.size == 0) {
      // Path that is empty will never match anything, so ignore this route.
      *ignore_route = true;
      return GRPC_ERROR_NONE;
    }
    if (path.data[0] != '/') {
      // Path which does not start with a / will never match anything, so
      // ignore this route.
      *ignore_route = true;
      return GRPC_ERROR_NONE;
    }
    std::vector<absl::string_view> path_elements =
        absl::StrSplit(absl::string_view(path.data, path.size).substr(1),
                       absl::MaxSplits('/', 2));
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
    rds_route->matchers.path_matcher.type = XdsApi::RdsUpdate::RdsRoute::
        Matchers::PathMatcher::PathMatcherType::PATH;
    rds_route->matchers.path_matcher.string_matcher =
        UpbStringToStdString(path);
  } else if (envoy_api_v2_route_RouteMatch_has_safe_regex(match)) {
    const envoy_type_matcher_RegexMatcher* regex_matcher =
        envoy_api_v2_route_RouteMatch_safe_regex(match);
    GPR_ASSERT(regex_matcher != nullptr);
    const std::string matcher = UpbStringToStdString(
        envoy_type_matcher_RegexMatcher_regex(regex_matcher));
    std::unique_ptr<RE2> regex = absl::make_unique<RE2>(matcher);
    if (!regex->ok()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Invalid regex string specified in path matcher.");
    }
    rds_route->matchers.path_matcher.type = XdsApi::RdsUpdate::RdsRoute::
        Matchers::PathMatcher::PathMatcherType::REGEX;
    rds_route->matchers.path_matcher.regex_matcher = std::move(regex);
  } else {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Invalid route path specifier specified.");
  }
  return GRPC_ERROR_NONE;
}

grpc_error* RouteHeaderMatchersParse(const envoy_api_v2_route_RouteMatch* match,
                                     XdsApi::RdsUpdate::RdsRoute* rds_route) {
  size_t size;
  const envoy_api_v2_route_HeaderMatcher* const* headers =
      envoy_api_v2_route_RouteMatch_headers(match, &size);
  for (size_t i = 0; i < size; ++i) {
    const envoy_api_v2_route_HeaderMatcher* header = headers[i];
    XdsApi::RdsUpdate::RdsRoute::Matchers::HeaderMatcher header_matcher;
    header_matcher.name =
        UpbStringToStdString(envoy_api_v2_route_HeaderMatcher_name(header));
    if (envoy_api_v2_route_HeaderMatcher_has_exact_match(header)) {
      header_matcher.type = XdsApi::RdsUpdate::RdsRoute::Matchers::
          HeaderMatcher::HeaderMatcherType::EXACT;
      header_matcher.string_matcher = UpbStringToStdString(
          envoy_api_v2_route_HeaderMatcher_exact_match(header));
    } else if (envoy_api_v2_route_HeaderMatcher_has_safe_regex_match(header)) {
      const envoy_type_matcher_RegexMatcher* regex_matcher =
          envoy_api_v2_route_HeaderMatcher_safe_regex_match(header);
      GPR_ASSERT(regex_matcher != nullptr);
      const std::string matcher = UpbStringToStdString(
          envoy_type_matcher_RegexMatcher_regex(regex_matcher));
      std::unique_ptr<RE2> regex = absl::make_unique<RE2>(matcher);
      if (!regex->ok()) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Invalid regex string specified in header matcher.");
      }
      header_matcher.type = XdsApi::RdsUpdate::RdsRoute::Matchers::
          HeaderMatcher::HeaderMatcherType::REGEX;
      header_matcher.regex_match = std::move(regex);
    } else if (envoy_api_v2_route_HeaderMatcher_has_range_match(header)) {
      header_matcher.type = XdsApi::RdsUpdate::RdsRoute::Matchers::
          HeaderMatcher::HeaderMatcherType::RANGE;
      const envoy_type_Int64Range* range_matcher =
          envoy_api_v2_route_HeaderMatcher_range_match(header);
      header_matcher.range_start = envoy_type_Int64Range_start(range_matcher);
      header_matcher.range_end = envoy_type_Int64Range_end(range_matcher);
      if (header_matcher.range_end < header_matcher.range_start) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Invalid range header matcher specifier specified: end "
            "cannot be smaller than start.");
      }
    } else if (envoy_api_v2_route_HeaderMatcher_has_present_match(header)) {
      header_matcher.type = XdsApi::RdsUpdate::RdsRoute::Matchers::
          HeaderMatcher::HeaderMatcherType::PRESENT;
      header_matcher.present_match =
          envoy_api_v2_route_HeaderMatcher_present_match(header);
    } else if (envoy_api_v2_route_HeaderMatcher_has_prefix_match(header)) {
      header_matcher.type = XdsApi::RdsUpdate::RdsRoute::Matchers::
          HeaderMatcher::HeaderMatcherType::PREFIX;
      header_matcher.string_matcher = UpbStringToStdString(
          envoy_api_v2_route_HeaderMatcher_prefix_match(header));
    } else if (envoy_api_v2_route_HeaderMatcher_has_suffix_match(header)) {
      header_matcher.type = XdsApi::RdsUpdate::RdsRoute::Matchers::
          HeaderMatcher::HeaderMatcherType::SUFFIX;
      header_matcher.string_matcher = UpbStringToStdString(
          envoy_api_v2_route_HeaderMatcher_suffix_match(header));
    } else {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Invalid route header matcher specified.");
    }
    header_matcher.invert_match =
        envoy_api_v2_route_HeaderMatcher_invert_match(header);
    rds_route->matchers.header_matchers.emplace_back(std::move(header_matcher));
  }
  return GRPC_ERROR_NONE;
}

grpc_error* RouteRuntimeFractionParse(
    const envoy_api_v2_route_RouteMatch* match,
    XdsApi::RdsUpdate::RdsRoute* rds_route) {
  const envoy_api_v2_core_RuntimeFractionalPercent* runtime_fraction =
      envoy_api_v2_route_RouteMatch_runtime_fraction(match);
  if (runtime_fraction != nullptr) {
    const envoy_type_FractionalPercent* fraction =
        envoy_api_v2_core_RuntimeFractionalPercent_default_value(
            runtime_fraction);
    if (fraction != nullptr) {
      uint32_t numerator = envoy_type_FractionalPercent_numerator(fraction);
      const auto denominator =
          static_cast<envoy_type_FractionalPercent_DenominatorType>(
              envoy_type_FractionalPercent_denominator(fraction));
      // Normalize to million.
      switch (denominator) {
        case envoy_type_FractionalPercent_HUNDRED:
          numerator *= 10000;
          break;
        case envoy_type_FractionalPercent_TEN_THOUSAND:
          numerator *= 100;
          break;
        case envoy_type_FractionalPercent_MILLION:
          break;
        default:
          return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "Unknown denominator type");
      }
      rds_route->matchers.fraction_per_million = numerator;
    }
  }
  return GRPC_ERROR_NONE;
}

grpc_error* RouteActionParse(const envoy_api_v2_route_Route* route,
                             XdsApi::RdsUpdate::RdsRoute* rds_route,
                             bool* ignore_route) {
  if (!envoy_api_v2_route_Route_has_route(route)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "No RouteAction found in route.");
  }
  const envoy_api_v2_route_RouteAction* route_action =
      envoy_api_v2_route_Route_route(route);
  // Get the cluster or weighted_clusters in the RouteAction.
  if (envoy_api_v2_route_RouteAction_has_cluster(route_action)) {
    const upb_strview cluster_name =
        envoy_api_v2_route_RouteAction_cluster(route_action);
    if (cluster_name.size == 0) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "RouteAction cluster contains empty cluster name.");
    }
    rds_route->cluster_name = UpbStringToStdString(cluster_name);
  } else if (envoy_api_v2_route_RouteAction_has_weighted_clusters(
                 route_action)) {
    const envoy_api_v2_route_WeightedCluster* weighted_cluster =
        envoy_api_v2_route_RouteAction_weighted_clusters(route_action);
    uint32_t total_weight = 100;
    const google_protobuf_UInt32Value* weight =
        envoy_api_v2_route_WeightedCluster_total_weight(weighted_cluster);
    if (weight != nullptr) {
      total_weight = google_protobuf_UInt32Value_value(weight);
    }
    size_t clusters_size;
    const envoy_api_v2_route_WeightedCluster_ClusterWeight* const* clusters =
        envoy_api_v2_route_WeightedCluster_clusters(weighted_cluster,
                                                    &clusters_size);
    uint32_t sum_of_weights = 0;
    for (size_t j = 0; j < clusters_size; ++j) {
      const envoy_api_v2_route_WeightedCluster_ClusterWeight* cluster_weight =
          clusters[j];
      XdsApi::RdsUpdate::RdsRoute::ClusterWeight cluster;
      cluster.name = UpbStringToStdString(
          envoy_api_v2_route_WeightedCluster_ClusterWeight_name(
              cluster_weight));
      if (cluster.name.empty()) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "RouteAction weighted_cluster cluster contains empty cluster "
            "name.");
      }
      const google_protobuf_UInt32Value* weight =
          envoy_api_v2_route_WeightedCluster_ClusterWeight_weight(
              cluster_weight);
      if (weight == nullptr) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "RouteAction weighted_cluster cluster missing weight");
      }
      cluster.weight = google_protobuf_UInt32Value_value(weight);
      sum_of_weights += cluster.weight;
      rds_route->weighted_clusters.emplace_back(std::move(cluster));
    }
    if (total_weight != sum_of_weights) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "RouteAction weighted_cluster has incorrect total weight");
    }
    if (rds_route->weighted_clusters.empty()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "RouteAction weighted_cluster has no valid clusters specified.");
    }
  } else {
    // No cluster or weighted_clusters found in RouteAction, ignore this route.
    *ignore_route = true;
    return GRPC_ERROR_NONE;
  }
  return GRPC_ERROR_NONE;
}

grpc_error* RouteConfigParse(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_api_v2_RouteConfiguration* route_config,
    const std::string& expected_server_name, const bool xds_routing_enabled,
    XdsApi::RdsUpdate* rds_update) {
  MaybeLogRouteConfiguration(client, tracer, symtab, route_config);
  // Get the virtual hosts.
  size_t size;
  const envoy_api_v2_route_VirtualHost* const* virtual_hosts =
      envoy_api_v2_RouteConfiguration_virtual_hosts(route_config, &size);
  // Find the best matched virtual host.
  // The search order for 4 groups of domain patterns:
  //   1. Exact match.
  //   2. Suffix match (e.g., "*ABC").
  //   3. Prefix match (e.g., "ABC*").
  //   4. Universe match (i.e., "*").
  // Within each group, longest match wins.
  // If the same best matched domain pattern appears in multiple virtual hosts,
  // the first matched virtual host wins.
  const envoy_api_v2_route_VirtualHost* target_virtual_host = nullptr;
  MatchType best_match_type = INVALID_MATCH;
  size_t longest_match = 0;
  // Check each domain pattern in each virtual host to determine the best
  // matched virtual host.
  for (size_t i = 0; i < size; ++i) {
    size_t domain_size;
    upb_strview const* domains =
        envoy_api_v2_route_VirtualHost_domains(virtual_hosts[i], &domain_size);
    for (size_t j = 0; j < domain_size; ++j) {
      const std::string domain_pattern(domains[j].data, domains[j].size);
      // Check the match type first. Skip the pattern if it's not better than
      // current match.
      const MatchType match_type = DomainPatternMatchType(domain_pattern);
      if (match_type == INVALID_MATCH) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Invalid domain pattern.");
      }
      if (match_type > best_match_type) continue;
      if (match_type == best_match_type &&
          domain_pattern.size() <= longest_match) {
        continue;
      }
      // Skip if match fails.
      if (!DomainMatch(match_type, domain_pattern, expected_server_name)) {
        continue;
      }
      // Choose this match.
      target_virtual_host = virtual_hosts[i];
      best_match_type = match_type;
      longest_match = domain_pattern.size();
      if (best_match_type == EXACT_MATCH) break;
    }
    if (best_match_type == EXACT_MATCH) break;
  }
  if (target_virtual_host == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "No matched virtual host found in the route config.");
  }
  // Get the route list from the matched virtual host.
  const envoy_api_v2_route_Route* const* routes =
      envoy_api_v2_route_VirtualHost_routes(target_virtual_host, &size);
  if (size < 1) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "No route found in the virtual host.");
  }
  // If xds_routing is not configured, only look at the last one in the route
  // list (the default route)
  if (!xds_routing_enabled) {
    const envoy_api_v2_route_Route* route = routes[size - 1];
    const envoy_api_v2_route_RouteMatch* match =
        envoy_api_v2_route_Route_match(route);
    XdsApi::RdsUpdate::RdsRoute rds_route;
    rds_route.matchers.path_matcher.type = XdsApi::RdsUpdate::RdsRoute::
        Matchers::PathMatcher::PathMatcherType::PREFIX;
    // if xds routing is not enabled, we must be working on the default route;
    // in this case, we must have an empty or single slash prefix.
    if (!envoy_api_v2_route_RouteMatch_has_prefix(match)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "No prefix field found in Default RouteMatch.");
    }
    const upb_strview prefix = envoy_api_v2_route_RouteMatch_prefix(match);
    if (!upb_strview_eql(prefix, upb_strview_makez("")) &&
        !upb_strview_eql(prefix, upb_strview_makez("/"))) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Default route must have empty prefix.");
    }
    bool ignore_route = false;
    grpc_error* error = RouteActionParse(route, &rds_route, &ignore_route);
    if (error != GRPC_ERROR_NONE) return error;
    if (ignore_route) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Default route action is ignored.");
    }
    rds_update->routes.emplace_back(std::move(rds_route));
    return GRPC_ERROR_NONE;
  }
  // Loop over the whole list of routes
  for (size_t i = 0; i < size; ++i) {
    const envoy_api_v2_route_Route* route = routes[i];
    const envoy_api_v2_route_RouteMatch* match =
        envoy_api_v2_route_Route_match(route);
    size_t query_parameters_size;
    static_cast<void>(envoy_api_v2_route_RouteMatch_query_parameters(
        match, &query_parameters_size));
    if (query_parameters_size > 0) {
      continue;
    }
    XdsApi::RdsUpdate::RdsRoute rds_route;
    bool ignore_route = false;
    grpc_error* error = RoutePathMatchParse(match, &rds_route, &ignore_route);
    if (error != GRPC_ERROR_NONE) return error;
    if (ignore_route) continue;
    error = RouteHeaderMatchersParse(match, &rds_route);
    if (error != GRPC_ERROR_NONE) return error;
    error = RouteRuntimeFractionParse(match, &rds_route);
    if (error != GRPC_ERROR_NONE) return error;
    error = RouteActionParse(route, &rds_route, &ignore_route);
    if (error != GRPC_ERROR_NONE) return error;
    if (ignore_route) continue;
    const google_protobuf_BoolValue* case_sensitive =
        envoy_api_v2_route_RouteMatch_case_sensitive(match);
    if (case_sensitive != nullptr &&
        !google_protobuf_BoolValue_value(case_sensitive)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "case_sensitive if set must be set to true.");
    }
    rds_update->routes.emplace_back(std::move(rds_route));
  }
  if (rds_update->routes.empty()) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No valid routes specified.");
  }
  return GRPC_ERROR_NONE;
}

grpc_error* LdsResponseParse(XdsClient* client, TraceFlag* tracer,
                             upb_symtab* symtab,
                             const envoy_api_v2_DiscoveryResponse* response,
                             const std::string& expected_server_name,
                             const bool xds_routing_enabled,
                             absl::optional<XdsApi::LdsUpdate>* lds_update,
                             upb_arena* arena) {
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_api_v2_DiscoveryResponse_resources(response, &size);
  for (size_t i = 0; i < size; ++i) {
    // Check the type_url of the resource.
    const upb_strview type_url = google_protobuf_Any_type_url(resources[i]);
    if (!upb_strview_eql(type_url, upb_strview_makez(XdsApi::kLdsTypeUrl))) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource is not LDS.");
    }
    // Decode the listener.
    const upb_strview encoded_listener =
        google_protobuf_Any_value(resources[i]);
    const envoy_api_v2_Listener* listener = envoy_api_v2_Listener_parse(
        encoded_listener.data, encoded_listener.size, arena);
    if (listener == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Can't decode listener.");
    }
    // Check listener name. Ignore unexpected listeners.
    const upb_strview name = envoy_api_v2_Listener_name(listener);
    const upb_strview expected_name =
        upb_strview_makez(expected_server_name.c_str());
    if (!upb_strview_eql(name, expected_name)) continue;
    // Get api_listener and decode it to http_connection_manager.
    const envoy_config_listener_v2_ApiListener* api_listener =
        envoy_api_v2_Listener_api_listener(listener);
    if (api_listener == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Listener has no ApiListener.");
    }
    const upb_strview encoded_api_listener = google_protobuf_Any_value(
        envoy_config_listener_v2_ApiListener_api_listener(api_listener));
    const envoy_config_filter_network_http_connection_manager_v2_HttpConnectionManager*
        http_connection_manager =
            envoy_config_filter_network_http_connection_manager_v2_HttpConnectionManager_parse(
                encoded_api_listener.data, encoded_api_listener.size, arena);
    // Found inlined route_config. Parse it to find the cluster_name.
    if (envoy_config_filter_network_http_connection_manager_v2_HttpConnectionManager_has_route_config(
            http_connection_manager)) {
      const envoy_api_v2_RouteConfiguration* route_config =
          envoy_config_filter_network_http_connection_manager_v2_HttpConnectionManager_route_config(
              http_connection_manager);
      XdsApi::RdsUpdate rds_update;
      grpc_error* error = RouteConfigParse(client, tracer, symtab, route_config,
                                           expected_server_name,
                                           xds_routing_enabled, &rds_update);
      if (error != GRPC_ERROR_NONE) return error;
      lds_update->emplace();
      (*lds_update)->rds_update.emplace(std::move(rds_update));
      return GRPC_ERROR_NONE;
    }
    // Validate that RDS must be used to get the route_config dynamically.
    if (!envoy_config_filter_network_http_connection_manager_v2_HttpConnectionManager_has_rds(
            http_connection_manager)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "HttpConnectionManager neither has inlined route_config nor RDS.");
    }
    const envoy_config_filter_network_http_connection_manager_v2_Rds* rds =
        envoy_config_filter_network_http_connection_manager_v2_HttpConnectionManager_rds(
            http_connection_manager);
    // Check that the ConfigSource specifies ADS.
    const envoy_api_v2_core_ConfigSource* config_source =
        envoy_config_filter_network_http_connection_manager_v2_Rds_config_source(
            rds);
    if (config_source == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "HttpConnectionManager missing config_source for RDS.");
    }
    if (!envoy_api_v2_core_ConfigSource_has_ads(config_source)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "HttpConnectionManager ConfigSource for RDS does not specify ADS.");
    }
    // Get the route_config_name.
    lds_update->emplace();
    (*lds_update)->route_config_name = UpbStringToStdString(
        envoy_config_filter_network_http_connection_manager_v2_Rds_route_config_name(
            rds));
    return GRPC_ERROR_NONE;
  }
  return GRPC_ERROR_NONE;
}

grpc_error* RdsResponseParse(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_api_v2_DiscoveryResponse* response,
    const std::string& expected_server_name,
    const std::set<absl::string_view>& expected_route_configuration_names,
    const bool xds_routing_enabled,
    absl::optional<XdsApi::RdsUpdate>* rds_update, upb_arena* arena) {
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_api_v2_DiscoveryResponse_resources(response, &size);
  for (size_t i = 0; i < size; ++i) {
    // Check the type_url of the resource.
    const upb_strview type_url = google_protobuf_Any_type_url(resources[i]);
    if (!upb_strview_eql(type_url, upb_strview_makez(XdsApi::kRdsTypeUrl))) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource is not RDS.");
    }
    // Decode the route_config.
    const upb_strview encoded_route_config =
        google_protobuf_Any_value(resources[i]);
    const envoy_api_v2_RouteConfiguration* route_config =
        envoy_api_v2_RouteConfiguration_parse(encoded_route_config.data,
                                              encoded_route_config.size, arena);
    if (route_config == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Can't decode route_config.");
    }
    // Check route_config_name. Ignore unexpected route_config.
    const upb_strview route_config_name =
        envoy_api_v2_RouteConfiguration_name(route_config);
    absl::string_view route_config_name_strview(route_config_name.data,
                                                route_config_name.size);
    if (expected_route_configuration_names.find(route_config_name_strview) ==
        expected_route_configuration_names.end()) {
      continue;
    }
    // Parse the route_config.
    XdsApi::RdsUpdate local_rds_update;
    grpc_error* error = RouteConfigParse(
        client, tracer, symtab, route_config, expected_server_name,
        xds_routing_enabled, &local_rds_update);
    if (error != GRPC_ERROR_NONE) return error;
    rds_update->emplace(std::move(local_rds_update));
    return GRPC_ERROR_NONE;
  }
  return GRPC_ERROR_NONE;
}

grpc_error* CdsResponseParse(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_api_v2_DiscoveryResponse* response,
    const std::set<absl::string_view>& expected_cluster_names,
    XdsApi::CdsUpdateMap* cds_update_map, upb_arena* arena) {
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_api_v2_DiscoveryResponse_resources(response, &size);
  // Parse all the resources in the CDS response.
  for (size_t i = 0; i < size; ++i) {
    XdsApi::CdsUpdate cds_update;
    // Check the type_url of the resource.
    const upb_strview type_url = google_protobuf_Any_type_url(resources[i]);
    if (!upb_strview_eql(type_url, upb_strview_makez(XdsApi::kCdsTypeUrl))) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource is not CDS.");
    }
    // Decode the cluster.
    const upb_strview encoded_cluster = google_protobuf_Any_value(resources[i]);
    const envoy_api_v2_Cluster* cluster = envoy_api_v2_Cluster_parse(
        encoded_cluster.data, encoded_cluster.size, arena);
    if (cluster == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Can't decode cluster.");
    }
    MaybeLogCluster(client, tracer, symtab, cluster);
    // Ignore unexpected cluster names.
    upb_strview cluster_name = envoy_api_v2_Cluster_name(cluster);
    absl::string_view cluster_name_strview(cluster_name.data,
                                           cluster_name.size);
    if (expected_cluster_names.find(cluster_name_strview) ==
        expected_cluster_names.end()) {
      continue;
    }
    // Check the cluster_discovery_type.
    if (!envoy_api_v2_Cluster_has_type(cluster)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("DiscoveryType not found.");
    }
    if (envoy_api_v2_Cluster_type(cluster) != envoy_api_v2_Cluster_EDS) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("DiscoveryType is not EDS.");
    }
    // Check the EDS config source.
    const envoy_api_v2_Cluster_EdsClusterConfig* eds_cluster_config =
        envoy_api_v2_Cluster_eds_cluster_config(cluster);
    const envoy_api_v2_core_ConfigSource* eds_config =
        envoy_api_v2_Cluster_EdsClusterConfig_eds_config(eds_cluster_config);
    if (!envoy_api_v2_core_ConfigSource_has_ads(eds_config)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "EDS ConfigSource is not ADS.");
    }
    // Record EDS service_name (if any).
    upb_strview service_name =
        envoy_api_v2_Cluster_EdsClusterConfig_service_name(eds_cluster_config);
    if (service_name.size != 0) {
      cds_update.eds_service_name = UpbStringToStdString(service_name);
    }
    // Check the LB policy.
    if (envoy_api_v2_Cluster_lb_policy(cluster) !=
        envoy_api_v2_Cluster_ROUND_ROBIN) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "LB policy is not ROUND_ROBIN.");
    }
    // Record LRS server name (if any).
    const envoy_api_v2_core_ConfigSource* lrs_server =
        envoy_api_v2_Cluster_lrs_server(cluster);
    if (lrs_server != nullptr) {
      if (!envoy_api_v2_core_ConfigSource_has_self(lrs_server)) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "LRS ConfigSource is not self.");
      }
      cds_update.lrs_load_reporting_server_name.emplace("");
    }
    cds_update_map->emplace(UpbStringToStdString(cluster_name),
                            std::move(cds_update));
  }
  return GRPC_ERROR_NONE;
}

grpc_error* ServerAddressParseAndAppend(
    const envoy_api_v2_endpoint_LbEndpoint* lb_endpoint,
    ServerAddressList* list) {
  // If health_status is not HEALTHY or UNKNOWN, skip this endpoint.
  const int32_t health_status =
      envoy_api_v2_endpoint_LbEndpoint_health_status(lb_endpoint);
  if (health_status != envoy_api_v2_core_UNKNOWN &&
      health_status != envoy_api_v2_core_HEALTHY) {
    return GRPC_ERROR_NONE;
  }
  // Find the ip:port.
  const envoy_api_v2_endpoint_Endpoint* endpoint =
      envoy_api_v2_endpoint_LbEndpoint_endpoint(lb_endpoint);
  const envoy_api_v2_core_Address* address =
      envoy_api_v2_endpoint_Endpoint_address(endpoint);
  const envoy_api_v2_core_SocketAddress* socket_address =
      envoy_api_v2_core_Address_socket_address(address);
  upb_strview address_strview =
      envoy_api_v2_core_SocketAddress_address(socket_address);
  uint32_t port = envoy_api_v2_core_SocketAddress_port_value(socket_address);
  if (GPR_UNLIKELY(port >> 16) != 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Invalid port.");
  }
  // Populate grpc_resolved_address.
  grpc_resolved_address addr;
  char* address_str = static_cast<char*>(gpr_malloc(address_strview.size + 1));
  memcpy(address_str, address_strview.data, address_strview.size);
  address_str[address_strview.size] = '\0';
  grpc_string_to_sockaddr(&addr, address_str, port);
  gpr_free(address_str);
  // Append the address to the list.
  list->emplace_back(addr, nullptr);
  return GRPC_ERROR_NONE;
}

grpc_error* LocalityParse(
    const envoy_api_v2_endpoint_LocalityLbEndpoints* locality_lb_endpoints,
    XdsApi::PriorityListUpdate::LocalityMap::Locality* output_locality) {
  // Parse LB weight.
  const google_protobuf_UInt32Value* lb_weight =
      envoy_api_v2_endpoint_LocalityLbEndpoints_load_balancing_weight(
          locality_lb_endpoints);
  // If LB weight is not specified, it means this locality is assigned no load.
  // TODO(juanlishen): When we support CDS to configure the inter-locality
  // policy, we should change the LB weight handling.
  output_locality->lb_weight =
      lb_weight != nullptr ? google_protobuf_UInt32Value_value(lb_weight) : 0;
  if (output_locality->lb_weight == 0) return GRPC_ERROR_NONE;
  // Parse locality name.
  const envoy_api_v2_core_Locality* locality =
      envoy_api_v2_endpoint_LocalityLbEndpoints_locality(locality_lb_endpoints);
  upb_strview region = envoy_api_v2_core_Locality_region(locality);
  upb_strview zone = envoy_api_v2_core_Locality_region(locality);
  upb_strview sub_zone = envoy_api_v2_core_Locality_sub_zone(locality);
  output_locality->name = MakeRefCounted<XdsLocalityName>(
      UpbStringToStdString(region), UpbStringToStdString(zone),
      UpbStringToStdString(sub_zone));
  // Parse the addresses.
  size_t size;
  const envoy_api_v2_endpoint_LbEndpoint* const* lb_endpoints =
      envoy_api_v2_endpoint_LocalityLbEndpoints_lb_endpoints(
          locality_lb_endpoints, &size);
  for (size_t i = 0; i < size; ++i) {
    grpc_error* error = ServerAddressParseAndAppend(
        lb_endpoints[i], &output_locality->serverlist);
    if (error != GRPC_ERROR_NONE) return error;
  }
  // Parse the priority.
  output_locality->priority =
      envoy_api_v2_endpoint_LocalityLbEndpoints_priority(locality_lb_endpoints);
  return GRPC_ERROR_NONE;
}

grpc_error* DropParseAndAppend(
    const envoy_api_v2_ClusterLoadAssignment_Policy_DropOverload* drop_overload,
    XdsApi::DropConfig* drop_config) {
  // Get the category.
  upb_strview category =
      envoy_api_v2_ClusterLoadAssignment_Policy_DropOverload_category(
          drop_overload);
  if (category.size == 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty drop category name");
  }
  // Get the drop rate (per million).
  const envoy_type_FractionalPercent* drop_percentage =
      envoy_api_v2_ClusterLoadAssignment_Policy_DropOverload_drop_percentage(
          drop_overload);
  uint32_t numerator = envoy_type_FractionalPercent_numerator(drop_percentage);
  const auto denominator =
      static_cast<envoy_type_FractionalPercent_DenominatorType>(
          envoy_type_FractionalPercent_denominator(drop_percentage));
  // Normalize to million.
  switch (denominator) {
    case envoy_type_FractionalPercent_HUNDRED:
      numerator *= 10000;
      break;
    case envoy_type_FractionalPercent_TEN_THOUSAND:
      numerator *= 100;
      break;
    case envoy_type_FractionalPercent_MILLION:
      break;
    default:
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unknown denominator type");
  }
  // Cap numerator to 1000000.
  numerator = GPR_MIN(numerator, 1000000);
  drop_config->AddCategory(UpbStringToStdString(category), numerator);
  return GRPC_ERROR_NONE;
}

grpc_error* EdsResponseParse(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_api_v2_DiscoveryResponse* response,
    const std::set<absl::string_view>& expected_eds_service_names,
    XdsApi::EdsUpdateMap* eds_update_map, upb_arena* arena) {
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_api_v2_DiscoveryResponse_resources(response, &size);
  for (size_t i = 0; i < size; ++i) {
    XdsApi::EdsUpdate eds_update;
    // Check the type_url of the resource.
    upb_strview type_url = google_protobuf_Any_type_url(resources[i]);
    if (!upb_strview_eql(type_url, upb_strview_makez(XdsApi::kEdsTypeUrl))) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource is not EDS.");
    }
    // Get the cluster_load_assignment.
    upb_strview encoded_cluster_load_assignment =
        google_protobuf_Any_value(resources[i]);
    envoy_api_v2_ClusterLoadAssignment* cluster_load_assignment =
        envoy_api_v2_ClusterLoadAssignment_parse(
            encoded_cluster_load_assignment.data,
            encoded_cluster_load_assignment.size, arena);
    if (cluster_load_assignment == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Can't parse cluster_load_assignment.");
    }
    MaybeLogClusterLoadAssignment(client, tracer, symtab,
                                  cluster_load_assignment);
    // Check the cluster name (which actually means eds_service_name). Ignore
    // unexpected names.
    upb_strview cluster_name = envoy_api_v2_ClusterLoadAssignment_cluster_name(
        cluster_load_assignment);
    absl::string_view cluster_name_strview(cluster_name.data,
                                           cluster_name.size);
    if (expected_eds_service_names.find(cluster_name_strview) ==
        expected_eds_service_names.end()) {
      continue;
    }
    // Get the endpoints.
    size_t locality_size;
    const envoy_api_v2_endpoint_LocalityLbEndpoints* const* endpoints =
        envoy_api_v2_ClusterLoadAssignment_endpoints(cluster_load_assignment,
                                                     &locality_size);
    for (size_t j = 0; j < locality_size; ++j) {
      XdsApi::PriorityListUpdate::LocalityMap::Locality locality;
      grpc_error* error = LocalityParse(endpoints[j], &locality);
      if (error != GRPC_ERROR_NONE) return error;
      // Filter out locality with weight 0.
      if (locality.lb_weight == 0) continue;
      eds_update.priority_list_update.Add(locality);
    }
    for (uint32_t priority = 0;
         priority < eds_update.priority_list_update.size(); ++priority) {
      auto* locality_map = eds_update.priority_list_update.Find(priority);
      if (locality_map == nullptr || locality_map->size() == 0) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "EDS update includes sparse priority list");
      }
    }
    // Get the drop config.
    eds_update.drop_config = MakeRefCounted<XdsApi::DropConfig>();
    const envoy_api_v2_ClusterLoadAssignment_Policy* policy =
        envoy_api_v2_ClusterLoadAssignment_policy(cluster_load_assignment);
    if (policy != nullptr) {
      size_t drop_size;
      const envoy_api_v2_ClusterLoadAssignment_Policy_DropOverload* const*
          drop_overload =
              envoy_api_v2_ClusterLoadAssignment_Policy_drop_overloads(
                  policy, &drop_size);
      for (size_t j = 0; j < drop_size; ++j) {
        grpc_error* error =
            DropParseAndAppend(drop_overload[j], eds_update.drop_config.get());
        if (error != GRPC_ERROR_NONE) return error;
      }
    }
    eds_update_map->emplace(UpbStringToStdString(cluster_name),
                            std::move(eds_update));
  }
  return GRPC_ERROR_NONE;
}

}  // namespace

grpc_error* XdsApi::ParseAdsResponse(
    const grpc_slice& encoded_response, const std::string& expected_server_name,
    const std::set<absl::string_view>& expected_route_configuration_names,
    const std::set<absl::string_view>& expected_cluster_names,
    const std::set<absl::string_view>& expected_eds_service_names,
    absl::optional<LdsUpdate>* lds_update,
    absl::optional<RdsUpdate>* rds_update, CdsUpdateMap* cds_update_map,
    EdsUpdateMap* eds_update_map, std::string* version, std::string* nonce,
    std::string* type_url) {
  upb::Arena arena;
  // Decode the response.
  const envoy_api_v2_DiscoveryResponse* response =
      envoy_api_v2_DiscoveryResponse_parse(
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(encoded_response)),
          GRPC_SLICE_LENGTH(encoded_response), arena.ptr());
  // If decoding fails, output an empty type_url and return.
  if (response == nullptr) {
    *type_url = "";
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Can't decode the whole response.");
  }
  MaybeLogDiscoveryResponse(client_, tracer_, symtab_.ptr(), response);
  // Record the type_url, the version_info, and the nonce of the response.
  upb_strview type_url_strview =
      envoy_api_v2_DiscoveryResponse_type_url(response);
  *type_url = UpbStringToStdString(type_url_strview);
  upb_strview version_info =
      envoy_api_v2_DiscoveryResponse_version_info(response);
  *version = UpbStringToStdString(version_info);
  upb_strview nonce_strview = envoy_api_v2_DiscoveryResponse_nonce(response);
  *nonce = UpbStringToStdString(nonce_strview);
  // Parse the response according to the resource type.
  if (*type_url == kLdsTypeUrl) {
    return LdsResponseParse(client_, tracer_, symtab_.ptr(), response,
                            expected_server_name, xds_routing_enabled_,
                            lds_update, arena.ptr());
  } else if (*type_url == kRdsTypeUrl) {
    return RdsResponseParse(client_, tracer_, symtab_.ptr(), response,
                            expected_server_name,
                            expected_route_configuration_names,
                            xds_routing_enabled_, rds_update, arena.ptr());
  } else if (*type_url == kCdsTypeUrl) {
    return CdsResponseParse(client_, tracer_, symtab_.ptr(), response,
                            expected_cluster_names, cds_update_map,
                            arena.ptr());
  } else if (*type_url == kEdsTypeUrl) {
    return EdsResponseParse(client_, tracer_, symtab_.ptr(), response,
                            expected_eds_service_names, eds_update_map,
                            arena.ptr());
  } else {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Unsupported ADS resource type.");
  }
}

namespace {

void MaybeLogLrsRequest(
    XdsClient* client, TraceFlag* tracer, upb_symtab* symtab,
    const envoy_service_load_stats_v2_LoadStatsRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_service_load_stats_v2_LoadStatsRequest_getmsgdef(symtab);
    char buf[10240];
    upb_text_encode(request, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] constructed LRS request: %s", client,
            buf);
  }
}

grpc_slice SerializeLrsRequest(
    const envoy_service_load_stats_v2_LoadStatsRequest* request,
    upb_arena* arena) {
  size_t output_length;
  char* output = envoy_service_load_stats_v2_LoadStatsRequest_serialize(
      request, arena, &output_length);
  return grpc_slice_from_copied_buffer(output, output_length);
}

}  // namespace

grpc_slice XdsApi::CreateLrsInitialRequest(const std::string& server_name) {
  upb::Arena arena;
  // Create a request.
  envoy_service_load_stats_v2_LoadStatsRequest* request =
      envoy_service_load_stats_v2_LoadStatsRequest_new(arena.ptr());
  // Populate node.
  envoy_api_v2_core_Node* node_msg =
      envoy_service_load_stats_v2_LoadStatsRequest_mutable_node(request,
                                                                arena.ptr());
  PopulateNode(arena.ptr(), node_, build_version_, user_agent_name_,
               server_name, node_msg);
  envoy_api_v2_core_Node_add_client_features(
      node_msg, upb_strview_makez("envoy.lrs.supports_send_all_clusters"),
      arena.ptr());
  MaybeLogLrsRequest(client_, tracer_, symtab_.ptr(), request);
  return SerializeLrsRequest(request, arena.ptr());
}

namespace {

void LocalityStatsPopulate(envoy_api_v2_endpoint_UpstreamLocalityStats* output,
                           const XdsLocalityName& locality_name,
                           const XdsClusterLocalityStats::Snapshot& snapshot,
                           upb_arena* arena) {
  // Set locality.
  envoy_api_v2_core_Locality* locality =
      envoy_api_v2_endpoint_UpstreamLocalityStats_mutable_locality(output,
                                                                   arena);
  if (!locality_name.region().empty()) {
    envoy_api_v2_core_Locality_set_region(
        locality, upb_strview_makez(locality_name.region().c_str()));
  }
  if (!locality_name.zone().empty()) {
    envoy_api_v2_core_Locality_set_zone(
        locality, upb_strview_makez(locality_name.zone().c_str()));
  }
  if (!locality_name.sub_zone().empty()) {
    envoy_api_v2_core_Locality_set_sub_zone(
        locality, upb_strview_makez(locality_name.sub_zone().c_str()));
  }
  // Set total counts.
  envoy_api_v2_endpoint_UpstreamLocalityStats_set_total_successful_requests(
      output, snapshot.total_successful_requests);
  envoy_api_v2_endpoint_UpstreamLocalityStats_set_total_requests_in_progress(
      output, snapshot.total_requests_in_progress);
  envoy_api_v2_endpoint_UpstreamLocalityStats_set_total_error_requests(
      output, snapshot.total_error_requests);
  envoy_api_v2_endpoint_UpstreamLocalityStats_set_total_issued_requests(
      output, snapshot.total_issued_requests);
  // Add backend metrics.
  for (const auto& p : snapshot.backend_metrics) {
    const std::string& metric_name = p.first;
    const XdsClusterLocalityStats::BackendMetric& metric_value = p.second;
    envoy_api_v2_endpoint_EndpointLoadMetricStats* load_metric =
        envoy_api_v2_endpoint_UpstreamLocalityStats_add_load_metric_stats(
            output, arena);
    envoy_api_v2_endpoint_EndpointLoadMetricStats_set_metric_name(
        load_metric, upb_strview_make(metric_name.data(), metric_name.size()));
    envoy_api_v2_endpoint_EndpointLoadMetricStats_set_num_requests_finished_with_metric(
        load_metric, metric_value.num_requests_finished_with_metric);
    envoy_api_v2_endpoint_EndpointLoadMetricStats_set_total_metric_value(
        load_metric, metric_value.total_metric_value);
  }
}

}  // namespace

grpc_slice XdsApi::CreateLrsRequest(
    ClusterLoadReportMap cluster_load_report_map) {
  upb::Arena arena;
  // Create a request.
  envoy_service_load_stats_v2_LoadStatsRequest* request =
      envoy_service_load_stats_v2_LoadStatsRequest_new(arena.ptr());
  for (auto& p : cluster_load_report_map) {
    const std::string& cluster_name = p.first.first;
    const std::string& eds_service_name = p.first.second;
    const ClusterLoadReport& load_report = p.second;
    // Add cluster stats.
    envoy_api_v2_endpoint_ClusterStats* cluster_stats =
        envoy_service_load_stats_v2_LoadStatsRequest_add_cluster_stats(
            request, arena.ptr());
    // Set the cluster name.
    envoy_api_v2_endpoint_ClusterStats_set_cluster_name(
        cluster_stats,
        upb_strview_make(cluster_name.data(), cluster_name.size()));
    // Set EDS service name, if non-empty.
    if (!eds_service_name.empty()) {
      envoy_api_v2_endpoint_ClusterStats_set_cluster_service_name(
          cluster_stats,
          upb_strview_make(eds_service_name.data(), eds_service_name.size()));
    }
    // Add locality stats.
    for (const auto& p : load_report.locality_stats) {
      const XdsLocalityName& locality_name = *p.first;
      const auto& snapshot = p.second;
      envoy_api_v2_endpoint_UpstreamLocalityStats* locality_stats =
          envoy_api_v2_endpoint_ClusterStats_add_upstream_locality_stats(
              cluster_stats, arena.ptr());
      LocalityStatsPopulate(locality_stats, locality_name, snapshot,
                            arena.ptr());
    }
    // Add dropped requests.
    uint64_t total_dropped_requests = 0;
    for (const auto& p : load_report.dropped_requests) {
      const char* category = p.first.c_str();
      const uint64_t count = p.second;
      envoy_api_v2_endpoint_ClusterStats_DroppedRequests* dropped_requests =
          envoy_api_v2_endpoint_ClusterStats_add_dropped_requests(cluster_stats,
                                                                  arena.ptr());
      envoy_api_v2_endpoint_ClusterStats_DroppedRequests_set_category(
          dropped_requests, upb_strview_makez(category));
      envoy_api_v2_endpoint_ClusterStats_DroppedRequests_set_dropped_count(
          dropped_requests, count);
      total_dropped_requests += count;
    }
    // Set total dropped requests.
    envoy_api_v2_endpoint_ClusterStats_set_total_dropped_requests(
        cluster_stats, total_dropped_requests);
    // Set real load report interval.
    gpr_timespec timespec =
        grpc_millis_to_timespec(load_report.load_report_interval, GPR_TIMESPAN);
    google_protobuf_Duration* load_report_interval =
        envoy_api_v2_endpoint_ClusterStats_mutable_load_report_interval(
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
  const envoy_service_load_stats_v2_LoadStatsResponse* decoded_response =
      envoy_service_load_stats_v2_LoadStatsResponse_parse(
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(encoded_response)),
          GRPC_SLICE_LENGTH(encoded_response), arena.ptr());
  // Parse the response.
  if (decoded_response == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Can't decode response.");
  }
  // Check send_all_clusters.
  if (envoy_service_load_stats_v2_LoadStatsResponse_send_all_clusters(
          decoded_response)) {
    *send_all_clusters = true;
  } else {
    // Store the cluster names.
    size_t size;
    const upb_strview* clusters =
        envoy_service_load_stats_v2_LoadStatsResponse_clusters(decoded_response,
                                                               &size);
    for (size_t i = 0; i < size; ++i) {
      cluster_names->emplace(clusters[i].data, clusters[i].size);
    }
  }
  // Get the load report interval.
  const google_protobuf_Duration* load_reporting_interval_duration =
      envoy_service_load_stats_v2_LoadStatsResponse_load_reporting_interval(
          decoded_response);
  gpr_timespec timespec{
      google_protobuf_Duration_seconds(load_reporting_interval_duration),
      google_protobuf_Duration_nanos(load_reporting_interval_duration),
      GPR_TIMESPAN};
  *load_reporting_interval = gpr_time_to_millis(timespec);
  return GRPC_ERROR_NONE;
}

}  // namespace grpc_core
