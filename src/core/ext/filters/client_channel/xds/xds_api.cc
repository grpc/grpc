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
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include <grpc/impl/codegen/log.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/xds/xds_api.h"
#include "src/core/lib/gpr/useful.h"
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

XdsApi::XdsApi(XdsClient* client, TraceFlag* tracer,
               const XdsBootstrap::Node* node)
    : client_(client),
      tracer_(tracer),
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
      google_protobuf_Struct_FieldsEntry* field =
          google_protobuf_Struct_add_fields(metadata, arena);
      google_protobuf_Struct_FieldsEntry_set_key(
          field, upb_strview_makez("PROXYLESS_CLIENT_HOSTNAME"));
      google_protobuf_Value* value =
          google_protobuf_Struct_FieldsEntry_mutable_value(field, arena);
      google_protobuf_Value_set_string_value(
          value, upb_strview_make(server_name.data(), server_name.size()));
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

envoy_api_v2_DiscoveryRequest* CreateDiscoveryRequest(
    upb_arena* arena, const char* type_url, const std::string& version,
    const std::string& nonce, grpc_error* error) {
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
  return request;
}

inline absl::string_view UpbStringToAbsl(const upb_strview& str) {
  return absl::string_view(str.data, str.size);
}

inline void AddStringField(const char* name, const upb_strview& value,
                           std::vector<std::string>* fields,
                           bool add_if_empty = false) {
  if (value.size > 0 || add_if_empty) {
    fields->emplace_back(
        absl::StrCat(name, ": \"", UpbStringToAbsl(value), "\""));
  }
}

inline void AddLocalityField(int indent_level,
                             const envoy_api_v2_core_Locality* locality,
                             std::vector<std::string>* fields) {
  std::string indent =
      absl::StrJoin(std::vector<std::string>(indent_level, "  "), "");
  // region
  std::string field = absl::StrCat(indent, "region");
  AddStringField(field.c_str(), envoy_api_v2_core_Locality_region(locality),
                 fields);
  // zone
  field = absl::StrCat(indent, "zone");
  AddStringField(field.c_str(), envoy_api_v2_core_Locality_zone(locality),
                 fields);
  // sub_zone
  field = absl::StrCat(indent, "sub_zone");
  AddStringField(field.c_str(), envoy_api_v2_core_Locality_sub_zone(locality),
                 fields);
}

void AddNodeLogFields(const envoy_api_v2_core_Node* node,
                      std::vector<std::string>* fields) {
  fields->emplace_back("node {");
  // id
  AddStringField("  id", envoy_api_v2_core_Node_id(node), fields);
  // metadata
  const google_protobuf_Struct* metadata =
      envoy_api_v2_core_Node_metadata(node);
  if (metadata != nullptr) {
    fields->emplace_back("  metadata {");
    size_t num_entries;
    const google_protobuf_Struct_FieldsEntry* const* entries =
        google_protobuf_Struct_fields(metadata, &num_entries);
    for (size_t i = 0; i < num_entries; ++i) {
      fields->emplace_back("    field {");
      // key
      AddStringField("      key",
                     google_protobuf_Struct_FieldsEntry_key(entries[i]),
                     fields);
      // value
      const google_protobuf_Value* value =
          google_protobuf_Struct_FieldsEntry_value(entries[i]);
      if (value != nullptr) {
        std::string value_str;
        if (google_protobuf_Value_has_string_value(value)) {
          value_str = absl::StrCat(
              "string_value: \"",
              UpbStringToAbsl(google_protobuf_Value_string_value(value)), "\"");
        } else if (google_protobuf_Value_has_null_value(value)) {
          value_str = "null_value: NULL_VALUE";
        } else if (google_protobuf_Value_has_number_value(value)) {
          value_str = absl::StrCat("double_value: ",
                                   google_protobuf_Value_number_value(value));
        } else if (google_protobuf_Value_has_bool_value(value)) {
          value_str = absl::StrCat("bool_value: ",
                                   google_protobuf_Value_bool_value(value));
        } else if (google_protobuf_Value_has_struct_value(value)) {
          value_str = "struct_value: <not printed>";
        } else if (google_protobuf_Value_has_list_value(value)) {
          value_str = "list_value: <not printed>";
        } else {
          value_str = "<unknown>";
        }
        fields->emplace_back(absl::StrCat("      value { ", value_str, " }"));
      }
      fields->emplace_back("    }");
    }
    fields->emplace_back("  }");
  }
  // locality
  const envoy_api_v2_core_Locality* locality =
      envoy_api_v2_core_Node_locality(node);
  if (locality != nullptr) {
    fields->emplace_back("  locality {");
    AddLocalityField(2, locality, fields);
    fields->emplace_back("  }");
  }
  // build_version
  AddStringField("  build_version", envoy_api_v2_core_Node_build_version(node),
                 fields);
  // user_agent_name
  AddStringField("  user_agent_name",
                 envoy_api_v2_core_Node_user_agent_name(node), fields);
  // user_agent_version
  AddStringField("  user_agent_version",
                 envoy_api_v2_core_Node_user_agent_version(node), fields);
  // client_features
  size_t num_client_features;
  const upb_strview* client_features =
      envoy_api_v2_core_Node_client_features(node, &num_client_features);
  for (size_t i = 0; i < num_client_features; ++i) {
    AddStringField("  client_features", client_features[i], fields);
  }
  fields->emplace_back("}");
}

