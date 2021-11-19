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

// FIXME: prune includes!
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

void MaybeLogListener(const XdsEncodingContext& context,
                      const envoy_config_listener_v3_Listener* listener) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_config_listener_v3_Listener_getmsgdef(context.symtab);
    char buf[10240];
    upb_text_encode(listener, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] Listener: %s", context.client, buf);
  }
}

void MaybeLogHttpConnectionManager(
    const XdsEncodingContext& context,
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

grpc_error_handle HttpConnectionManagerParse(
    bool is_client, const XdsEncodingContext& context,
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
      http_connection_manager->http_max_stream_duration =
          Duration::Parse(duration);
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
        return GRPC_ERROR_CREATE_FROM_CPP_STRING(
            absl::StrCat("empty filter name at index ", i));
      }
      if (names_seen.find(name) != names_seen.end()) {
        return GRPC_ERROR_CREATE_FROM_CPP_STRING(
            absl::StrCat("duplicate HTTP filter name: ", name));
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
        return GRPC_ERROR_CREATE_FROM_CPP_STRING(
            absl::StrCat("no filter config specified for filter name ", name));
      }
      absl::string_view filter_type;
      grpc_error_handle error =
          ExtractHttpFilterTypeName(context, any, &filter_type);
      if (error != GRPC_ERROR_NONE) return error;
      const XdsHttpFilterImpl* filter_impl =
          XdsHttpFilterRegistry::GetFilterForType(filter_type);
      if (filter_impl == nullptr) {
        if (is_optional) continue;
        return GRPC_ERROR_CREATE_FROM_CPP_STRING(
            absl::StrCat("no filter registered for config type ", filter_type));
      }
      if ((is_client && !filter_impl->IsSupportedOnClients()) ||
          (!is_client && !filter_impl->IsSupportedOnServers())) {
        if (is_optional) continue;
        return GRPC_ERROR_CREATE_FROM_CPP_STRING(
            absl::StrFormat("Filter %s is not supported on %s", filter_type,
                            is_client ? "clients" : "servers"));
      }
      absl::StatusOr<XdsHttpFilterImpl::FilterConfig> filter_config =
          filter_impl->GenerateFilterConfig(google_protobuf_Any_value(any),
                                            context.arena);
      if (!filter_config.ok()) {
        return GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
            "filter config for type ", filter_type,
            " failed to parse: ", filter_config.status().ToString()));
      }
      http_connection_manager->http_filters.emplace_back(
          XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter{
              std::string(name), std::move(*filter_config)});
    }
    if (http_connection_manager->http_filters.empty()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Expected at least one HTTP filter");
    }
    // Make sure that the last filter is terminal and non-last filters are
    // non-terminal. Note that this check is being performed in a separate loop
    // to take care of the case where there are two terminal filters in the list
    // out of which only one gets added in the final list.
    for (const auto& http_filter : http_connection_manager->http_filters) {
      const XdsHttpFilterImpl* filter_impl =
          XdsHttpFilterRegistry::GetFilterForType(
              http_filter.config.config_proto_type_name);
      if (&http_filter != &http_connection_manager->http_filters.back()) {
        // Filters before the last filter must not be terminal.
        if (filter_impl->IsTerminalFilter()) {
          return GRPC_ERROR_CREATE_FROM_CPP_STRING(
              absl::StrCat("terminal filter for config type ",
                           http_filter.config.config_proto_type_name,
                           " must be the last filter in the chain"));
        }
      } else {
        // The last filter must be terminal.
        if (!filter_impl->IsTerminalFilter()) {
          return GRPC_ERROR_CREATE_FROM_CPP_STRING(
              absl::StrCat("non-terminal filter for config type ",
                           http_filter.config.config_proto_type_name,
                           " is the last filter in the chain"));
        }
      }
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
  // Guarding parsing of RouteConfig on the server side with the environmental
  // variable since that's the first feature on the server side that will be
  // using this.
  if (is_client || XdsRbacEnabled()) {
    // Found inlined route_config. Parse it to find the cluster_name.
    if (envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_has_route_config(
            http_connection_manager_proto)) {
      const envoy_config_route_v3_RouteConfiguration* route_config =
          envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_route_config(
              http_connection_manager_proto);
      XdsRouteConfigResource rds_update;
      grpc_error_handle error =
          XdsRouteConfigResource::Parse(context, route_config, &rds_update);
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

grpc_error_handle LdsResourceParseClient(
    const XdsEncodingContext& context,
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
    const XdsEncodingContext& context,
    const envoy_config_core_v3_TransportSocket* transport_socket,
    XdsApi::DownstreamTlsContext* downstream_tls_context) {
  absl::string_view name = UpbStringToAbsl(
      envoy_config_core_v3_TransportSocket_name(transport_socket));
  if (name != "envoy.transport_sockets.tls") {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("Unrecognized transport socket: ", name));
  }
  auto* typed_config =
      envoy_config_core_v3_TransportSocket_typed_config(transport_socket);
  std::vector<grpc_error_handle> errors;
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
      grpc_error_handle error =
          CommonTlsContext::Parse(context, common_tls_context,
                                &downstream_tls_context->common_tls_context);
      if (error != GRPC_ERROR_NONE) errors.push_back(error);
    }
    auto* require_client_certificate =
        envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_require_client_certificate(
            downstream_tls_context_proto);
    if (require_client_certificate != nullptr) {
      downstream_tls_context->require_client_certificate =
          google_protobuf_BoolValue_value(require_client_certificate);
    }
    auto* require_sni =
        envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_require_sni(
            downstream_tls_context_proto);
    if (require_sni != nullptr &&
        google_protobuf_BoolValue_value(require_sni)) {
      errors.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("require_sni: unsupported"));
    }
    if (envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_ocsp_staple_policy(
            downstream_tls_context_proto) !=
        envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_LENIENT_STAPLING) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "ocsp_staple_policy: Only LENIENT_STAPLING supported"));
    }
  }
  if (downstream_tls_context->common_tls_context
          .tls_certificate_provider_instance.instance_name.empty()) {
    errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "TLS configuration provided but no "
        "tls_certificate_provider_instance found."));
  }
  if (downstream_tls_context->require_client_certificate &&
      downstream_tls_context->common_tls_context.certificate_validation_context
          .ca_certificate_provider_instance.instance_name.empty()) {
    errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "TLS configuration requires client certificates but no certificate "
        "provider instance specified for validation."));
  }
  if (!downstream_tls_context->common_tls_context.certificate_validation_context
           .match_subject_alt_names.empty()) {
    errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "match_subject_alt_names not supported on servers"));
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("Error parsing DownstreamTlsContext",
                                       &errors);
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
    const XdsEncodingContext& context,
    const envoy_config_listener_v3_FilterChain* filter_chain_proto, bool is_v2,
    FilterChain* filter_chain) {
  std::vector<grpc_error_handle> errors;
  auto* filter_chain_match =
      envoy_config_listener_v3_FilterChain_filter_chain_match(
          filter_chain_proto);
  if (filter_chain_match != nullptr) {
    grpc_error_handle error = FilterChainMatchParse(
        filter_chain_match, &filter_chain->filter_chain_match);
    if (error != GRPC_ERROR_NONE) errors.push_back(error);
  }
  filter_chain->filter_chain_data =
      std::make_shared<XdsApi::LdsUpdate::FilterChainData>();
  // Parse the filters list. Currently we only support HttpConnectionManager.
  size_t size = 0;
  auto* filters =
      envoy_config_listener_v3_FilterChain_filters(filter_chain_proto, &size);
  if (size != 1) {
    errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "FilterChain should have exactly one filter: HttpConnectionManager; no "
        "other filter is supported at the moment"));
  } else {
    auto* typed_config =
        envoy_config_listener_v3_Filter_typed_config(filters[0]);
    if (typed_config == nullptr) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "No typed_config found in filter."));
    } else {
      absl::string_view type_url =
          UpbStringToAbsl(google_protobuf_Any_type_url(typed_config));
      if (type_url !=
          "type.googleapis.com/"
          "envoy.extensions.filters.network.http_connection_manager.v3."
          "HttpConnectionManager") {
        errors.push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
            absl::StrCat("Unsupported filter type ", type_url)));
      } else {
        const upb_strview encoded_http_connection_manager =
            google_protobuf_Any_value(typed_config);
        const auto* http_connection_manager =
            envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_parse(
                encoded_http_connection_manager.data,
                encoded_http_connection_manager.size, context.arena);
        if (http_connection_manager == nullptr) {
          errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "Could not parse HttpConnectionManager config from filter "
              "typed_config"));
        } else {
          grpc_error_handle error = HttpConnectionManagerParse(
              false /* is_client */, context, http_connection_manager, is_v2,
              &filter_chain->filter_chain_data->http_connection_manager);
          if (error != GRPC_ERROR_NONE) errors.push_back(error);
        }
      }
    }
  }
  auto* transport_socket =
      envoy_config_listener_v3_FilterChain_transport_socket(filter_chain_proto);
  if (transport_socket != nullptr) {
    grpc_error_handle error = DownstreamTlsContextParse(
        context, transport_socket,
        &filter_chain->filter_chain_data->downstream_tls_context);
    if (error != GRPC_ERROR_NONE) errors.push_back(error);
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("Error parsing FilterChain", &errors);
}

