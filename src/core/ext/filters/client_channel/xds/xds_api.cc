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

#include <grpc/impl/codegen/log.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/xds/xds_api.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

#include "envoy/api/v2/cds.upb.h"
#include "envoy/api/v2/core/address.upb.h"
#include "envoy/api/v2/core/base.upb.h"
#include "envoy/api/v2/core/config_source.upb.h"
#include "envoy/api/v2/core/health_check.upb.h"
#include "envoy/api/v2/discovery.upb.h"
#include "envoy/api/v2/eds.upb.h"
#include "envoy/api/v2/endpoint/endpoint.upb.h"
#include "envoy/api/v2/endpoint/load_report.upb.h"
#include "envoy/api/v2/lds.upb.h"
#include "envoy/api/v2/rds.upb.h"
#include "envoy/api/v2/route/route.upb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.upb.h"
#include "envoy/config/listener/v2/api_listener.upb.h"
#include "envoy/service/load_stats/v2/lrs.upb.h"
#include "envoy/type/percent.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "google/rpc/status.upb.h"
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
    google_protobuf_Struct_FieldsEntry* field =
        google_protobuf_Struct_add_fields(metadata_pb, arena);
    google_protobuf_Struct_FieldsEntry_set_key(
        field, upb_strview_makez(p.first.c_str()));
    google_protobuf_Value* value =
        google_protobuf_Struct_FieldsEntry_mutable_value(field, arena);
    PopulateMetadataValue(arena, value, p.second);
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
                  const char* build_version, envoy_api_v2_core_Node* node_msg) {
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
  envoy_api_v2_core_Node_set_build_version(node_msg,
                                           upb_strview_makez(build_version));
}

envoy_api_v2_DiscoveryRequest* CreateDiscoveryRequest(
    upb_arena* arena, const char* type_url, const std::string& version,
    const std::string& nonce, grpc_error* error, const XdsBootstrap::Node* node,
    const char* build_version) {
  // Create a request.
  envoy_api_v2_DiscoveryRequest* request =
      envoy_api_v2_DiscoveryRequest_new(arena);
  // Set type_url.
  envoy_api_v2_DiscoveryRequest_set_type_url(request,
                                             upb_strview_makez(type_url));
  // Set version_info.
  if (!version.empty()) {
    envoy_api_v2_DiscoveryRequest_set_version_info(
        request, upb_strview_makez(version.c_str()));
  }
  // Set nonce.
  if (!nonce.empty()) {
    envoy_api_v2_DiscoveryRequest_set_response_nonce(
        request, upb_strview_makez(nonce.c_str()));
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
        envoy_api_v2_DiscoveryRequest_mutable_error_detail(request, arena);
    google_rpc_Status_set_message(error_detail, error_description_strview);
    GRPC_ERROR_UNREF(error);
  }
  // Populate node.
  if (build_version != nullptr) {
    envoy_api_v2_core_Node* node_msg =
        envoy_api_v2_DiscoveryRequest_mutable_node(request, arena);
    PopulateNode(arena, node, build_version, node_msg);
  }
  return request;
}

grpc_slice SerializeDiscoveryRequest(upb_arena* arena,
                                     envoy_api_v2_DiscoveryRequest* request) {
  size_t output_length;
  char* output =
      envoy_api_v2_DiscoveryRequest_serialize(request, arena, &output_length);
  return grpc_slice_from_copied_buffer(output, output_length);
}

}  // namespace

grpc_slice XdsApi::CreateUnsupportedTypeNackRequest(const std::string& type_url,
                                                    const std::string& nonce,
                                                    grpc_error* error) {
  upb::Arena arena;
  envoy_api_v2_DiscoveryRequest* request = CreateDiscoveryRequest(
      arena.ptr(), type_url.c_str(), /*version=*/"", nonce, error,
      /*node=*/nullptr, /*build_version=*/nullptr);
  return SerializeDiscoveryRequest(arena.ptr(), request);
}

grpc_slice XdsApi::CreateLdsRequest(const std::string& server_name,
                                    const std::string& version,
                                    const std::string& nonce, grpc_error* error,
                                    bool populate_node) {
  upb::Arena arena;
  envoy_api_v2_DiscoveryRequest* request =
      CreateDiscoveryRequest(arena.ptr(), kLdsTypeUrl, version, nonce, error,
                             populate_node ? node_ : nullptr,
                             populate_node ? build_version_ : nullptr);
  // Add resource_name.
  envoy_api_v2_DiscoveryRequest_add_resource_names(
      request, upb_strview_make(server_name.data(), server_name.size()),
      arena.ptr());
  return SerializeDiscoveryRequest(arena.ptr(), request);
}

