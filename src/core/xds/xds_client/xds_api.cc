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

#include "src/core/xds/xds_client/xds_api.h"

#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include <stdint.h>
#include <stdlib.h>

#include <set>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/endpoint/v3/load_report.upb.h"
#include "envoy/service/discovery/v3/discovery.upb.h"
#include "envoy/service/discovery/v3/discovery.upbdefs.h"
#include "envoy/service/load_stats/v3/lrs.upb.h"
#include "envoy/service/load_stats/v3/lrs.upbdefs.h"
#include "envoy/service/status/v3/csds.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "google/rpc/status.upb.h"
#include "src/core/util/json/json.h"
#include "src/core/util/upb_utils.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.hpp"
#include "upb/reflection/def.h"
#include "upb/text/encode.h"

// IWYU pragma: no_include "upb/msg_internal.h"

namespace grpc_core {

XdsApi::XdsApi(XdsClient* client, TraceFlag* tracer,
               const XdsBootstrap::Node* node, upb::DefPool* def_pool,
               std::string user_agent_name, std::string user_agent_version)
    : client_(client),
      tracer_(tracer),
      node_(node),
      def_pool_(def_pool),
      user_agent_name_(std::move(user_agent_name)),
      user_agent_version_(std::move(user_agent_version)) {}

namespace {

struct XdsApiContext {
  XdsClient* client;
  TraceFlag* tracer;
  upb_DefPool* def_pool;
  upb_Arena* arena;
};

void PopulateMetadataValue(google_protobuf_Value* value_pb, const Json& value,
                           upb_Arena* arena);

void PopulateListValue(google_protobuf_ListValue* list_value,
                       const Json::Array& values, upb_Arena* arena) {
  for (const auto& value : values) {
    auto* value_pb = google_protobuf_ListValue_add_values(list_value, arena);
    PopulateMetadataValue(value_pb, value, arena);
  }
}

void PopulateMetadata(google_protobuf_Struct* metadata_pb,
                      const Json::Object& metadata, upb_Arena* arena) {
  for (const auto& p : metadata) {
    google_protobuf_Value* value = google_protobuf_Value_new(arena);
    PopulateMetadataValue(value, p.second, arena);
    google_protobuf_Struct_fields_set(
        metadata_pb, StdStringToUpbString(p.first), value, arena);
  }
}

void PopulateMetadataValue(google_protobuf_Value* value_pb, const Json& value,
                           upb_Arena* arena) {
  switch (value.type()) {
    case Json::Type::kNull:
      google_protobuf_Value_set_null_value(value_pb, 0);
      break;
    case Json::Type::kNumber:
      google_protobuf_Value_set_number_value(
          value_pb, strtod(value.string().c_str(), nullptr));
      break;
    case Json::Type::kString:
      google_protobuf_Value_set_string_value(
          value_pb, StdStringToUpbString(value.string()));
      break;
    case Json::Type::kBoolean:
      google_protobuf_Value_set_bool_value(value_pb, value.boolean());
      break;
    case Json::Type::kObject: {
      google_protobuf_Struct* struct_value =
          google_protobuf_Value_mutable_struct_value(value_pb, arena);
      PopulateMetadata(struct_value, value.object(), arena);
      break;
    }
    case Json::Type::kArray: {
      google_protobuf_ListValue* list_value =
          google_protobuf_Value_mutable_list_value(value_pb, arena);
      PopulateListValue(list_value, value.array(), arena);
      break;
    }
  }
}

void MaybeLogDiscoveryRequest(
    const XdsApiContext& context,
    const envoy_service_discovery_v3_DiscoveryRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*context.tracer) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_service_discovery_v3_DiscoveryRequest_getmsgdef(context.def_pool);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(request), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << context.client
            << "] constructed ADS request: " << buf;
  }
}

std::string SerializeDiscoveryRequest(
    const XdsApiContext& context,
    envoy_service_discovery_v3_DiscoveryRequest* request) {
  size_t output_length;
  char* output = envoy_service_discovery_v3_DiscoveryRequest_serialize(
      request, context.arena, &output_length);
  return std::string(output, output_length);
}

}  // namespace

