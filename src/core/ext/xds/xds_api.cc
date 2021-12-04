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

#include "src/core/ext/xds/xds_api.h"

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
#include "upb/text_encode.h"
#include "upb/upb.h"
#include "upb/upb.hpp"
#include "xds/type/v3/typed_struct.upb.h"

#include <grpc/impl/codegen/log.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/xds/upb_utils.h"
#include "src/core/ext/xds/xds_cluster.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_endpoint.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/ext/xds/xds_routing.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/slice/slice_utils.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

//
// XdsApi
//

// TODO(roth): All constants and functions for individual resource types
// should be merged into the XdsResourceType abstraction.
const char* XdsApi::kLdsTypeUrl = "envoy.config.listener.v3.Listener";
const char* XdsApi::kRdsTypeUrl = "envoy.config.route.v3.RouteConfiguration";
const char* XdsApi::kCdsTypeUrl = "envoy.config.cluster.v3.Cluster";
const char* XdsApi::kEdsTypeUrl =
    "envoy.config.endpoint.v3.ClusterLoadAssignment";

namespace {

const char* kLdsV2TypeUrl = "envoy.api.v2.Listener";
const char* kRdsV2TypeUrl = "envoy.api.v2.RouteConfiguration";
const char* kCdsV2TypeUrl = "envoy.api.v2.Cluster";
const char* kEdsV2TypeUrl = "envoy.api.v2.ClusterLoadAssignment";

bool IsLdsInternal(absl::string_view type_url, bool* is_v2 = nullptr) {
  if (type_url == XdsApi::kLdsTypeUrl) return true;
  if (type_url == kLdsV2TypeUrl) {
    if (is_v2 != nullptr) *is_v2 = true;
    return true;
  }
  return false;
}

bool IsRdsInternal(absl::string_view type_url, bool* /*is_v2*/ = nullptr) {
  return type_url == XdsApi::kRdsTypeUrl || type_url == kRdsV2TypeUrl;
}

bool IsCdsInternal(absl::string_view type_url, bool* /*is_v2*/ = nullptr) {
  return type_url == XdsApi::kCdsTypeUrl || type_url == kCdsV2TypeUrl;
}

bool IsEdsInternal(absl::string_view type_url, bool* /*is_v2*/ = nullptr) {
  return type_url == XdsApi::kEdsTypeUrl || type_url == kEdsV2TypeUrl;
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

absl::StatusOr<XdsApi::ResourceName> ParseResourceNameInternal(
    absl::string_view name,
    std::function<bool(absl::string_view, bool*)> is_expected_type) {
  // Old-style names use the empty string for authority.
  // authority is prefixed with "old:" to indicate that it's an old-style name.
  if (!absl::StartsWith(name, "xdstp:")) {
    return XdsApi::ResourceName{"old:", std::string(name)};
  }
  // New style name.  Parse URI.
  auto uri = URI::Parse(name);
  if (!uri.ok()) return uri.status();
  // Split the resource type off of the path to get the id.
  std::pair<absl::string_view, absl::string_view> path_parts =
      absl::StrSplit(uri->path(), absl::MaxSplits('/', 1));
  if (!is_expected_type(path_parts.first, nullptr)) {
    return absl::InvalidArgumentError(
        "xdstp URI path must indicate valid xDS resource type");
  }
  std::vector<std::pair<absl::string_view, absl::string_view>> query_parameters(
      uri->query_parameter_map().begin(), uri->query_parameter_map().end());
  std::sort(query_parameters.begin(), query_parameters.end());
  return XdsApi::ResourceName{
      absl::StrCat("xdstp:", uri->authority()),
      absl::StrCat(
          path_parts.second, (query_parameters.empty() ? "?" : ""),
          absl::StrJoin(query_parameters, "&", absl::PairFormatter("=")))};
}

}  // namespace

// If gRPC is built with -DGRPC_XDS_USER_AGENT_NAME_SUFFIX="...", that string
// will be appended to the user agent name reported to the xDS server.
#ifdef GRPC_XDS_USER_AGENT_NAME_SUFFIX
#define GRPC_XDS_USER_AGENT_NAME_SUFFIX_STRING \
  " " GRPC_XDS_USER_AGENT_NAME_SUFFIX
#else
#define GRPC_XDS_USER_AGENT_NAME_SUFFIX_STRING ""
#endif

// If gRPC is built with -DGRPC_XDS_USER_AGENT_VERSION_SUFFIX="...", that string
// will be appended to the user agent version reported to the xDS server.
#ifdef GRPC_XDS_USER_AGENT_VERSION_SUFFIX
#define GRPC_XDS_USER_AGENT_VERSION_SUFFIX_STRING \
  " " GRPC_XDS_USER_AGENT_VERSION_SUFFIX
#else
#define GRPC_XDS_USER_AGENT_VERSION_SUFFIX_STRING ""
#endif

XdsApi::XdsApi(XdsClient* client, TraceFlag* tracer,
               const XdsBootstrap::Node* node,
               const CertificateProviderStore::PluginDefinitionMap*
                   certificate_provider_definition_map)
    : client_(client),
      tracer_(tracer),
      node_(node),
      certificate_provider_definition_map_(certificate_provider_definition_map),
      build_version_(absl::StrCat("gRPC C-core ", GPR_PLATFORM_STRING, " ",
                                  grpc_version_string(),
                                  GRPC_XDS_USER_AGENT_NAME_SUFFIX_STRING,
                                  GRPC_XDS_USER_AGENT_VERSION_SUFFIX_STRING)),
      user_agent_name_(absl::StrCat("gRPC C-core ", GPR_PLATFORM_STRING,
                                    GRPC_XDS_USER_AGENT_NAME_SUFFIX_STRING)),
      user_agent_version_(
          absl::StrCat("C-core ", grpc_version_string(),
                       GRPC_XDS_USER_AGENT_NAME_SUFFIX_STRING,
                       GRPC_XDS_USER_AGENT_VERSION_SUFFIX_STRING)) {
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

bool XdsApi::IsLds(absl::string_view type_url) {
  return IsLdsInternal(type_url);
}

bool XdsApi::IsRds(absl::string_view type_url) {
  return IsRdsInternal(type_url);
}

bool XdsApi::IsCds(absl::string_view type_url) {
  return IsCdsInternal(type_url);
}

bool XdsApi::IsEds(absl::string_view type_url) {
  return IsEdsInternal(type_url);
}

absl::StatusOr<XdsApi::ResourceName> XdsApi::ParseResourceName(
    absl::string_view name, bool (*is_expected_type)(absl::string_view)) {
  return ParseResourceNameInternal(
      name, [is_expected_type](absl::string_view type, bool*) {
        return is_expected_type(type);
      });
}

std::string XdsApi::ConstructFullResourceName(absl::string_view authority,
                                              absl::string_view resource_type,
                                              absl::string_view name) {
  if (absl::ConsumePrefix(&authority, "xdstp:")) {
    return absl::StrCat("xdstp://", authority, "/", resource_type, "/", name);
  } else {
    return std::string(absl::StripPrefix(name, "old:"));
  }
}

namespace {

void PopulateMetadataValue(const XdsEncodingContext& context,
                           google_protobuf_Value* value_pb, const Json& value);

void PopulateListValue(const XdsEncodingContext& context,
                       google_protobuf_ListValue* list_value,
                       const Json::Array& values) {
  for (const auto& value : values) {
    auto* value_pb =
        google_protobuf_ListValue_add_values(list_value, context.arena);
    PopulateMetadataValue(context, value_pb, value);
  }
}

void PopulateMetadata(const XdsEncodingContext& context,
                      google_protobuf_Struct* metadata_pb,
                      const Json::Object& metadata) {
  for (const auto& p : metadata) {
    google_protobuf_Value* value = google_protobuf_Value_new(context.arena);
    PopulateMetadataValue(context, value, p.second);
    google_protobuf_Struct_fields_set(
        metadata_pb, StdStringToUpbString(p.first), value, context.arena);
  }
}

void PopulateMetadataValue(const XdsEncodingContext& context,
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

void PopulateBuildVersion(const XdsEncodingContext& context,
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

void PopulateNode(const XdsEncodingContext& context,
                  const XdsBootstrap::Node* node,
                  const std::string& build_version,
                  const std::string& user_agent_name,
                  const std::string& user_agent_version,
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
      node_msg, StdStringToUpbString(user_agent_version));
  envoy_config_core_v3_Node_add_client_features(
      node_msg, upb_strview_makez("envoy.lb.does_not_support_overprovisioning"),
      context.arena);
}

void MaybeLogDiscoveryRequest(
    const XdsEncodingContext& context,
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
    const XdsEncodingContext& context,
    envoy_service_discovery_v3_DiscoveryRequest* request) {
  size_t output_length;
  char* output = envoy_service_discovery_v3_DiscoveryRequest_serialize(
      request, context.arena, &output_length);
  return grpc_slice_from_copied_buffer(output, output_length);
}

}  // namespace

grpc_slice XdsApi::CreateAdsRequest(
    const XdsBootstrap::XdsServer& server, const std::string& type_url,
    const std::map<absl::string_view /*authority*/,
                   std::set<absl::string_view /*name*/>>& resource_names,
    const std::string& version, const std::string& nonce,
    grpc_error_handle error, bool populate_node) {
  upb::Arena arena;
  const XdsEncodingContext context = {client_,
                                      tracer_,
                                      symtab_.ptr(),
                                      arena.ptr(),
                                      server.ShouldUseV3(),
                                      certificate_provider_definition_map_};
  // Create a request.
  envoy_service_discovery_v3_DiscoveryRequest* request =
      envoy_service_discovery_v3_DiscoveryRequest_new(arena.ptr());
  // Set type_url.
  absl::string_view real_type_url =
      TypeUrlExternalToInternal(server.ShouldUseV3(), type_url);
  std::string real_type_url_str =
      absl::StrCat("type.googleapis.com/", real_type_url);
  envoy_service_discovery_v3_DiscoveryRequest_set_type_url(
      request, StdStringToUpbString(real_type_url_str));
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
    PopulateNode(context, node_, build_version_, user_agent_name_,
                 user_agent_version_, node_msg);
  }
  // A vector for temporary local storage of resource name strings.
  std::vector<std::string> resource_name_storage;
  // Make sure the vector is sized right up-front, so that reallocations
  // don't move the strings out from under the upb proto object that
  // points to them.
  size_t size = 0;
  for (const auto& p : resource_names) {
    size += p.second.size();
  }
  resource_name_storage.reserve(size);
  // Add resource_names.
  for (const auto& a : resource_names) {
    absl::string_view authority = a.first;
    for (const auto& p : a.second) {
      absl::string_view resource_id = p;
      resource_name_storage.push_back(
          ConstructFullResourceName(authority, real_type_url, resource_id));
      envoy_service_discovery_v3_DiscoveryRequest_add_resource_names(
          request, StdStringToUpbString(resource_name_storage.back()),
          arena.ptr());
    }
  }
  MaybeLogDiscoveryRequest(context, request);
  return SerializeDiscoveryRequest(context, request);
}

namespace {

void MaybeLogDiscoveryResponse(
    const XdsEncodingContext& context,
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

grpc_error_handle AdsResourceParse(
    const XdsEncodingContext& context, XdsResourceType* type, size_t idx,
    const google_protobuf_Any* resource_any,
    const std::map<absl::string_view /*authority*/,
                   std::set<absl::string_view /*name*/>>&
        subscribed_resource_names,
    std::function<grpc_error_handle(
        absl::string_view, XdsApi::ResourceName,
        std::unique_ptr<XdsResourceType::ResourceData>, std::string)>
        add_result_func,
    std::set<XdsApi::ResourceName>* resource_names_failed) {
  // Check the type_url of the resource.
  absl::string_view type_url = absl::StripPrefix(
      UpbStringToAbsl(google_protobuf_Any_type_url(resource_any)),
      "type.googleapis.com/");
  bool is_v2 = false;
  if (!type->IsType(type_url, &is_v2)) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("resource index ", idx, ": found resource type ", type_url,
                     " in response for type ", type->type_url()));
  }
  // Parse the resource.
  absl::string_view serialized_resource =
      UpbStringToAbsl(google_protobuf_Any_value(resource_any));
  absl::StatusOr<XdsResourceType::DecodeResult> result =
      type->Decode(context, serialized_resource, is_v2);
  if (!result.ok()) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("resource index ", idx, ": ", result.status().ToString()));
  }
  // Check the resource name.
  auto resource_name = ParseResourceNameInternal(
      result->name, [type](absl::string_view type_url, bool* is_v2) {
        return type->IsType(type_url, is_v2);
      });
  if (!resource_name.ok()) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
        "resource index ", idx, ": Cannot parse xDS resource name \"",
        result->name, "\""));
  }
  // Ignore unexpected names.
  auto iter = subscribed_resource_names.find(resource_name->authority);
  if (iter == subscribed_resource_names.end() ||
      iter->second.find(resource_name->id) == iter->second.end()) {
    return GRPC_ERROR_NONE;
  }
  // Check that resource was valid.
  if (!result->resource.ok()) {
    resource_names_failed->insert(*resource_name);
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
        "resource index ", idx, ": ", result->name,
        ": validation error: ", result->resource.status().ToString()));
  }
  // Add result.
  grpc_error_handle error = add_result_func(result->name, *resource_name,
                                            std::move(*result->resource),
                                            std::string(serialized_resource));
  if (error != GRPC_ERROR_NONE) {
    resource_names_failed->insert(*resource_name);
    return grpc_error_add_child(
        GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
            "resource index ", idx, ": ", result->name, ": validation error")),
        error);
  }
  return GRPC_ERROR_NONE;
}