grpc_error_handle AddressParse(
    const envoy_config_core_v3_Address* address_proto, std::string* address) {
  const auto* socket_address =
      envoy_config_core_v3_Address_socket_address(address_proto);
  if (socket_address == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
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
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
        "Duplicate matching rules detected when adding filter chain: ",
        filter_chain.filter_chain_match.ToString()));
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

grpc_error_handle LdsResourceParseServer(
    const XdsEncodingContext& context,
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

grpc_error_handle LdsResourceParse(
    const XdsEncodingContext& context,
    const envoy_config_listener_v3_Listener* listener, bool is_v2,
    XdsApi::LdsUpdate* lds_update) {
  // Check whether it's a client or server listener.
  const envoy_config_listener_v3_ApiListener* api_listener =
      envoy_config_listener_v3_Listener_api_listener(listener);
  const envoy_config_core_v3_Address* address =
      envoy_config_listener_v3_Listener_address(listener);
  if (api_listener != nullptr && address != nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Listener has both address and ApiListener");
  }
  if (api_listener == nullptr && address == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Listener has neither address nor ApiListener");
  }
  // Validate Listener fields.
  grpc_error_handle error = GRPC_ERROR_NONE;
  if (api_listener != nullptr) {
    error = LdsResourceParseClient(context, api_listener, is_v2, lds_update);
  } else {
    error = LdsResourceParseServer(context, listener, is_v2, lds_update);
  }
  return error;
}

class ListenerResourceType : public XdsResourceType {
 public:
  struct ListenerData : public ResourceData {
    XdsApi::LdsUpdate resource;
  };

  absl::string_view type_url() const override { return XdsApi::kLdsTypeUrl; }
  absl::string_view v2_type_url() const override { return kLdsV2TypeUrl; }

  absl::StatusOr<DecodeResult> Decode(const XdsEncodingContext& context,
                                      absl::string_view serialized_resource,
                                      bool is_v2) const override {
    // Parse serialized proto.
    auto* resource = envoy_config_listener_v3_Listener_parse(
        serialized_resource.data(), serialized_resource.size(), context.arena);
    if (resource == nullptr) {
      return absl::InvalidArgumentError("Can't parse Listener resource.");
    }
    MaybeLogListener(context, resource);
    // Validate resource.
    DecodeResult result;
    result.name =
        UpbStringToStdString(envoy_config_listener_v3_Listener_name(resource));
    auto listener_data = absl::make_unique<ListenerData>();
    grpc_error_handle error =
        LdsResourceParse(context, resource, is_v2, &listener_data->resource);
    if (error != GRPC_ERROR_NONE) {
      result.resource =
          absl::InvalidArgumentError(grpc_error_std_string(error));
      GRPC_ERROR_UNREF(error);
    } else {
      result.resource = std::move(listener_data);
    }
    return std::move(result);
  }
};

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
      ListenerResourceType resource_type;
      auto& update_map = result.lds_update_map;
      parse_error = AdsResourceParse(
          context, &resource_type, i, resources[i], subscribed_listener_names,
          [&update_map](absl::string_view resource_name_string,
                        XdsApi::ResourceName resource_name,
                        std::unique_ptr<XdsResourceType::ResourceData> resource,
                        std::string serialized_resource) {
            return AddResult<LdsUpdateMap, ListenerResourceType::ListenerData>(
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
            return AddResult<EdsUpdateMap, XdsEndpointResourceType::EndpointData>(
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

google_protobuf_Timestamp* GrpcMillisToTimestamp(const XdsEncodingContext& context,
                                                 grpc_millis value) {
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