void MaybeLogDiscoveryRequest(XdsClient* client, TraceFlag* tracer,
                              const envoy_api_v2_DiscoveryRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    // TODO(roth): When we can upgrade upb, use upb textformat code to dump
    // the raw proto instead of doing this manually.
    std::vector<std::string> fields;
    // version_info
    AddStringField("version_info",
                   envoy_api_v2_DiscoveryRequest_version_info(request),
                   &fields);
    // node
    const envoy_api_v2_core_Node* node =
        envoy_api_v2_DiscoveryRequest_node(request);
    if (node != nullptr) AddNodeLogFields(node, &fields);
    // resource_names
    size_t num_resource_names;
    const upb_strview* resource_names =
        envoy_api_v2_DiscoveryRequest_resource_names(request,
                                                     &num_resource_names);
    for (size_t i = 0; i < num_resource_names; ++i) {
      AddStringField("resource_names", resource_names[i], &fields);
    }
    // type_url
    AddStringField("type_url", envoy_api_v2_DiscoveryRequest_type_url(request),
                   &fields);
    // response_nonce
    AddStringField("response_nonce",
                   envoy_api_v2_DiscoveryRequest_response_nonce(request),
                   &fields);
    // error_detail
    const struct google_rpc_Status* error_detail =
        envoy_api_v2_DiscoveryRequest_error_detail(request);
    if (error_detail != nullptr) {
      fields.emplace_back("error_detail {");
      // code
      int32_t code = google_rpc_Status_code(error_detail);
      if (code != 0) fields.emplace_back(absl::StrCat("  code: ", code));
      // message
      AddStringField("  message", google_rpc_Status_message(error_detail),
                     &fields);
      fields.emplace_back("}");
    }
    gpr_log(GPR_DEBUG, "[xds_client %p] constructed ADS request: %s", client,
            absl::StrJoin(fields, "\n").c_str());
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

grpc_slice XdsApi::CreateUnsupportedTypeNackRequest(const std::string& type_url,
                                                    const std::string& nonce,
                                                    grpc_error* error) {
  upb::Arena arena;
  envoy_api_v2_DiscoveryRequest* request = CreateDiscoveryRequest(
      arena.ptr(), type_url.c_str(), /*version=*/"", nonce, error);
  MaybeLogDiscoveryRequest(client_, tracer_, request);
  return SerializeDiscoveryRequest(arena.ptr(), request);
}

grpc_slice XdsApi::CreateLdsRequest(const std::string& server_name,
                                    const std::string& version,
                                    const std::string& nonce, grpc_error* error,
                                    bool populate_node) {
  upb::Arena arena;
  envoy_api_v2_DiscoveryRequest* request =
      CreateDiscoveryRequest(arena.ptr(), kLdsTypeUrl, version, nonce, error);
  // Populate node.
  if (populate_node) {
    envoy_api_v2_core_Node* node_msg =
        envoy_api_v2_DiscoveryRequest_mutable_node(request, arena.ptr());
    PopulateNode(arena.ptr(), node_, build_version_, user_agent_name_, "",
                 node_msg);
  }
  // Add resource_name.
  envoy_api_v2_DiscoveryRequest_add_resource_names(
      request, upb_strview_make(server_name.data(), server_name.size()),
      arena.ptr());
  MaybeLogDiscoveryRequest(client_, tracer_, request);
  return SerializeDiscoveryRequest(arena.ptr(), request);
}

grpc_slice XdsApi::CreateRdsRequest(const std::string& route_config_name,
                                    const std::string& version,
                                    const std::string& nonce, grpc_error* error,
                                    bool populate_node) {
  upb::Arena arena;
  envoy_api_v2_DiscoveryRequest* request =
      CreateDiscoveryRequest(arena.ptr(), kRdsTypeUrl, version, nonce, error);
  // Populate node.
  if (populate_node) {
    envoy_api_v2_core_Node* node_msg =
        envoy_api_v2_DiscoveryRequest_mutable_node(request, arena.ptr());
    PopulateNode(arena.ptr(), node_, build_version_, user_agent_name_, "",
                 node_msg);
  }
  // Add resource_name.
  envoy_api_v2_DiscoveryRequest_add_resource_names(
      request,
      upb_strview_make(route_config_name.data(), route_config_name.size()),
      arena.ptr());
  MaybeLogDiscoveryRequest(client_, tracer_, request);
  return SerializeDiscoveryRequest(arena.ptr(), request);
}

grpc_slice XdsApi::CreateCdsRequest(
    const std::set<absl::string_view>& cluster_names,
    const std::string& version, const std::string& nonce, grpc_error* error,
    bool populate_node) {
  upb::Arena arena;
  envoy_api_v2_DiscoveryRequest* request =
      CreateDiscoveryRequest(arena.ptr(), kCdsTypeUrl, version, nonce, error);
  // Populate node.
  if (populate_node) {
    envoy_api_v2_core_Node* node_msg =
        envoy_api_v2_DiscoveryRequest_mutable_node(request, arena.ptr());
    PopulateNode(arena.ptr(), node_, build_version_, user_agent_name_, "",
                 node_msg);
  }
  // Add resource_names.
  for (const auto& cluster_name : cluster_names) {
    envoy_api_v2_DiscoveryRequest_add_resource_names(
        request, upb_strview_make(cluster_name.data(), cluster_name.size()),
        arena.ptr());
  }
  MaybeLogDiscoveryRequest(client_, tracer_, request);
  return SerializeDiscoveryRequest(arena.ptr(), request);
}

grpc_slice XdsApi::CreateEdsRequest(
    const std::set<absl::string_view>& eds_service_names,
    const std::string& version, const std::string& nonce, grpc_error* error,
    bool populate_node) {
  upb::Arena arena;
  envoy_api_v2_DiscoveryRequest* request =
      CreateDiscoveryRequest(arena.ptr(), kEdsTypeUrl, version, nonce, error);
  // Populate node.
  if (populate_node) {
    envoy_api_v2_core_Node* node_msg =
        envoy_api_v2_DiscoveryRequest_mutable_node(request, arena.ptr());
    PopulateNode(arena.ptr(), node_, build_version_, user_agent_name_, "",
                 node_msg);
  }
  // Add resource_names.
  for (const auto& eds_service_name : eds_service_names) {
    envoy_api_v2_DiscoveryRequest_add_resource_names(
        request,
        upb_strview_make(eds_service_name.data(), eds_service_name.size()),
        arena.ptr());
  }
  MaybeLogDiscoveryRequest(client_, tracer_, request);
  return SerializeDiscoveryRequest(arena.ptr(), request);
}

namespace {

void MaybeLogDiscoveryResponse(XdsClient* client, TraceFlag* tracer,
                               const envoy_api_v2_DiscoveryResponse* response) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    // TODO(roth): When we can upgrade upb, use upb textformat code to dump
    // the raw proto instead of doing this manually.
    std::vector<std::string> fields;
    // version_info
    AddStringField("version_info",
                   envoy_api_v2_DiscoveryResponse_version_info(response),
                   &fields);
    // resources
    size_t num_resources;
    envoy_api_v2_DiscoveryResponse_resources(response, &num_resources);
    fields.emplace_back(
        absl::StrCat("resources: <", num_resources, " element(s)>"));
    // type_url
    AddStringField("type_url",
                   envoy_api_v2_DiscoveryResponse_type_url(response), &fields);
    // nonce
    AddStringField("nonce", envoy_api_v2_DiscoveryResponse_nonce(response),
                   &fields);
    gpr_log(GPR_DEBUG, "[xds_client %p] received response: %s", client,
            absl::StrJoin(fields, "\n").c_str());
  }
}