template <typename UpdateMap, typename ResourceTypeData>
grpc_error_handle AddResult(
    UpdateMap* update_map, absl::string_view resource_name_string,
    XdsApi::ResourceName resource_name,
    std::unique_ptr<XdsResourceType::ResourceData> resource,
    std::string serialized_resource) {
  // Reject duplicate names.
  if (update_map->find(resource_name) != update_map->end()) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("duplicate resource name \"", resource_name_string, "\""));
  }
  // Save result.
  auto& resource_data = (*update_map)[resource_name];
  ResourceTypeData* typed_resource =
      static_cast<ResourceTypeData*>(resource.get());
  resource_data.resource = std::move(typed_resource->resource);
  resource_data.serialized_proto = std::move(serialized_resource);
  return GRPC_ERROR_NONE;
}

}  // namespace

XdsApi::AdsParseResult XdsApi::ParseAdsResponse(
    const XdsBootstrap::XdsServer& server, const grpc_slice& encoded_response,
    const std::map<absl::string_view /*authority*/,
                   std::set<absl::string_view /*name*/>>&
        subscribed_listener_names,
    const std::map<absl::string_view /*authority*/,
                   std::set<absl::string_view /*name*/>>&
        subscribed_route_config_names,
    const std::map<absl::string_view /*authority*/,
                   std::set<absl::string_view /*name*/>>&
        subscribed_cluster_names,
    const std::map<absl::string_view /*authority*/,
                   std::set<absl::string_view /*name*/>>&
        subscribed_eds_service_names) {
  AdsParseResult result;
  upb::Arena arena;
  const XdsEncodingContext context = {client_,
                                      tracer_,
                                      symtab_.ptr(),
                                      arena.ptr(),
                                      server.ShouldUseV3(),
                                      certificate_provider_definition_map_};
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
  result.type_url = TypeUrlInternalToExternal(absl::StripPrefix(
      UpbStringToAbsl(
          envoy_service_discovery_v3_DiscoveryResponse_type_url(response)),
      "type.googleapis.com/"));
  result.version = UpbStringToStdString(
      envoy_service_discovery_v3_DiscoveryResponse_version_info(response));
  result.nonce = UpbStringToStdString(
      envoy_service_discovery_v3_DiscoveryResponse_nonce(response));
  // Get the resources from the response.
  std::vector<grpc_error_handle> errors;
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_service_discovery_v3_DiscoveryResponse_resources(response, &size);
  for (size_t i = 0; i < size; ++i) {
    // Parse the response according to the resource type.
    // TODO(roth): When we have time, change the API here to avoid the need
    // for templating and conditionals.
    grpc_error_handle parse_error = GRPC_ERROR_NONE;
    if (IsLds(result.type_url)) {
      XdsListenerResourceType resource_type;
      auto& update_map = result.lds_update_map;
      parse_error = AdsResourceParse(
          context, &resource_type, i, resources[i], subscribed_listener_names,
          [&update_map](absl::string_view resource_name_string,
                        XdsApi::ResourceName resource_name,
                        std::unique_ptr<XdsResourceType::ResourceData> resource,
                        std::string serialized_resource) {
            return AddResult<LdsUpdateMap,
                             XdsListenerResourceType::ListenerData>(
                &update_map, resource_name_string, std::move(resource_name),
                std::move(resource), std::move(serialized_resource));
          },
          &result.resource_names_failed);
    } else if (IsRds(result.type_url)) {
      XdsRouteConfigResourceType resource_type;
      auto& update_map = result.rds_update_map;
      parse_error = AdsResourceParse(
          context, &resource_type, i, resources[i],
          subscribed_route_config_names,
          [&update_map](absl::string_view resource_name_string,
                        XdsApi::ResourceName resource_name,
                        std::unique_ptr<XdsResourceType::ResourceData> resource,
                        std::string serialized_resource) {
            return AddResult<RdsUpdateMap,
                             XdsRouteConfigResourceType::RouteConfigData>(
                &update_map, resource_name_string, std::move(resource_name),
                std::move(resource), std::move(serialized_resource));
          },
          &result.resource_names_failed);
    } else if (IsCds(result.type_url)) {
      XdsClusterResourceType resource_type;
      auto& update_map = result.cds_update_map;
      parse_error = AdsResourceParse(
          context, &resource_type, i, resources[i], subscribed_cluster_names,
          [&update_map](absl::string_view resource_name_string,
                        XdsApi::ResourceName resource_name,
                        std::unique_ptr<XdsResourceType::ResourceData> resource,
                        std::string serialized_resource) {
            return AddResult<CdsUpdateMap, XdsClusterResourceType::ClusterData>(
                &update_map, resource_name_string, std::move(resource_name),
                std::move(resource), std::move(serialized_resource));
          },
          &result.resource_names_failed);
    } else if (IsEds(result.type_url)) {
      XdsEndpointResourceType resource_type;
      auto& update_map = result.eds_update_map;
      parse_error = AdsResourceParse(
          context, &resource_type, i, resources[i],
          subscribed_eds_service_names,
          [&update_map](absl::string_view resource_name_string,
                        XdsApi::ResourceName resource_name,
                        std::unique_ptr<XdsResourceType::ResourceData> resource,
                        std::string serialized_resource) {
            return AddResult<EdsUpdateMap,
                             XdsEndpointResourceType::EndpointData>(
                &update_map, resource_name_string, std::move(resource_name),
                std::move(resource), std::move(serialized_resource));
          },
          &result.resource_names_failed);
    }
    if (parse_error != GRPC_ERROR_NONE) errors.push_back(parse_error);
  }
  result.parse_error =
      GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing ADS response", &errors);
  return result;
}

