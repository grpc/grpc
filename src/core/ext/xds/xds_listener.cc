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

#include "src/core/ext/xds/xds_listener.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/config_source.upb.h"
#include "envoy/config/core/v3/protocol.upb.h"
#include "envoy/config/listener/v3/api_listener.upb.h"
#include "envoy/config/listener/v3/listener.upb.h"
#include "envoy/config/listener/v3/listener.upbdefs.h"
#include "envoy/config/listener/v3/listener_components.upb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upbdefs.h"
#include "google/protobuf/wrappers.upb.h"
#include "upb/text_encode.h"
#include "upb/upb.h"
#include "upb/upb.hpp"

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/sockaddr.h"

namespace grpc_core {

//
// XdsListenerResource::DownstreamTlsContext
//

std::string XdsListenerResource::DownstreamTlsContext::ToString() const {
  return absl::StrFormat("common_tls_context=%s, require_client_certificate=%s",
                         common_tls_context.ToString(),
                         require_client_certificate ? "true" : "false");
}

bool XdsListenerResource::DownstreamTlsContext::Empty() const {
  return common_tls_context.Empty();
}

//
// XdsListenerResource::HttpConnectionManager
//

std::string XdsListenerResource::HttpConnectionManager::ToString() const {
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
// XdsListenerResource::HttpFilter
//

std::string XdsListenerResource::HttpConnectionManager::HttpFilter::ToString()
    const {
  return absl::StrCat("{name=", name, ", config=", config.ToString(), "}");
}

//
// XdsListenerResource::FilterChainData
//

std::string XdsListenerResource::FilterChainData::ToString() const {
  return absl::StrCat(
      "{downstream_tls_context=", downstream_tls_context.ToString(),
      " http_connection_manager=", http_connection_manager.ToString(), "}");
}

//
// XdsListenerResource::FilterChainMap::CidrRange
//

std::string XdsListenerResource::FilterChainMap::CidrRange::ToString() const {
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
    std::vector<XdsListenerResource::FilterChainMap::CidrRange> prefix_ranges;
    XdsListenerResource::FilterChainMap::ConnectionSourceType source_type =
        XdsListenerResource::FilterChainMap::ConnectionSourceType::kAny;
    std::vector<XdsListenerResource::FilterChainMap::CidrRange>
        source_prefix_ranges;
    std::vector<uint32_t> source_ports;
    std::vector<std::string> server_names;
    std::string transport_protocol;
    std::vector<std::string> application_protocols;

    std::string ToString() const;
  } filter_chain_match;

  std::shared_ptr<XdsListenerResource::FilterChainData> filter_chain_data;
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
  if (source_type == XdsListenerResource::FilterChainMap::ConnectionSourceType::
                         kSameIpOrLoopback) {
    contents.push_back("source_type=SAME_IP_OR_LOOPBACK");
  } else if (source_type == XdsListenerResource::FilterChainMap::
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
// XdsListenerResource::FilterChainMap
//

std::string XdsListenerResource::FilterChainMap::ToString() const {
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
              XdsListenerResource::FilterChainMap::ConnectionSourceType>(
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
// XdsListenerResource
//

std::string XdsListenerResource::ToString() const {
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
// XdsListenerResourceType
//

namespace {

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
    XdsListenerResource::HttpConnectionManager* http_connection_manager) {
  MaybeLogHttpConnectionManager(context, http_connection_manager_proto);
  // NACK a non-zero `xff_num_trusted_hops` and a `non-empty
  // original_ip_detection_extensions` as mentioned in
  // https://github.com/grpc/proposal/blob/master/A41-xds-rbac.md
  if (envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_xff_num_trusted_hops(
          http_connection_manager_proto) != 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "'xff_num_trusted_hops' must be zero");
  }
  if (envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_has_original_ip_detection_extensions(
          http_connection_manager_proto)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "'original_ip_detection_extensions' must be empty");
  }
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
            " failed to parse: ", StatusToString(filter_config.status())));
      }
      http_connection_manager->http_filters.emplace_back(
          XdsListenerResource::HttpConnectionManager::HttpFilter{
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
        XdsListenerResource::HttpConnectionManager::HttpFilter{
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
    XdsListenerResource* lds_update) {
  lds_update->type = XdsListenerResource::ListenerType::kHttpApiListener;
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
    XdsListenerResource::DownstreamTlsContext* downstream_tls_context) {
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
    XdsListenerResource::FilterChainMap::CidrRange* cidr_range) {
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
    XdsListenerResource::FilterChainMap::CidrRange cidr_range;
    grpc_error_handle error = CidrRangeParse(prefix_ranges[i], &cidr_range);
    if (error != GRPC_ERROR_NONE) return error;
    filter_chain_match->prefix_ranges.push_back(cidr_range);
  }
  filter_chain_match->source_type =
      static_cast<XdsListenerResource::FilterChainMap::ConnectionSourceType>(
          envoy_config_listener_v3_FilterChainMatch_source_type(
              filter_chain_match_proto));
  auto* source_prefix_ranges =
      envoy_config_listener_v3_FilterChainMatch_source_prefix_ranges(
          filter_chain_match_proto, &size);
  filter_chain_match->source_prefix_ranges.reserve(size);
  for (size_t i = 0; i < size; i++) {
    XdsListenerResource::FilterChainMap::CidrRange cidr_range;
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
      std::make_shared<XdsListenerResource::FilterChainData>();
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
// XdsListenerResource::FilterChainMap
struct InternalFilterChainMap {
  using SourceIpMap =
      std::map<std::string, XdsListenerResource::FilterChainMap::SourceIp>;
  using ConnectionSourceTypesArray = std::array<SourceIpMap, 3>;
  struct DestinationIp {
    absl::optional<XdsListenerResource::FilterChainMap::CidrRange> prefix_range;
    bool transport_protocol_raw_buffer_provided = false;
    ConnectionSourceTypesArray source_types_array;
  };
  using DestinationIpMap = std::map<std::string, DestinationIp>;
  DestinationIpMap destination_ip_map;
};

grpc_error_handle AddFilterChainDataForSourcePort(
    const FilterChain& filter_chain,
    XdsListenerResource::FilterChainMap::SourcePortsMap* ports_map,
    uint32_t port) {
  auto insert_result = ports_map->emplace(
      port, XdsListenerResource::FilterChainMap::FilterChainDataSharedPtr{
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
    XdsListenerResource::FilterChainMap::SourcePortsMap* ports_map) {
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
        "", XdsListenerResource::FilterChainMap::SourceIp());
    return AddFilterChainDataForSourcePorts(
        filter_chain, &insert_result.first->second.ports_map);
  } else {
    for (const auto& prefix_range :
         filter_chain.filter_chain_match.source_prefix_ranges) {
      auto insert_result = source_ip_map->emplace(
          absl::StrCat(grpc_sockaddr_to_string(&prefix_range.address, false),
                       "/", prefix_range.prefix_len),
          XdsListenerResource::FilterChainMap::SourceIp());
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

XdsListenerResource::FilterChainMap BuildFromInternalFilterChainMap(
    InternalFilterChainMap* internal_filter_chain_map) {
  XdsListenerResource::FilterChainMap filter_chain_map;
  for (auto& destination_ip_pair :
       internal_filter_chain_map->destination_ip_map) {
    XdsListenerResource::FilterChainMap::DestinationIp destination_ip;
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
    XdsListenerResource::FilterChainMap* filter_chain_map) {
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
    XdsListenerResource* lds_update) {
  lds_update->type = XdsListenerResource::ListenerType::kTcpListener;
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
    XdsListenerResource* lds_update) {
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

}  // namespace

absl::StatusOr<XdsResourceType::DecodeResult> XdsListenerResourceType::Decode(
    const XdsEncodingContext& context, absl::string_view serialized_resource,
    bool is_v2) const {
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
  auto listener_data = absl::make_unique<ResourceDataSubclass>();
  grpc_error_handle error =
      LdsResourceParse(context, resource, is_v2, &listener_data->resource);
  if (error != GRPC_ERROR_NONE) {
    std::string error_str = grpc_error_std_string(error);
    GRPC_ERROR_UNREF(error);
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_ERROR, "[xds_client %p] invalid Listener %s: %s",
              context.client, result.name.c_str(), error_str.c_str());
    }
    result.resource = absl::InvalidArgumentError(error_str);
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_INFO, "[xds_client %p] parsed Listener %s: %s",
              context.client, result.name.c_str(),
              listener_data->resource.ToString().c_str());
    }
    result.resource = std::move(listener_data);
  }
  return std::move(result);
}

}  // namespace grpc_core