void MaybeLogRouteConfiguration(
    XdsClient* client, TraceFlag* tracer,
    const envoy_api_v2_RouteConfiguration* route_config) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    // TODO(roth): When we can upgrade upb, use upb textformat code to dump
    // the raw proto instead of doing this manually.
    std::vector<std::string> fields;
    // name
    AddStringField("name", envoy_api_v2_RouteConfiguration_name(route_config),
                   &fields);
    // virtual_hosts
    size_t num_virtual_hosts;
    const envoy_api_v2_route_VirtualHost* const* virtual_hosts =
        envoy_api_v2_RouteConfiguration_virtual_hosts(route_config,
                                                      &num_virtual_hosts);
    for (size_t i = 0; i < num_virtual_hosts; ++i) {
      const auto* virtual_host = virtual_hosts[i];
      fields.push_back("virtual_hosts {");
      // name
      AddStringField(
          "  name", envoy_api_v2_route_VirtualHost_name(virtual_host), &fields);
      // domains
      size_t num_domains;
      const upb_strview* const domains =
          envoy_api_v2_route_VirtualHost_domains(virtual_host, &num_domains);
      for (size_t j = 0; j < num_domains; ++j) {
        AddStringField("  domains", domains[j], &fields);
      }
      // routes
      size_t num_routes;
      const envoy_api_v2_route_Route* const* routes =
          envoy_api_v2_route_VirtualHost_routes(virtual_host, &num_routes);
      for (size_t j = 0; j < num_routes; ++j) {
        const auto* route = routes[j];
        fields.push_back("  route {");
        // name
        AddStringField("    name", envoy_api_v2_route_Route_name(route),
                       &fields);
        // match
        const envoy_api_v2_route_RouteMatch* match =
            envoy_api_v2_route_Route_match(route);
        if (match != nullptr) {
          fields.emplace_back("    match {");
          // path matching
          if (envoy_api_v2_route_RouteMatch_has_prefix(match)) {
            AddStringField("      prefix",
                           envoy_api_v2_route_RouteMatch_prefix(match), &fields,
                           /*add_if_empty=*/true);
          } else if (envoy_api_v2_route_RouteMatch_has_path(match)) {
            AddStringField("      path",
                           envoy_api_v2_route_RouteMatch_path(match), &fields,
                           /*add_if_empty=*/true);
          } else if (envoy_api_v2_route_RouteMatch_has_regex(match)) {
            AddStringField("      regex",
                           envoy_api_v2_route_RouteMatch_regex(match), &fields,
                           /*add_if_empty=*/true);
          } else if (envoy_api_v2_route_RouteMatch_has_safe_regex(match)) {
            fields.emplace_back("      safe_regex: <not printed>");
          } else {
            fields.emplace_back("      <unknown path matching type>");
          }
          // header matching
          size_t num_headers;
          envoy_api_v2_route_RouteMatch_headers(match, &num_headers);
          if (num_headers > 0) {
            fields.emplace_back(
                absl::StrCat("      headers: <", num_headers, " element(s)>"));
          }
          fields.emplace_back("    }");
        }
        // action
        if (envoy_api_v2_route_Route_has_route(route)) {
          const envoy_api_v2_route_RouteAction* action =
              envoy_api_v2_route_Route_route(route);
          fields.emplace_back("    route {");
          if (envoy_api_v2_route_RouteAction_has_cluster(action)) {
            AddStringField("      cluster",
                           envoy_api_v2_route_RouteAction_cluster(action),
                           &fields);
          } else if (envoy_api_v2_route_RouteAction_has_cluster_header(
                         action)) {
            AddStringField(
                "      cluster_header",
                envoy_api_v2_route_RouteAction_cluster_header(action), &fields);
          } else if (envoy_api_v2_route_RouteAction_has_weighted_clusters(
                         action)) {
            fields.emplace_back("      weighted_clusters: <not printed>");
          }
          fields.emplace_back("    }");
        } else if (envoy_api_v2_route_Route_has_redirect(route)) {
          fields.emplace_back("    redirect: <not printed>");
        } else if (envoy_api_v2_route_Route_has_direct_response(route)) {
          fields.emplace_back("    direct_response: <not printed>");
        } else if (envoy_api_v2_route_Route_has_filter_action(route)) {
          fields.emplace_back("    filter_action: <not printed>");
        }
        fields.push_back("  }");
      }
      fields.push_back("}");
    }
    gpr_log(GPR_DEBUG, "[xds_client %p] RouteConfiguration: %s", client,
            absl::StrJoin(fields, "\n").c_str());
  }
}