grpc_slice XdsApi::CreateRdsRequest(const std::string& route_config_name,
                                    const std::string& version,
                                    const std::string& nonce, grpc_error* error,
                                    bool populate_node) {
  upb::Arena arena;
  envoy_api_v2_DiscoveryRequest* request =
      CreateDiscoveryRequest(arena.ptr(), kRdsTypeUrl, version, nonce, error,
                             populate_node ? node_ : nullptr,
                             populate_node ? build_version_ : nullptr);
  // Add resource_name.
  envoy_api_v2_DiscoveryRequest_add_resource_names(
      request,
      upb_strview_make(route_config_name.data(), route_config_name.size()),
      arena.ptr());
  return SerializeDiscoveryRequest(arena.ptr(), request);
}

grpc_slice XdsApi::CreateCdsRequest(const std::set<StringView>& cluster_names,
                                    const std::string& version,
                                    const std::string& nonce, grpc_error* error,
                                    bool populate_node) {
  upb::Arena arena;
  envoy_api_v2_DiscoveryRequest* request =
      CreateDiscoveryRequest(arena.ptr(), kCdsTypeUrl, version, nonce, error,
                             populate_node ? node_ : nullptr,
                             populate_node ? build_version_ : nullptr);
  // Add resource_names.
  for (const auto& cluster_name : cluster_names) {
    envoy_api_v2_DiscoveryRequest_add_resource_names(
        request, upb_strview_make(cluster_name.data(), cluster_name.size()),
        arena.ptr());
  }
  return SerializeDiscoveryRequest(arena.ptr(), request);
}

grpc_slice XdsApi::CreateEdsRequest(
    const std::set<StringView>& eds_service_names, const std::string& version,
    const std::string& nonce, grpc_error* error, bool populate_node) {
  upb::Arena arena;
  envoy_api_v2_DiscoveryRequest* request =
      CreateDiscoveryRequest(arena.ptr(), kEdsTypeUrl, version, nonce, error,
                             populate_node ? node_ : nullptr,
                             populate_node ? build_version_ : nullptr);
  // Add resource_names.
  for (const auto& eds_service_name : eds_service_names) {
    envoy_api_v2_DiscoveryRequest_add_resource_names(
        request,
        upb_strview_make(eds_service_name.data(), eds_service_name.size()),
        arena.ptr());
  }
  return SerializeDiscoveryRequest(arena.ptr(), request);
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
    StringView pattern_suffix(domain_pattern.c_str() + 1);
    StringView host_suffix(expected_host_name.c_str() +
                           expected_host_name.size() - pattern_suffix.size());
    return pattern_suffix == host_suffix;
  } else if (match_type == PREFIX_MATCH) {
    // Asterisk must match at least one char.
    if (expected_host_name.size() < domain_pattern.size()) return false;
    StringView pattern_prefix(domain_pattern.c_str(),
                              domain_pattern.size() - 1);
    StringView host_prefix(expected_host_name.c_str(), pattern_prefix.size());
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

grpc_error* RouteConfigParse(
    const envoy_api_v2_RouteConfiguration* route_config,
    const std::string& expected_server_name, XdsApi::RdsUpdate* rds_update) {
  // Strip off port from server name, if any.
  size_t pos = expected_server_name.find(':');
  std::string expected_host_name = expected_server_name.substr(0, pos);
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
      if (!DomainMatch(match_type, domain_pattern, expected_host_name)) {
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
  // Only look at the last one in the route list (the default route),
  const envoy_api_v2_route_Route* route = routes[size - 1];
  // Validate that the match field must have a prefix field which is an empty
  // string.
  const envoy_api_v2_route_RouteMatch* match =
      envoy_api_v2_route_Route_match(route);
  if (!envoy_api_v2_route_RouteMatch_has_prefix(match)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "No prefix field found in RouteMatch.");
  }
  const upb_strview prefix = envoy_api_v2_route_RouteMatch_prefix(match);
  if (!upb_strview_eql(prefix, upb_strview_makez(""))) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Prefix is not empty string.");
  }
  if (!envoy_api_v2_route_Route_has_route(route)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "No RouteAction found in route.");
  }
  const envoy_api_v2_route_RouteAction* route_action =
      envoy_api_v2_route_Route_route(route);
  // Get the cluster in the RouteAction.
  if (!envoy_api_v2_route_RouteAction_has_cluster(route_action)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "No cluster found in RouteAction.");
  }
  const upb_strview cluster =
      envoy_api_v2_route_RouteAction_cluster(route_action);
  rds_update->cluster_name = std::string(cluster.data, cluster.size);
  return GRPC_ERROR_NONE;
}