void XdsApi::PopulateNode(envoy_config_core_v3_Node* node_msg,
                          upb_Arena* arena) {
  PopulateXdsNode(node_, user_agent_name_, user_agent_version_, node_msg,
                  arena);
}

void PopulateXdsNode(const XdsBootstrap::Node* node,
                     absl::string_view user_agent_name,
                     absl::string_view user_agent_version,
                     envoy_config_core_v3_Node* node_msg, upb_Arena* arena) {
  if (node != nullptr) {
    if (!node->id().empty()) {
      envoy_config_core_v3_Node_set_id(node_msg,
                                       StdStringToUpbString(node->id()));
    }
    if (!node->cluster().empty()) {
      envoy_config_core_v3_Node_set_cluster(
          node_msg, StdStringToUpbString(node->cluster()));
    }
    if (!node->metadata().empty()) {
      google_protobuf_Struct* metadata =
          envoy_config_core_v3_Node_mutable_metadata(node_msg, arena);
      PopulateMetadata(metadata, node->metadata(), arena);
    }
    if (!node->locality_region().empty() || !node->locality_zone().empty() ||
        !node->locality_sub_zone().empty()) {
      envoy_config_core_v3_Locality* locality =
          envoy_config_core_v3_Node_mutable_locality(node_msg, arena);
      if (!node->locality_region().empty()) {
        envoy_config_core_v3_Locality_set_region(
            locality, StdStringToUpbString(node->locality_region()));
      }
      if (!node->locality_zone().empty()) {
        envoy_config_core_v3_Locality_set_zone(
            locality, StdStringToUpbString(node->locality_zone()));
      }
      if (!node->locality_sub_zone().empty()) {
        envoy_config_core_v3_Locality_set_sub_zone(
            locality, StdStringToUpbString(node->locality_sub_zone()));
      }
    }
  }
  envoy_config_core_v3_Node_set_user_agent_name(
      node_msg, StdStringToUpbString(user_agent_name));
  envoy_config_core_v3_Node_set_user_agent_version(
      node_msg, StdStringToUpbString(user_agent_version));
  envoy_config_core_v3_Node_add_client_features(
      node_msg,
      upb_StringView_FromString("envoy.lb.does_not_support_overprovisioning"),
      arena);
}

std::string XdsApi::CreateAdsRequest(
    absl::string_view type_url, absl::string_view version,
    absl::string_view nonce, const std::vector<std::string>& resource_names,
    absl::Status status, bool populate_node) {
  upb::Arena arena;
  const XdsApiContext context = {client_, tracer_, def_pool_->ptr(),
                                 arena.ptr()};
  // Create a request.
  envoy_service_discovery_v3_DiscoveryRequest* request =
      envoy_service_discovery_v3_DiscoveryRequest_new(arena.ptr());
  // Set type_url.
  std::string type_url_str = absl::StrCat("type.googleapis.com/", type_url);
  envoy_service_discovery_v3_DiscoveryRequest_set_type_url(
      request, StdStringToUpbString(type_url_str));
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
  if (!status.ok()) {
    google_rpc_Status* error_detail =
        envoy_service_discovery_v3_DiscoveryRequest_mutable_error_detail(
            request, arena.ptr());
    // Hard-code INVALID_ARGUMENT as the status code.
    // TODO(roth): If at some point we decide we care about this value,
    // we could attach a status code to the individual errors where we
    // generate them in the parsing code, and then use that here.
    google_rpc_Status_set_code(error_detail, GRPC_STATUS_INVALID_ARGUMENT);
    // Error description comes from the status that was passed in.
    error_string_storage = std::string(status.message());
    upb_StringView error_description =
        StdStringToUpbString(error_string_storage);
    google_rpc_Status_set_message(error_detail, error_description);
  }
  // Populate node.
  if (populate_node) {
    envoy_config_core_v3_Node* node_msg =
        envoy_service_discovery_v3_DiscoveryRequest_mutable_node(request,
                                                                 arena.ptr());
    PopulateNode(node_msg, arena.ptr());
    envoy_config_core_v3_Node_add_client_features(
        node_msg, upb_StringView_FromString("xds.config.resource-in-sotw"),
        context.arena);
  }
  // Add resource_names.
  for (const std::string& resource_name : resource_names) {
    envoy_service_discovery_v3_DiscoveryRequest_add_resource_names(
        request, StdStringToUpbString(resource_name), arena.ptr());
  }
  MaybeLogDiscoveryRequest(context, request);
  return SerializeDiscoveryRequest(context, request);
}