void MaybeLogCluster(XdsClient* client, TraceFlag* tracer,
                     const envoy_api_v2_Cluster* cluster) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    // TODO(roth): When we can upgrade upb, use upb textformat code to dump
    // the raw proto instead of doing this manually.
    std::vector<std::string> fields;
    // name
    AddStringField("name", envoy_api_v2_Cluster_name(cluster), &fields);
    // type
    if (envoy_api_v2_Cluster_has_type(cluster)) {
      fields.emplace_back(
          absl::StrCat("type: ", envoy_api_v2_Cluster_type(cluster)));
    } else if (envoy_api_v2_Cluster_has_cluster_type(cluster)) {
      fields.emplace_back("cluster_type: <not printed>");
    } else {
      fields.emplace_back("<unknown type>");
    }
    // eds_cluster_config
    const envoy_api_v2_Cluster_EdsClusterConfig* eds_cluster_config =
        envoy_api_v2_Cluster_eds_cluster_config(cluster);
    if (eds_cluster_config != nullptr) {
      fields.emplace_back("eds_cluster_config {");
      // eds_config
      const struct envoy_api_v2_core_ConfigSource* eds_config =
          envoy_api_v2_Cluster_EdsClusterConfig_eds_config(eds_cluster_config);
      if (eds_config != nullptr) {
        if (envoy_api_v2_core_ConfigSource_has_ads(eds_config)) {
          fields.emplace_back("  eds_config { ads {} }");
        } else {
          fields.emplace_back("  eds_config: <non-ADS type>");
        }
      }
      // service_name
      AddStringField("  service_name",
                     envoy_api_v2_Cluster_EdsClusterConfig_service_name(
                         eds_cluster_config),
                     &fields);
      fields.emplace_back("}");
    }
    // lb_policy
    fields.emplace_back(
        absl::StrCat("lb_policy: ", envoy_api_v2_Cluster_lb_policy(cluster)));
    // lrs_server
    const envoy_api_v2_core_ConfigSource* lrs_server =
        envoy_api_v2_Cluster_lrs_server(cluster);
    if (lrs_server != nullptr) {
      if (envoy_api_v2_core_ConfigSource_has_self(lrs_server)) {
        fields.emplace_back("lrs_server { self {} }");
      } else {
        fields.emplace_back("lrs_server: <non-self type>");
      }
    }
    gpr_log(GPR_DEBUG, "[xds_client %p] Cluster: %s", client,
            absl::StrJoin(fields, "\n").c_str());
  }
}