grpc_error* LdsResponseParse(const envoy_api_v2_DiscoveryResponse* response,
                             const std::string& expected_server_name,
                             XdsApi::LdsUpdate* lds_update, upb_arena* arena) {
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_api_v2_DiscoveryResponse_resources(response, &size);
  if (size < 1) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "LDS response contains 0 resource.");
  }
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
          "Listener doesn't have ApiListener.");
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
      grpc_error* error =
          RouteConfigParse(route_config, expected_server_name, &rds_update);
      if (error != GRPC_ERROR_NONE) return error;
      lds_update->rds_update.emplace(std::move(rds_update));
      const upb_strview route_config_name =
          envoy_api_v2_RouteConfiguration_name(route_config);
      lds_update->route_config_name =
          std::string(route_config_name.data, route_config_name.size);
      return GRPC_ERROR_NONE;
    }
    // Validate that RDS must be used to get the route_config dynamically.
    if (!envoy_config_filter_network_http_connection_manager_v2_HttpConnectionManager_has_rds(
            http_connection_manager)) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "HttpConnectionManager neither has inlined route_config nor RDS.");
    }
    // Get the route_config_name.
    const envoy_config_filter_network_http_connection_manager_v2_Rds* rds =
        envoy_config_filter_network_http_connection_manager_v2_HttpConnectionManager_rds(
            http_connection_manager);
    const upb_strview route_config_name =
        envoy_config_filter_network_http_connection_manager_v2_Rds_route_config_name(
            rds);
    lds_update->route_config_name =
        std::string(route_config_name.data, route_config_name.size);
    return GRPC_ERROR_NONE;
  }
  return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "No listener found for expected server name.");
}

grpc_error* RdsResponseParse(const envoy_api_v2_DiscoveryResponse* response,
                             const std::string& expected_server_name,
                             const std::string& expected_route_config_name,
                             XdsApi::RdsUpdate* rds_update, upb_arena* arena) {
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_api_v2_DiscoveryResponse_resources(response, &size);
  if (size < 1) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "RDS response contains 0 resource.");
  }
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
    const upb_strview name = envoy_api_v2_RouteConfiguration_name(route_config);
    const upb_strview expected_name =
        upb_strview_makez(expected_route_config_name.c_str());
    if (!upb_strview_eql(name, expected_name)) continue;
    // Parse the route_config.
    XdsApi::RdsUpdate local_rds_update;
    grpc_error* error =
        RouteConfigParse(route_config, expected_server_name, &local_rds_update);
    if (error != GRPC_ERROR_NONE) return error;
    *rds_update = std::move(local_rds_update);
    return GRPC_ERROR_NONE;
  }
  return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "No route config found for expected name.");
}

grpc_error* CdsResponseParse(const envoy_api_v2_DiscoveryResponse* response,
                             XdsApi::CdsUpdateMap* cds_update_map,
                             upb_arena* arena) {
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_api_v2_DiscoveryResponse_resources(response, &size);
  if (size < 1) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "CDS response contains 0 resource.");
  }
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
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("ConfigSource is not ADS.");
    }
    // Record EDS service_name (if any).
    upb_strview service_name =
        envoy_api_v2_Cluster_EdsClusterConfig_service_name(eds_cluster_config);
    if (service_name.size != 0) {
      cds_update.eds_service_name =
          std::string(service_name.data, service_name.size);
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
            "ConfigSource is not self.");
      }
      cds_update.lrs_load_reporting_server_name.emplace("");
    }
    upb_strview cluster_name = envoy_api_v2_Cluster_name(cluster);
    cds_update_map->emplace(std::string(cluster_name.data, cluster_name.size),
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
      std::string(region.data, region.size), std::string(zone.data, zone.size),
      std::string(sub_zone.data, sub_zone.size));
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
    XdsApi::DropConfig* drop_config, bool* drop_all) {
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
  if (numerator == 1000000) *drop_all = true;
  drop_config->AddCategory(std::string(category.data, category.size),
                           numerator);
  return GRPC_ERROR_NONE;
}