namespace {

void MaybeLogDiscoveryResponse(
    const XdsApiContext& context,
    const envoy_service_discovery_v3_DiscoveryResponse* response) {
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*context.tracer) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_service_discovery_v3_DiscoveryResponse_getmsgdef(
            context.def_pool);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(response), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << context.client
            << "] received response: " << buf;
  }
}

}  // namespace

absl::Status XdsApi::ParseAdsResponse(absl::string_view encoded_response,
                                      AdsResponseParserInterface* parser) {
  upb::Arena arena;
  const XdsApiContext context = {client_, tracer_, def_pool_->ptr(),
                                 arena.ptr()};
  // Decode the response.
  const envoy_service_discovery_v3_DiscoveryResponse* response =
      envoy_service_discovery_v3_DiscoveryResponse_parse(
          encoded_response.data(), encoded_response.size(), arena.ptr());
  // If decoding fails, report a fatal error and return.
  if (response == nullptr) {
    return absl::InvalidArgumentError("Can't decode DiscoveryResponse.");
  }
  MaybeLogDiscoveryResponse(context, response);
  // Report the type_url, version, nonce, and number of resources to the parser.
  AdsResponseParserInterface::AdsResponseFields fields;
  fields.type_url = std::string(absl::StripPrefix(
      UpbStringToAbsl(
          envoy_service_discovery_v3_DiscoveryResponse_type_url(response)),
      "type.googleapis.com/"));
  fields.version = UpbStringToStdString(
      envoy_service_discovery_v3_DiscoveryResponse_version_info(response));
  fields.nonce = UpbStringToStdString(
      envoy_service_discovery_v3_DiscoveryResponse_nonce(response));
  size_t num_resources;
  const google_protobuf_Any* const* resources =
      envoy_service_discovery_v3_DiscoveryResponse_resources(response,
                                                             &num_resources);
  fields.num_resources = num_resources;
  absl::Status status = parser->ProcessAdsResponseFields(std::move(fields));
  if (!status.ok()) return status;
  // Process each resource.
  for (size_t i = 0; i < num_resources; ++i) {
    absl::string_view type_url = absl::StripPrefix(
        UpbStringToAbsl(google_protobuf_Any_type_url(resources[i])),
        "type.googleapis.com/");
    absl::string_view serialized_resource =
        UpbStringToAbsl(google_protobuf_Any_value(resources[i]));
    // Unwrap Resource messages, if so wrapped.
    absl::string_view resource_name;
    if (type_url == "envoy.service.discovery.v3.Resource") {
      const auto* resource_wrapper = envoy_service_discovery_v3_Resource_parse(
          serialized_resource.data(), serialized_resource.size(), arena.ptr());
      if (resource_wrapper == nullptr) {
        parser->ResourceWrapperParsingFailed(
            i, "Can't decode Resource proto wrapper");
        continue;
      }
      const auto* resource =
          envoy_service_discovery_v3_Resource_resource(resource_wrapper);
      if (resource == nullptr) {
        parser->ResourceWrapperParsingFailed(
            i, "No resource present in Resource proto wrapper");
        continue;
      }
      type_url = absl::StripPrefix(
          UpbStringToAbsl(google_protobuf_Any_type_url(resource)),
          "type.googleapis.com/");
      serialized_resource =
          UpbStringToAbsl(google_protobuf_Any_value(resource));
      resource_name = UpbStringToAbsl(
          envoy_service_discovery_v3_Resource_name(resource_wrapper));
    }
    parser->ParseResource(context.arena, i, type_url, resource_name,
                          serialized_resource);
  }
  return absl::OkStatus();
}

}  // namespace grpc_core