void MaybeLogClusterLoadAssignment(
    XdsClient* client, TraceFlag* tracer,
    const envoy_api_v2_ClusterLoadAssignment* cla) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    // TODO(roth): When we can upgrade upb, use upb textformat code to dump
    // the raw proto instead of doing this manually.
    std::vector<std::string> fields;
    // cluster_name
    AddStringField("cluster_name",
                   envoy_api_v2_ClusterLoadAssignment_cluster_name(cla),
                   &fields);
    // endpoints
    size_t num_localities;
    const struct envoy_api_v2_endpoint_LocalityLbEndpoints* const*
        locality_endpoints =
            envoy_api_v2_ClusterLoadAssignment_endpoints(cla, &num_localities);
    for (size_t i = 0; i < num_localities; ++i) {
      const auto* locality_endpoint = locality_endpoints[i];
      fields.emplace_back("endpoints {");
      // locality
      const auto* locality =
          envoy_api_v2_endpoint_LocalityLbEndpoints_locality(locality_endpoint);
      if (locality != nullptr) {
        fields.emplace_back("  locality {");
        AddLocalityField(2, locality, &fields);
        fields.emplace_back("  }");
      }
      // lb_endpoints
      size_t num_lb_endpoints;
      const envoy_api_v2_endpoint_LbEndpoint* const* lb_endpoints =
          envoy_api_v2_endpoint_LocalityLbEndpoints_lb_endpoints(
              locality_endpoint, &num_lb_endpoints);
      for (size_t j = 0; j < num_lb_endpoints; ++j) {
        const auto* lb_endpoint = lb_endpoints[j];
        fields.emplace_back("  lb_endpoints {");
        // health_status
        uint32_t health_status =
            envoy_api_v2_endpoint_LbEndpoint_health_status(lb_endpoint);
        if (health_status > 0) {
          fields.emplace_back(
              absl::StrCat("    health_status: ", health_status));
        }
        // endpoint
        const envoy_api_v2_endpoint_Endpoint* endpoint =
            envoy_api_v2_endpoint_LbEndpoint_endpoint(lb_endpoint);
        if (endpoint != nullptr) {
          fields.emplace_back("    endpoint {");
          // address
          const auto* address =
              envoy_api_v2_endpoint_Endpoint_address(endpoint);
          if (address != nullptr) {
            fields.emplace_back("      address {");
            // socket_address
            const auto* socket_address =
                envoy_api_v2_core_Address_socket_address(address);
            if (socket_address != nullptr) {
              fields.emplace_back("        socket_address {");
              // address
              AddStringField(
                  "          address",
                  envoy_api_v2_core_SocketAddress_address(socket_address),
                  &fields);
              // port_value
              if (envoy_api_v2_core_SocketAddress_has_port_value(
                      socket_address)) {
                fields.emplace_back(
                    absl::StrCat("          port_value: ",
                                 envoy_api_v2_core_SocketAddress_port_value(
                                     socket_address)));
              } else {
                fields.emplace_back("        <non-numeric port>");
              }
              fields.emplace_back("        }");
            } else {
              fields.emplace_back("        <non-socket address>");
            }
            fields.emplace_back("      }");
          }
          fields.emplace_back("    }");
        }
        fields.emplace_back("  }");
      }
      // load_balancing_weight
      const google_protobuf_UInt32Value* lb_weight =
          envoy_api_v2_endpoint_LocalityLbEndpoints_load_balancing_weight(
              locality_endpoint);
      if (lb_weight != nullptr) {
        fields.emplace_back(
            absl::StrCat("  load_balancing_weight { value: ",
                         google_protobuf_UInt32Value_value(lb_weight), " }"));
      }
      // priority
      uint32_t priority =
          envoy_api_v2_endpoint_LocalityLbEndpoints_priority(locality_endpoint);
      if (priority > 0) {
        fields.emplace_back(absl::StrCat("  priority: ", priority));
      }
      fields.emplace_back("}");
    }
    // policy
    const envoy_api_v2_ClusterLoadAssignment_Policy* policy =
        envoy_api_v2_ClusterLoadAssignment_policy(cla);
    if (policy != nullptr) {
      fields.emplace_back("policy {");
      // drop_overloads
      size_t num_drop_overloads;
      const envoy_api_v2_ClusterLoadAssignment_Policy_DropOverload* const*
          drop_overloads =
              envoy_api_v2_ClusterLoadAssignment_Policy_drop_overloads(
                  policy, &num_drop_overloads);
      for (size_t i = 0; i < num_drop_overloads; ++i) {
        auto* drop_overload = drop_overloads[i];
        fields.emplace_back("  drop_overloads {");
        // category
        AddStringField(
            "    category",
            envoy_api_v2_ClusterLoadAssignment_Policy_DropOverload_category(
                drop_overload),
            &fields);
        // drop_percentage
        const auto* drop_percentage =
            envoy_api_v2_ClusterLoadAssignment_Policy_DropOverload_drop_percentage(
                drop_overload);
        if (drop_percentage != nullptr) {
          fields.emplace_back("    drop_percentage {");
          fields.emplace_back(absl::StrCat(
              "      numerator: ",
              envoy_type_FractionalPercent_numerator(drop_percentage)));
          fields.emplace_back(absl::StrCat(
              "      denominator: ",
              envoy_type_FractionalPercent_denominator(drop_percentage)));
          fields.emplace_back("    }");
        }
        fields.emplace_back("  }");
      }
      // overprovisioning_factor
      fields.emplace_back("}");
    }
    gpr_log(GPR_DEBUG, "[xds_client %p] ClusterLoadAssignment: %s", client,
            absl::StrJoin(fields, "\n").c_str());
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