grpc_error* EdsResponsedParse(
    const envoy_api_v2_DiscoveryResponse* response,
    const std::set<StringView>& expected_eds_service_names,
    XdsApi::EdsUpdateMap* eds_update_map, upb_arena* arena) {
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_api_v2_DiscoveryResponse_resources(response, &size);
  if (size < 1) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "EDS response contains 0 resource.");
  }
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
    // Check the cluster name (which actually means eds_service_name). Ignore
    // unexpected names.
    upb_strview cluster_name = envoy_api_v2_ClusterLoadAssignment_cluster_name(
        cluster_load_assignment);
    StringView cluster_name_strview(cluster_name.data, cluster_name.size);
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
            DropParseAndAppend(drop_overload[j], eds_update.drop_config.get(),
                               &eds_update.drop_all);
        if (error != GRPC_ERROR_NONE) return error;
      }
    }
    // Validate the update content.
    if (eds_update.priority_list_update.empty() && !eds_update.drop_all) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "EDS response doesn't contain any valid "
          "locality but doesn't require to drop all calls.");
    }
    eds_update_map->emplace(std::string(cluster_name.data, cluster_name.size),
                            std::move(eds_update));
  }
  return GRPC_ERROR_NONE;
}

}  // namespace

grpc_error* XdsApi::ParseAdsResponse(
    const grpc_slice& encoded_response, const std::string& expected_server_name,
    const std::string& expected_route_config_name,
    const std::set<StringView>& expected_eds_service_names,
    LdsUpdate* lds_update, RdsUpdate* rds_update, CdsUpdateMap* cds_update_map,
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
  // Record the type_url, the version_info, and the nonce of the response.
  upb_strview type_url_strview =
      envoy_api_v2_DiscoveryResponse_type_url(response);
  *type_url = std::string(type_url_strview.data, type_url_strview.size);
  upb_strview version_info =
      envoy_api_v2_DiscoveryResponse_version_info(response);
  *version = std::string(version_info.data, version_info.size);
  upb_strview nonce_strview = envoy_api_v2_DiscoveryResponse_nonce(response);
  *nonce = std::string(nonce_strview.data, nonce_strview.size);
  // Parse the response according to the resource type.
  if (*type_url == kLdsTypeUrl) {
    return LdsResponseParse(response, expected_server_name, lds_update,
                            arena.ptr());
  } else if (*type_url == kRdsTypeUrl) {
    return RdsResponseParse(response, expected_server_name,
                            expected_route_config_name, rds_update,
                            arena.ptr());
  } else if (*type_url == kCdsTypeUrl) {
    return CdsResponseParse(response, cds_update_map, arena.ptr());
  } else if (*type_url == kEdsTypeUrl) {
    return EdsResponsedParse(response, expected_eds_service_names,
                             eds_update_map, arena.ptr());
  } else {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Unsupported ADS resource type.");
  }
}

namespace {

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
  PopulateNode(arena.ptr(), node_, build_version_, node_msg);
  // Add cluster stats. There is only one because we only use one server name in
  // one channel.
  envoy_api_v2_endpoint_ClusterStats* cluster_stats =
      envoy_service_load_stats_v2_LoadStatsRequest_add_cluster_stats(
          request, arena.ptr());
  // Set the cluster name.
  envoy_api_v2_endpoint_ClusterStats_set_cluster_name(
      cluster_stats, upb_strview_makez(server_name.c_str()));
  return SerializeLrsRequest(request, arena.ptr());
}

namespace {

void LocalityStatsPopulate(
    envoy_api_v2_endpoint_UpstreamLocalityStats* output,
    const std::pair<const RefCountedPtr<XdsLocalityName>,
                    XdsClientStats::LocalityStats::Snapshot>& input,
    upb_arena* arena) {
  // Set sub_zone.
  envoy_api_v2_core_Locality* locality =
      envoy_api_v2_endpoint_UpstreamLocalityStats_mutable_locality(output,
                                                                   arena);
  envoy_api_v2_core_Locality_set_sub_zone(
      locality, upb_strview_makez(input.first->sub_zone().c_str()));
  // Set total counts.
  const XdsClientStats::LocalityStats::Snapshot& snapshot = input.second;
  envoy_api_v2_endpoint_UpstreamLocalityStats_set_total_successful_requests(
      output, snapshot.total_successful_requests);
  envoy_api_v2_endpoint_UpstreamLocalityStats_set_total_requests_in_progress(
      output, snapshot.total_requests_in_progress);
  envoy_api_v2_endpoint_UpstreamLocalityStats_set_total_error_requests(
      output, snapshot.total_error_requests);
  envoy_api_v2_endpoint_UpstreamLocalityStats_set_total_issued_requests(
      output, snapshot.total_issued_requests);
  // Add load metric stats.
  for (auto& p : snapshot.load_metric_stats) {
    const char* metric_name = p.first.c_str();
    const XdsClientStats::LocalityStats::LoadMetric::Snapshot& metric_value =
        p.second;
    envoy_api_v2_endpoint_EndpointLoadMetricStats* load_metric =
        envoy_api_v2_endpoint_UpstreamLocalityStats_add_load_metric_stats(
            output, arena);
    envoy_api_v2_endpoint_EndpointLoadMetricStats_set_metric_name(
        load_metric, upb_strview_makez(metric_name));
    envoy_api_v2_endpoint_EndpointLoadMetricStats_set_num_requests_finished_with_metric(
        load_metric, metric_value.num_requests_finished_with_metric);
    envoy_api_v2_endpoint_EndpointLoadMetricStats_set_total_metric_value(
        load_metric, metric_value.total_metric_value);
  }
}

}  // namespace