namespace {

void MaybeLogLrsRequest(
    const XdsEncodingContext& context,
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
    const XdsEncodingContext& context,
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
  const XdsEncodingContext context = {client_,
                                      tracer_,
                                      symtab_.ptr(),
                                      arena.ptr(),
                                      server.ShouldUseV3(),
                                      certificate_provider_definition_map_};
  // Create a request.
  envoy_service_load_stats_v3_LoadStatsRequest* request =
      envoy_service_load_stats_v3_LoadStatsRequest_new(arena.ptr());
  // Populate node.
  envoy_config_core_v3_Node* node_msg =
      envoy_service_load_stats_v3_LoadStatsRequest_mutable_node(request,
                                                                arena.ptr());
  PopulateNode(context, node_, build_version_, user_agent_name_,
               user_agent_version_, node_msg);
  envoy_config_core_v3_Node_add_client_features(
      node_msg, upb_strview_makez("envoy.lrs.supports_send_all_clusters"),
      arena.ptr());
  MaybeLogLrsRequest(context, request);
  return SerializeLrsRequest(context, request);
}

namespace {

void LocalityStatsPopulate(
    const XdsEncodingContext& context,
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
  const XdsEncodingContext context = {
      client_,     tracer_, symtab_.ptr(),
      arena.ptr(), false,   certificate_provider_definition_map_};
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

google_protobuf_Timestamp* GrpcMillisToTimestamp(
    const XdsEncodingContext& context, grpc_millis value) {
  google_protobuf_Timestamp* timestamp =
      google_protobuf_Timestamp_new(context.arena);
  gpr_timespec timespec = grpc_millis_to_timespec(value, GPR_CLOCK_REALTIME);
  google_protobuf_Timestamp_set_seconds(timestamp, timespec.tv_sec);
  google_protobuf_Timestamp_set_nanos(timestamp, timespec.tv_nsec);
  return timestamp;
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
  const XdsEncodingContext context = {
      client_,     tracer_, symtab_.ptr(),
      arena.ptr(), true,    certificate_provider_definition_map_};
  PopulateNode(context, node_, build_version_, user_agent_name_,
               user_agent_version_, node);
  // Dump each resource.
  std::vector<std::string> type_url_storage;
  for (const auto& p : resource_type_metadata_map) {
    absl::string_view type_url = p.first;
    const ResourceMetadataMap& resource_metadata_map = p.second;
    type_url_storage.emplace_back(
        absl::StrCat("type.googleapis.com/", type_url));
    for (const auto& q : resource_metadata_map) {
      absl::string_view resource_name = q.first;
      const ResourceMetadata& metadata = *q.second;
      auto* entry =
          envoy_service_status_v3_ClientConfig_add_generic_xds_configs(
              client_config, context.arena);
      envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_type_url(
          entry, StdStringToUpbString(type_url_storage.back()));
      envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_name(
          entry, StdStringToUpbString(resource_name));
      envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_client_status(
          entry, metadata.client_status);
      if (!metadata.serialized_proto.empty()) {
        envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_version_info(
            entry, StdStringToUpbString(metadata.version));
        envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_last_updated(
            entry, GrpcMillisToTimestamp(context, metadata.update_time));
        auto* any_field =
            envoy_service_status_v3_ClientConfig_GenericXdsConfig_mutable_xds_config(
                entry, context.arena);
        google_protobuf_Any_set_type_url(
            any_field, StdStringToUpbString(type_url_storage.back()));
        google_protobuf_Any_set_value(
            any_field, StdStringToUpbString(metadata.serialized_proto));
      }
      if (metadata.client_status == XdsApi::ResourceMetadata::NACKED) {
        auto* update_failure_state =
            envoy_admin_v3_UpdateFailureState_new(context.arena);
        envoy_admin_v3_UpdateFailureState_set_details(
            update_failure_state,
            StdStringToUpbString(metadata.failed_details));
        envoy_admin_v3_UpdateFailureState_set_version_info(
            update_failure_state,
            StdStringToUpbString(metadata.failed_version));
        envoy_admin_v3_UpdateFailureState_set_last_update_attempt(
            update_failure_state,
            GrpcMillisToTimestamp(context, metadata.failed_update_time));
        envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_error_state(
            entry, update_failure_state);
      }
    }
  }
  // Serialize the upb message to bytes
  size_t output_length;
  char* output = envoy_service_status_v3_ClientConfig_serialize(
      client_config, arena.ptr(), &output_length);
  return std::string(output, output_length);
}

}  // namespace grpc_core