grpc_error* RouteConfigParse(
    XdsClient* client, TraceFlag* tracer,
    const envoy_api_v2_RouteConfiguration* route_config,
    const std::string& expected_server_name, const bool xds_routing_enabled,
    XdsApi::RdsUpdate* rds_update) {
  MaybeLogRouteConfiguration(client, tracer, route_config);
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
  size_t start_index = xds_routing_enabled ? 0 : size - 1;
  for (size_t i = start_index; i < size; ++i) {
    const envoy_api_v2_route_Route* route = routes[i];
    const envoy_api_v2_route_RouteMatch* match =
        envoy_api_v2_route_Route_match(route);
    XdsApi::RdsRoute rds_route;
    if (envoy_api_v2_route_RouteMatch_has_prefix(match)) {
      upb_strview prefix = envoy_api_v2_route_RouteMatch_prefix(match);
      // Empty prefix "" is accepted.
      if (prefix.size > 0) {
        // Prefix "/" is accepted.
        if (prefix.data[0] != '/') {
          return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "Prefix does not start with a /");
        }
        if (prefix.size > 1) {
          std::vector<absl::string_view> prefix_elements = absl::StrSplit(
              absl::string_view(prefix.data, prefix.size).substr(1),
              absl::MaxSplits('/', 1));
          if (prefix_elements.size() != 2) {
            return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Prefix not in the required format of /service/");
          } else if (!prefix_elements[1].empty()) {
            return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Prefix does not end with a /");
          } else if (prefix_elements[0].empty()) {
            return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Prefix contains empty service name");
          }
          rds_route.service = std::string(prefix_elements[0]);
        }
      }
    } else if (envoy_api_v2_route_RouteMatch_has_path(match)) {
      upb_strview path = envoy_api_v2_route_RouteMatch_path(match);
      if (path.size == 0) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Path if set cannot be empty");
      }
      if (path.data[0] != '/') {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Path does not start with a /");
      }
      std::vector<absl::string_view> path_elements = absl::StrSplit(
          absl::string_view(path.data, path.size).substr(1), '/');
      if (path_elements.size() != 2) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Path not in the required format of /service/method");
      } else if (path_elements[0].empty()) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Path contains empty service name");
      } else if (path_elements[1].empty()) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Path contains empty method name");
      }
      rds_route.service = std::string(path_elements[0]);
      rds_route.method = std::string(path_elements[1]);
    } else {
      // TODO(donnadionne): We may change this behavior once we decide how to
      // handle unsupported fields.
      continue;
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
    const upb_strview action =
        envoy_api_v2_route_RouteAction_cluster(route_action);
    if (action.size == 0) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "RouteAction contains empty cluster.");
    }
    rds_route.cluster_name = std::string(action.data, action.size);
    rds_update->routes.emplace_back(std::move(rds_route));
  }
  if (rds_update->routes.empty()) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No valid routes specified.");
  } else {
    if (!rds_update->routes.back().service.empty() ||
        !rds_update->routes.back().method.empty()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Default route must have empty service and method");
    }
  }
  return GRPC_ERROR_NONE;
}