grpc_slice XdsApi::CreateLrsRequest(
    std::map<StringView, std::set<XdsClientStats*>, StringLess>
        client_stats_map) {
  upb::Arena arena;
  // Get the snapshots.
  std::map<StringView, grpc_core::InlinedVector<XdsClientStats::Snapshot, 1>,
           StringLess>
      snapshot_map;
  for (auto& p : client_stats_map) {
    const StringView& cluster_name = p.first;
    for (auto* client_stats : p.second) {
      XdsClientStats::Snapshot snapshot = client_stats->GetSnapshotAndReset();
      // Prune unused locality stats.
      client_stats->PruneLocalityStats();
      if (snapshot.IsAllZero()) continue;
      snapshot_map[cluster_name].emplace_back(std::move(snapshot));
    }
  }
  // When all the counts are zero, return empty slice.
  if (snapshot_map.empty()) return grpc_empty_slice();
  // Create a request.
  envoy_service_load_stats_v2_LoadStatsRequest* request =
      envoy_service_load_stats_v2_LoadStatsRequest_new(arena.ptr());
  for (auto& p : snapshot_map) {
    const StringView& cluster_name = p.first;
    const auto& snapshot_list = p.second;
    for (size_t i = 0; i < snapshot_list.size(); ++i) {
      const auto& snapshot = snapshot_list[i];
      // Add cluster stats.
      envoy_api_v2_endpoint_ClusterStats* cluster_stats =
          envoy_service_load_stats_v2_LoadStatsRequest_add_cluster_stats(
              request, arena.ptr());
      // Set the cluster name.
      envoy_api_v2_endpoint_ClusterStats_set_cluster_name(
          cluster_stats,
          upb_strview_make(cluster_name.data(), cluster_name.size()));
      // Add locality stats.
      for (auto& p : snapshot.upstream_locality_stats) {
        envoy_api_v2_endpoint_UpstreamLocalityStats* locality_stats =
            envoy_api_v2_endpoint_ClusterStats_add_upstream_locality_stats(
                cluster_stats, arena.ptr());
        LocalityStatsPopulate(locality_stats, p, arena.ptr());
      }
      // Add dropped requests.
      for (auto& p : snapshot.dropped_requests) {
        const char* category = p.first.c_str();
        const uint64_t count = p.second;
        envoy_api_v2_endpoint_ClusterStats_DroppedRequests* dropped_requests =
            envoy_api_v2_endpoint_ClusterStats_add_dropped_requests(
                cluster_stats, arena.ptr());
        envoy_api_v2_endpoint_ClusterStats_DroppedRequests_set_category(
            dropped_requests, upb_strview_makez(category));
        envoy_api_v2_endpoint_ClusterStats_DroppedRequests_set_dropped_count(
            dropped_requests, count);
      }
      // Set total dropped requests.
      envoy_api_v2_endpoint_ClusterStats_set_total_dropped_requests(
          cluster_stats, snapshot.total_dropped_requests);
      // Set real load report interval.
      gpr_timespec timespec =
          grpc_millis_to_timespec(snapshot.load_report_interval, GPR_TIMESPAN);
      google_protobuf_Duration* load_report_interval =
          envoy_api_v2_endpoint_ClusterStats_mutable_load_report_interval(
              cluster_stats, arena.ptr());
      google_protobuf_Duration_set_seconds(load_report_interval,
                                           timespec.tv_sec);
      google_protobuf_Duration_set_nanos(load_report_interval,
                                         timespec.tv_nsec);
    }
  }
  return SerializeLrsRequest(request, arena.ptr());
}

grpc_error* XdsApi::ParseLrsResponse(const grpc_slice& encoded_response,
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
  // Store the cluster names.
  size_t size;
  const upb_strview* clusters =
      envoy_service_load_stats_v2_LoadStatsResponse_clusters(decoded_response,
                                                             &size);
  for (size_t i = 0; i < size; ++i) {
    cluster_names->emplace(clusters[i].data, clusters[i].size);
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