grpc_error* LdsResponseParse(XdsClient* client, TraceFlag* tracer,
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
      grpc_error* error =
          RouteConfigParse(client, tracer, route_config, expected_server_name,
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
    // Get the route_config_name.
    const envoy_config_filter_network_http_connection_manager_v2_Rds* rds =
        envoy_config_filter_network_http_connection_manager_v2_HttpConnectionManager_rds(
            http_connection_manager);
    const upb_strview route_config_name =
        envoy_config_filter_network_http_connection_manager_v2_Rds_route_config_name(
            rds);
    lds_update->emplace();
    (*lds_update)->route_config_name =
        std::string(route_config_name.data, route_config_name.size);
    return GRPC_ERROR_NONE;
  }
  return GRPC_ERROR_NONE;
}

grpc_error* RdsResponseParse(XdsClient* client, TraceFlag* tracer,
                             const envoy_api_v2_DiscoveryResponse* response,
                             const std::string& expected_server_name,
                             const std::string& expected_route_config_name,
                             const bool xds_routing_enabled,
                             absl::optional<XdsApi::RdsUpdate>* rds_update,
                             upb_arena* arena) {
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
    const upb_strview name = envoy_api_v2_RouteConfiguration_name(route_config);
    const upb_strview expected_name =
        upb_strview_makez(expected_route_config_name.c_str());
    if (!upb_strview_eql(name, expected_name)) continue;
    // Parse the route_config.
    XdsApi::RdsUpdate local_rds_update;
    grpc_error* error =
        RouteConfigParse(client, tracer, route_config, expected_server_name,
                         xds_routing_enabled, &local_rds_update);
    if (error != GRPC_ERROR_NONE) return error;
    rds_update->emplace(std::move(local_rds_update));
    return GRPC_ERROR_NONE;
  }
  return GRPC_ERROR_NONE;
}

grpc_error* CdsResponseParse(
    XdsClient* client, TraceFlag* tracer,
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
    MaybeLogCluster(client, tracer, cluster);
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
            "LRS ConfigSource is not self.");
      }
      cds_update.lrs_load_reporting_server_name.emplace("");
    }
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
  drop_config->AddCategory(std::string(category.data, category.size),
                           numerator);
  return GRPC_ERROR_NONE;
}

grpc_error* EdsResponseParse(
    XdsClient* client, TraceFlag* tracer,
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
    MaybeLogClusterLoadAssignment(client, tracer, cluster_load_assignment);
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
    eds_update_map->emplace(std::string(cluster_name.data, cluster_name.size),
                            std::move(eds_update));
  }
  return GRPC_ERROR_NONE;
}

}  // namespace

grpc_error* XdsApi::ParseAdsResponse(
    const grpc_slice& encoded_response, const std::string& expected_server_name,
    const std::string& expected_route_config_name,
    const bool xds_routing_enabled,
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
  MaybeLogDiscoveryResponse(client_, tracer_, response);
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
    return LdsResponseParse(client_, tracer_, response, expected_server_name,
                            xds_routing_enabled, lds_update, arena.ptr());
  } else if (*type_url == kRdsTypeUrl) {
    return RdsResponseParse(client_, tracer_, response, expected_server_name,
                            expected_route_config_name, xds_routing_enabled,
                            rds_update, arena.ptr());
  } else if (*type_url == kCdsTypeUrl) {
    return CdsResponseParse(client_, tracer_, response, expected_cluster_names,
                            cds_update_map, arena.ptr());
  } else if (*type_url == kEdsTypeUrl) {
    return EdsResponseParse(client_, tracer_, response,
                            expected_eds_service_names, eds_update_map,
                            arena.ptr());
  } else {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Unsupported ADS resource type.");
  }
}

namespace {

void MaybeLogLrsRequest(
    XdsClient* client, TraceFlag* tracer,
    const envoy_service_load_stats_v2_LoadStatsRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED(*tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    // TODO(roth): When we can upgrade upb, use upb textformat code to dump
    // the raw proto instead of doing this manually.
    std::vector<std::string> fields;
    // node
    const auto* node =
        envoy_service_load_stats_v2_LoadStatsRequest_node(request);
    if (node != nullptr) {
      AddNodeLogFields(node, &fields);
    }
    // cluster_stats
    size_t num_cluster_stats;
    const struct envoy_api_v2_endpoint_ClusterStats* const* cluster_stats =
        envoy_service_load_stats_v2_LoadStatsRequest_cluster_stats(
            request, &num_cluster_stats);
    for (size_t i = 0; i < num_cluster_stats; ++i) {
      const auto* cluster_stat = cluster_stats[i];
      fields.emplace_back("cluster_stats {");
      // cluster_name
      AddStringField(
          "  cluster_name",
          envoy_api_v2_endpoint_ClusterStats_cluster_name(cluster_stat),
          &fields);
      // cluster_service_name
      AddStringField(
          "  cluster_service_name",
          envoy_api_v2_endpoint_ClusterStats_cluster_service_name(cluster_stat),
          &fields);
      // upstream_locality_stats
      size_t num_stats;
      const envoy_api_v2_endpoint_UpstreamLocalityStats* const* stats =
          envoy_api_v2_endpoint_ClusterStats_upstream_locality_stats(
              cluster_stat, &num_stats);
      for (size_t j = 0; j < num_stats; ++j) {
        const auto* stat = stats[j];
        fields.emplace_back("  upstream_locality_stats {");
        // locality
        const auto* locality =
            envoy_api_v2_endpoint_UpstreamLocalityStats_locality(stat);
        if (locality != nullptr) {
          fields.emplace_back("    locality {");
          AddLocalityField(3, locality, &fields);
          fields.emplace_back("    }");
        }
        // total_successful_requests
        fields.emplace_back(absl::StrCat(
            "    total_successful_requests: ",
            envoy_api_v2_endpoint_UpstreamLocalityStats_total_successful_requests(
                stat)));
        // total_requests_in_progress
        fields.emplace_back(absl::StrCat(
            "    total_requests_in_progress: ",
            envoy_api_v2_endpoint_UpstreamLocalityStats_total_requests_in_progress(
                stat)));
        // total_error_requests
        fields.emplace_back(absl::StrCat(
            "    total_error_requests: ",
            envoy_api_v2_endpoint_UpstreamLocalityStats_total_error_requests(
                stat)));
        // total_issued_requests
        fields.emplace_back(absl::StrCat(
            "    total_issued_requests: ",
            envoy_api_v2_endpoint_UpstreamLocalityStats_total_issued_requests(
                stat)));
        fields.emplace_back("  }");
      }
      // total_dropped_requests
      fields.emplace_back(absl::StrCat(
          "  total_dropped_requests: ",
          envoy_api_v2_endpoint_ClusterStats_total_dropped_requests(
              cluster_stat)));
      // dropped_requests
      size_t num_drops;
      const envoy_api_v2_endpoint_ClusterStats_DroppedRequests* const* drops =
          envoy_api_v2_endpoint_ClusterStats_dropped_requests(cluster_stat,
                                                              &num_drops);
      for (size_t j = 0; j < num_drops; ++j) {
        const auto* drop = drops[j];
        fields.emplace_back("  dropped_requests {");
        // category
        AddStringField(
            "    category",
            envoy_api_v2_endpoint_ClusterStats_DroppedRequests_category(drop),
            &fields);
        // dropped_count
        fields.emplace_back(absl::StrCat(
            "    dropped_count: ",
            envoy_api_v2_endpoint_ClusterStats_DroppedRequests_dropped_count(
                drop)));
        fields.emplace_back("  }");
      }
      // load_report_interval
      const auto* load_report_interval =
          envoy_api_v2_endpoint_ClusterStats_load_report_interval(cluster_stat);
      if (load_report_interval != nullptr) {
        fields.emplace_back("  load_report_interval {");
        fields.emplace_back(absl::StrCat(
            "    seconds: ",
            google_protobuf_Duration_seconds(load_report_interval)));
        fields.emplace_back(
            absl::StrCat("    nanos: ",
                         google_protobuf_Duration_nanos(load_report_interval)));
        fields.emplace_back("  }");
      }
      fields.emplace_back("}");
    }
    gpr_log(GPR_DEBUG, "[xds_client %p] constructed LRS request: %s", client,
            absl::StrJoin(fields, "\n").c_str());
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
  MaybeLogLrsRequest(client_, tracer_, request);
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
  MaybeLogLrsRequest(client_, tracer_, request);
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
