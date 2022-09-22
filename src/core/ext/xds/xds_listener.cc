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

#include <stdint.h>

#include <set>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/strip.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/config_source.upb.h"
#include "envoy/config/core/v3/protocol.upb.h"
#include "envoy/config/listener/v3/api_listener.upb.h"
#include "envoy/config/listener/v3/listener.upb.h"
#include "envoy/config/listener/v3/listener.upbdefs.h"
#include "envoy/config/listener/v3/listener_components.upb.h"
#include "envoy/config/route/v3/route.upb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upbdefs.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "upb/text_encode.h"
#include "upb/upb.h"

#include <grpc/support/log.h>

#include "src/core/ext/xds/upb_utils.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/json/json.h"

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
  std::vector<std::string> contents;
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
  auto addr_str = grpc_sockaddr_to_string(&address, false);
  return absl::StrCat(
      "{address_prefix=",
      addr_str.ok() ? addr_str.value() : addr_str.status().ToString(),
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
  std::vector<std::string> contents;
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
  std::vector<std::string> contents;
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
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager*
        http_connection_manager_config) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_MessageDef* msg_type =
        envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_getmsgdef(
            context.symtab);
    char buf[10240];
    upb_TextEncode(http_connection_manager_config, msg_type, nullptr, 0, buf,
                   sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] HttpConnectionManager: %s",
            context.client, buf);
  }
}

absl::StatusOr<XdsListenerResource::HttpConnectionManager>
HttpConnectionManagerParse(
    bool is_client, const XdsResourceType::DecodeContext& context,
    const envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager*
        http_connection_manager_proto,
    bool is_v2) {
  MaybeLogHttpConnectionManager(context, http_connection_manager_proto);
  std::vector<std::string> errors;
  XdsListenerResource::HttpConnectionManager http_connection_manager;
  // NACK a non-zero `xff_num_trusted_hops` and a `non-empty
  // original_ip_detection_extensions` as mentioned in
  // https://github.com/grpc/proposal/blob/master/A41-xds-rbac.md
  if (envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_xff_num_trusted_hops(
          http_connection_manager_proto) != 0) {
    errors.emplace_back("'xff_num_trusted_hops' must be zero");
  }
  if (envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_has_original_ip_detection_extensions(
          http_connection_manager_proto)) {
    errors.emplace_back("'original_ip_detection_extensions' must be empty");
  }
  // Obtain max_stream_duration from Http Protocol Options.
  const envoy_config_core_v3_HttpProtocolOptions* options =
      envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_common_http_protocol_options(
          http_connection_manager_proto);
  if (options != nullptr) {
    const google_protobuf_Duration* duration =
        envoy_config_core_v3_HttpProtocolOptions_max_stream_duration(options);
    if (duration != nullptr) {
      http_connection_manager.http_max_stream_duration =
          ParseDuration(duration);
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
        errors.emplace_back(absl::StrCat("empty filter name at index ", i));
        continue;
      }
      if (names_seen.find(name) != names_seen.end()) {
        errors.emplace_back(absl::StrCat("duplicate HTTP filter name: ", name));
        continue;
      }
      names_seen.insert(name);
      const bool is_optional =
          envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_is_optional(
              http_filter);
      const google_protobuf_Any* any =
          envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_typed_config(
              http_filter);
      if (any == nullptr) {
        if (!is_optional) {
          errors.emplace_back(absl::StrCat(
              "no filter config specified for filter name ", name));
        }
        continue;
      }
      auto filter_type = ExtractExtensionTypeName(context, any);
      if (!filter_type.ok()) {
        errors.emplace_back(absl::StrCat("filter name ", name, ": ",
                                         filter_type.status().message()));
        continue;
      }
      const XdsHttpFilterImpl* filter_impl =
          XdsHttpFilterRegistry::GetFilterForType(filter_type->type);
      if (filter_impl == nullptr) {
        if (!is_optional) {
          errors.emplace_back(absl::StrCat(
              "no filter registered for config type ", filter_type->type));
        }
        continue;
      }
      if ((is_client && !filter_impl->IsSupportedOnClients()) ||
          (!is_client && !filter_impl->IsSupportedOnServers())) {
        if (!is_optional) {
          errors.emplace_back(absl::StrFormat(
              "Filter %s is not supported on %s", filter_type->type,
              is_client ? "clients" : "servers"));
        }
        continue;
      }
      absl::StatusOr<XdsHttpFilterImpl::FilterConfig> filter_config =
          filter_impl->GenerateFilterConfig(google_protobuf_Any_value(any),
                                            context.arena);
      if (!filter_config.ok()) {
        errors.emplace_back(absl::StrCat(
            "filter config for type ", filter_type->type,
            " failed to parse: ", StatusToString(filter_config.status())));
        continue;
      }
      http_connection_manager.http_filters.emplace_back(
          XdsListenerResource::HttpConnectionManager::HttpFilter{
              std::string(name), std::move(*filter_config)});
    }
    if (http_connection_manager.http_filters.empty()) {
      errors.emplace_back("Expected at least one HTTP filter");
    }
    // Make sure that the last filter is terminal and non-last filters are
    // non-terminal. Note that this check is being performed in a separate loop
    // to take care of the case where there are two terminal filters in the list
    // out of which only one gets added in the final list.
    for (const auto& http_filter : http_connection_manager.http_filters) {
      const XdsHttpFilterImpl* filter_impl =
          XdsHttpFilterRegistry::GetFilterForType(
              http_filter.config.config_proto_type_name);
      if (&http_filter != &http_connection_manager.http_filters.back()) {
        // Filters before the last filter must not be terminal.
        if (filter_impl->IsTerminalFilter()) {
          errors.emplace_back(
              absl::StrCat("terminal filter for config type ",
                           http_filter.config.config_proto_type_name,
                           " must be the last filter in the chain"));
        }
      } else {
        // The last filter must be terminal.
        if (!filter_impl->IsTerminalFilter()) {
          errors.emplace_back(
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
    http_connection_manager.http_filters.emplace_back(
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
      auto rds_update = XdsRouteConfigResource::Parse(context, route_config);
      if (!rds_update.ok()) {
        errors.emplace_back(rds_update.status().message());
      } else {
        http_connection_manager.rds_update = std::move(*rds_update);
      }
    } else {
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
        errors.emplace_back(
            "HttpConnectionManager missing config_source for RDS.");
      } else if (!envoy_config_core_v3_ConfigSource_has_ads(config_source) &&
                 !envoy_config_core_v3_ConfigSource_has_self(config_source)) {
        errors.emplace_back(
            "HttpConnectionManager ConfigSource for RDS does not specify ADS "
            "or SELF.");
      } else {
        // Get the route_config_name.
        http_connection_manager.route_config_name = UpbStringToStdString(
            envoy_extensions_filters_network_http_connection_manager_v3_Rds_route_config_name(
                rds));
      }
    }
  }
  // Return result.
  if (!errors.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Errors parsing HttpConnectionManager config: [",
                     absl::StrJoin(errors, "; "), "]"));
  }
  return http_connection_manager;
}

absl::StatusOr<XdsListenerResource> LdsResourceParseClient(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_listener_v3_ApiListener* api_listener, bool is_v2) {
  const upb_StringView encoded_api_listener = google_protobuf_Any_value(
      envoy_config_listener_v3_ApiListener_api_listener(api_listener));
  const auto* http_connection_manager =
      envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_parse(
          encoded_api_listener.data, encoded_api_listener.size, context.arena);
  if (http_connection_manager == nullptr) {
    return absl::InvalidArgumentError(
        "Could not parse HttpConnectionManager config from ApiListener");
  }
  auto hcm = HttpConnectionManagerParse(true /* is_client */, context,
                                        http_connection_manager, is_v2);
  if (!hcm.ok()) return hcm.status();
  XdsListenerResource lds_update;
  lds_update.type = XdsListenerResource::ListenerType::kHttpApiListener;
  lds_update.http_connection_manager = std::move(*hcm);
  return lds_update;
}

absl::StatusOr<XdsListenerResource::DownstreamTlsContext>
DownstreamTlsContextParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_core_v3_TransportSocket* transport_socket) {
  const auto* typed_config =
      envoy_config_core_v3_TransportSocket_typed_config(transport_socket);
  if (typed_config == nullptr) {
    return absl::InvalidArgumentError("transport socket typed config unset");
  }
  absl::string_view type_url = absl::StripPrefix(
      UpbStringToAbsl(google_protobuf_Any_type_url(typed_config)),
      "type.googleapis.com/");
  if (type_url !=
      "envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext") {
    return absl::InvalidArgumentError(
        absl::StrCat("Unrecognized transport socket type: ", type_url));
  }
  const upb_StringView encoded_downstream_tls_context =
      google_protobuf_Any_value(typed_config);
  const auto* downstream_tls_context_proto =
      envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_parse(
          encoded_downstream_tls_context.data,
          encoded_downstream_tls_context.size, context.arena);
  if (downstream_tls_context_proto == nullptr) {
    return absl::InvalidArgumentError("Can't decode downstream tls context.");
  }
  std::vector<std::string> errors;
  XdsListenerResource::DownstreamTlsContext downstream_tls_context;
  auto* common_tls_context =
      envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_common_tls_context(
          downstream_tls_context_proto);
  if (common_tls_context != nullptr) {
    auto common_context = CommonTlsContext::Parse(context, common_tls_context);
    if (!common_context.ok()) {
      errors.emplace_back(common_context.status().message());
    } else {
      downstream_tls_context.common_tls_context = std::move(*common_context);
    }
  }
  auto* require_client_certificate =
      envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_require_client_certificate(
          downstream_tls_context_proto);
  if (require_client_certificate != nullptr) {
    downstream_tls_context.require_client_certificate =
        google_protobuf_BoolValue_value(require_client_certificate);
  }
  auto* require_sni =
      envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_require_sni(
          downstream_tls_context_proto);
  if (require_sni != nullptr && google_protobuf_BoolValue_value(require_sni)) {
    errors.emplace_back("require_sni: unsupported");
  }
  if (envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_ocsp_staple_policy(
          downstream_tls_context_proto) !=
      envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_LENIENT_STAPLING) {
    errors.emplace_back("ocsp_staple_policy: Only LENIENT_STAPLING supported");
  }
  if (downstream_tls_context.common_tls_context
          .tls_certificate_provider_instance.instance_name.empty()) {
    errors.emplace_back(
        "TLS configuration provided but no "
        "tls_certificate_provider_instance found.");
  }
  if (downstream_tls_context.require_client_certificate &&
      downstream_tls_context.common_tls_context.certificate_validation_context
          .ca_certificate_provider_instance.instance_name.empty()) {
    errors.emplace_back(
        "TLS configuration requires client certificates but no certificate "
        "provider instance specified for validation.");
  }
  if (!downstream_tls_context.common_tls_context.certificate_validation_context
           .match_subject_alt_names.empty()) {
    errors.emplace_back("match_subject_alt_names not supported on servers");
  }
  // Return result.
  if (!errors.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Errors parsing DownstreamTlsContext: [",
                     absl::StrJoin(errors, "; "), "]"));
  }
  return downstream_tls_context;
}

absl::StatusOr<XdsListenerResource::FilterChainMap::CidrRange> CidrRangeParse(
    const envoy_config_core_v3_CidrRange* cidr_range_proto) {
  XdsListenerResource::FilterChainMap::CidrRange cidr_range;
  std::string address_prefix = UpbStringToStdString(
      envoy_config_core_v3_CidrRange_address_prefix(cidr_range_proto));
  auto address = StringToSockaddr(address_prefix, /*port=*/0);
  if (!address.ok()) return address.status();
  cidr_range.address = *address;
  cidr_range.prefix_len = 0;
  auto* prefix_len_proto =
      envoy_config_core_v3_CidrRange_prefix_len(cidr_range_proto);
  if (prefix_len_proto != nullptr) {
    cidr_range.prefix_len = std::min(
        google_protobuf_UInt32Value_value(prefix_len_proto),
        (reinterpret_cast<const grpc_sockaddr*>(cidr_range.address.addr))
                    ->sa_family == GRPC_AF_INET
            ? uint32_t(32)
            : uint32_t(128));
  }
  // Normalize the network address by masking it with prefix_len
  grpc_sockaddr_mask_bits(&cidr_range.address, cidr_range.prefix_len);
  return cidr_range;
}

absl::StatusOr<FilterChain::FilterChainMatch> FilterChainMatchParse(
    const envoy_config_listener_v3_FilterChainMatch* filter_chain_match_proto) {
  std::vector<std::string> errors;
  FilterChain::FilterChainMatch filter_chain_match;
  auto* destination_port =
      envoy_config_listener_v3_FilterChainMatch_destination_port(
          filter_chain_match_proto);
  if (destination_port != nullptr) {
    filter_chain_match.destination_port =
        google_protobuf_UInt32Value_value(destination_port);
  }
  size_t size = 0;
  auto* prefix_ranges = envoy_config_listener_v3_FilterChainMatch_prefix_ranges(
      filter_chain_match_proto, &size);
  filter_chain_match.prefix_ranges.reserve(size);
  for (size_t i = 0; i < size; i++) {
    auto cidr_range = CidrRangeParse(prefix_ranges[i]);
    if (!cidr_range.ok()) {
      errors.emplace_back(absl::StrCat("prefix range ", i, ": ",
                                       cidr_range.status().message()));
      continue;
    }
    filter_chain_match.prefix_ranges.push_back(*cidr_range);
  }
  filter_chain_match.source_type =
      static_cast<XdsListenerResource::FilterChainMap::ConnectionSourceType>(
          envoy_config_listener_v3_FilterChainMatch_source_type(
              filter_chain_match_proto));
  auto* source_prefix_ranges =
      envoy_config_listener_v3_FilterChainMatch_source_prefix_ranges(
          filter_chain_match_proto, &size);
  filter_chain_match.source_prefix_ranges.reserve(size);
  for (size_t i = 0; i < size; i++) {
    auto cidr_range = CidrRangeParse(source_prefix_ranges[i]);
    if (!cidr_range.ok()) {
      errors.emplace_back(absl::StrCat("source prefix range ", i, ": ",
                                       cidr_range.status().message()));
      continue;
    }
    filter_chain_match.source_prefix_ranges.push_back(*cidr_range);
  }
  auto* source_ports = envoy_config_listener_v3_FilterChainMatch_source_ports(
      filter_chain_match_proto, &size);
  filter_chain_match.source_ports.reserve(size);
  for (size_t i = 0; i < size; i++) {
    filter_chain_match.source_ports.push_back(source_ports[i]);
  }
  auto* server_names = envoy_config_listener_v3_FilterChainMatch_server_names(
      filter_chain_match_proto, &size);
  for (size_t i = 0; i < size; i++) {
    filter_chain_match.server_names.push_back(
        UpbStringToStdString(server_names[i]));
  }
  filter_chain_match.transport_protocol = UpbStringToStdString(
      envoy_config_listener_v3_FilterChainMatch_transport_protocol(
          filter_chain_match_proto));
  auto* application_protocols =
      envoy_config_listener_v3_FilterChainMatch_application_protocols(
          filter_chain_match_proto, &size);
  for (size_t i = 0; i < size; i++) {
    filter_chain_match.application_protocols.push_back(
        UpbStringToStdString(application_protocols[i]));
  }
  // Return result.
  if (!errors.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("errors parsing filter chain match: [",
                     absl::StrJoin(errors, "; "), "]"));
  }
  return filter_chain_match;
}

absl::StatusOr<FilterChain> FilterChainParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_listener_v3_FilterChain* filter_chain_proto,
    bool is_v2) {
  FilterChain filter_chain;
  std::vector<std::string> errors;
  auto* filter_chain_match =
      envoy_config_listener_v3_FilterChain_filter_chain_match(
          filter_chain_proto);
  if (filter_chain_match != nullptr) {
    auto match = FilterChainMatchParse(filter_chain_match);
    if (!match.ok()) {
      errors.emplace_back(match.status().message());
    } else {
      filter_chain.filter_chain_match = std::move(*match);
    }
  }
  filter_chain.filter_chain_data =
      std::make_shared<XdsListenerResource::FilterChainData>();
  // Parse the filters list. Currently we only support HttpConnectionManager.
  size_t size = 0;
  auto* filters =
      envoy_config_listener_v3_FilterChain_filters(filter_chain_proto, &size);
  if (size != 1) {
    errors.push_back(
        "FilterChain should have exactly one filter: HttpConnectionManager; no "
        "other filter is supported at the moment");
  } else {
    auto* typed_config =
        envoy_config_listener_v3_Filter_typed_config(filters[0]);
    if (typed_config == nullptr) {
      errors.emplace_back("No typed_config found in filter.");
    } else {
      absl::string_view type_url = absl::StripPrefix(
          UpbStringToAbsl(google_protobuf_Any_type_url(typed_config)),
          "type.googleapis.com/");
      if (type_url !=
          "envoy.extensions.filters.network.http_connection_manager.v3."
          "HttpConnectionManager") {
        errors.emplace_back(absl::StrCat("Unsupported filter type ", type_url));
      } else {
        const upb_StringView encoded_http_connection_manager =
            google_protobuf_Any_value(typed_config);
        const auto* http_connection_manager =
            envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_parse(
                encoded_http_connection_manager.data,
                encoded_http_connection_manager.size, context.arena);
        if (http_connection_manager == nullptr) {
          errors.emplace_back(
              "Could not parse HttpConnectionManager config from filter "
              "typed_config");
        } else {
          auto hcm = HttpConnectionManagerParse(
              /*is_client=*/false, context, http_connection_manager, is_v2);
          if (!hcm.ok()) {
            errors.emplace_back(hcm.status().message());
          } else {
            filter_chain.filter_chain_data->http_connection_manager =
                std::move(*hcm);
          }
        }
      }
    }
  }
  auto* transport_socket =
      envoy_config_listener_v3_FilterChain_transport_socket(filter_chain_proto);
  if (transport_socket != nullptr) {
    auto downstream_context =
        DownstreamTlsContextParse(context, transport_socket);
    if (!downstream_context.ok()) {
      errors.emplace_back(downstream_context.status().message());
    } else {
      filter_chain.filter_chain_data->downstream_tls_context =
          std::move(*downstream_context);
    }
  }
  // Return result.
  if (!errors.empty()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Errors parsing FilterChain: [", absl::StrJoin(errors, "; "), "]"));
  }
  return filter_chain;
}

absl::StatusOr<std::string> AddressParse(
    const envoy_config_core_v3_Address* address_proto) {
  const auto* socket_address =
      envoy_config_core_v3_Address_socket_address(address_proto);
  if (socket_address == nullptr) {
    return absl::InvalidArgumentError("Address does not have socket_address");
  }
  if (envoy_config_core_v3_SocketAddress_protocol(socket_address) !=
      envoy_config_core_v3_SocketAddress_TCP) {
    return absl::InvalidArgumentError("SocketAddress protocol is not TCP");
  }
  uint32_t port = envoy_config_core_v3_SocketAddress_port_value(socket_address);
  if (port > 65535) {
    return absl::InvalidArgumentError("Invalid port");
  }
  return JoinHostPort(
      UpbStringToAbsl(
          envoy_config_core_v3_SocketAddress_address(socket_address)),
      port);
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

absl::Status AddFilterChainDataForSourcePort(
    const FilterChain& filter_chain, uint32_t port,
    XdsListenerResource::FilterChainMap::SourcePortsMap* ports_map) {
  auto insert_result = ports_map->emplace(
      port, XdsListenerResource::FilterChainMap::FilterChainDataSharedPtr{
                filter_chain.filter_chain_data});
  if (!insert_result.second) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Duplicate matching rules detected when adding filter chain: ",
        filter_chain.filter_chain_match.ToString()));
  }
  return absl::OkStatus();
}

absl::Status AddFilterChainDataForSourcePorts(
    const FilterChain& filter_chain,
    XdsListenerResource::FilterChainMap::SourcePortsMap* ports_map) {
  if (filter_chain.filter_chain_match.source_ports.empty()) {
    return AddFilterChainDataForSourcePort(filter_chain, 0, ports_map);
  } else {
    for (uint32_t port : filter_chain.filter_chain_match.source_ports) {
      absl::Status status =
          AddFilterChainDataForSourcePort(filter_chain, port, ports_map);
      if (!status.ok()) return status;
    }
  }
  return absl::OkStatus();
}

absl::Status AddFilterChainDataForSourceIpRange(
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
      auto addr_str = grpc_sockaddr_to_string(&prefix_range.address, false);
      if (!addr_str.ok()) return addr_str.status();
      auto insert_result = source_ip_map->emplace(
          absl::StrCat(*addr_str, "/", prefix_range.prefix_len),
          XdsListenerResource::FilterChainMap::SourceIp());
      if (insert_result.second) {
        insert_result.first->second.prefix_range.emplace(prefix_range);
      }
      absl::Status status = AddFilterChainDataForSourcePorts(
          filter_chain, &insert_result.first->second.ports_map);
      if (!status.ok()) return status;
    }
  }
  return absl::OkStatus();
}

absl::Status AddFilterChainDataForSourceType(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIp* destination_ip) {
  GPR_ASSERT(static_cast<unsigned int>(
                 filter_chain.filter_chain_match.source_type) < 3);
  return AddFilterChainDataForSourceIpRange(
      filter_chain, &destination_ip->source_types_array[static_cast<int>(
                        filter_chain.filter_chain_match.source_type)]);
}

absl::Status AddFilterChainDataForApplicationProtocols(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIp* destination_ip) {
  // Only allow filter chains that do not mention application protocols
  if (!filter_chain.filter_chain_match.application_protocols.empty()) {
    return absl::OkStatus();
  }
  return AddFilterChainDataForSourceType(filter_chain, destination_ip);
}

absl::Status AddFilterChainDataForTransportProtocol(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIp* destination_ip) {
  const std::string& transport_protocol =
      filter_chain.filter_chain_match.transport_protocol;
  // Only allow filter chains with no transport protocol or "raw_buffer"
  if (!transport_protocol.empty() && transport_protocol != "raw_buffer") {
    return absl::OkStatus();
  }
  // If for this configuration, we've already seen filter chains that mention
  // the transport protocol as "raw_buffer", we will never match filter chains
  // that do not mention it.
  if (destination_ip->transport_protocol_raw_buffer_provided &&
      transport_protocol.empty()) {
    return absl::OkStatus();
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

absl::Status AddFilterChainDataForServerNames(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIp* destination_ip) {
  // Don't continue adding filter chains with server names mentioned
  if (!filter_chain.filter_chain_match.server_names.empty()) {
    return absl::OkStatus();
  }
  return AddFilterChainDataForTransportProtocol(filter_chain, destination_ip);
}

absl::Status AddFilterChainDataForDestinationIpRange(
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
      auto addr_str = grpc_sockaddr_to_string(&prefix_range.address, false);
      if (!addr_str.ok()) return addr_str.status();
      auto insert_result = destination_ip_map->emplace(
          absl::StrCat(*addr_str, "/", prefix_range.prefix_len),
          InternalFilterChainMap::DestinationIp());
      if (insert_result.second) {
        insert_result.first->second.prefix_range.emplace(prefix_range);
      }
      absl::Status status = AddFilterChainDataForServerNames(
          filter_chain, &insert_result.first->second);
      if (!status.ok()) return status;
    }
  }
  return absl::OkStatus();
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

absl::StatusOr<XdsListenerResource::FilterChainMap> BuildFilterChainMap(
    const std::vector<FilterChain>& filter_chains) {
  InternalFilterChainMap internal_filter_chain_map;
  for (const auto& filter_chain : filter_chains) {
    // Discard filter chain entries that specify destination port
    if (filter_chain.filter_chain_match.destination_port != 0) continue;
    absl::Status status = AddFilterChainDataForDestinationIpRange(
        filter_chain, &internal_filter_chain_map.destination_ip_map);
    if (!status.ok()) return status;
  }
  return BuildFromInternalFilterChainMap(&internal_filter_chain_map);
}

absl::StatusOr<XdsListenerResource> LdsResourceParseServer(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_listener_v3_Listener* listener, bool is_v2) {
  XdsListenerResource lds_update;
  lds_update.type = XdsListenerResource::ListenerType::kTcpListener;
  auto address =
      AddressParse(envoy_config_listener_v3_Listener_address(listener));
  if (!address.ok()) return address.status();
  lds_update.address = std::move(*address);
  const auto* use_original_dst =
      envoy_config_listener_v3_Listener_use_original_dst(listener);
  if (use_original_dst != nullptr) {
    if (google_protobuf_BoolValue_value(use_original_dst)) {
      return absl::InvalidArgumentError(
          "Field \'use_original_dst\' is not supported.");
    }
  }
  size_t size = 0;
  auto* filter_chains =
      envoy_config_listener_v3_Listener_filter_chains(listener, &size);
  std::vector<FilterChain> parsed_filter_chains;
  parsed_filter_chains.reserve(size);
  for (size_t i = 0; i < size; i++) {
    auto filter_chain = FilterChainParse(context, filter_chains[i], is_v2);
    if (!filter_chain.ok()) return filter_chain.status();
    parsed_filter_chains.push_back(std::move(*filter_chain));
  }
  auto filter_chain_map = BuildFilterChainMap(parsed_filter_chains);
  if (!filter_chain_map.ok()) return filter_chain_map.status();
  lds_update.filter_chain_map = std::move(*filter_chain_map);
  auto* default_filter_chain =
      envoy_config_listener_v3_Listener_default_filter_chain(listener);
  if (default_filter_chain != nullptr) {
    auto filter_chain = FilterChainParse(context, default_filter_chain, is_v2);
    if (!filter_chain.ok()) return filter_chain.status();
    if (filter_chain->filter_chain_data != nullptr) {
      lds_update.default_filter_chain =
          std::move(*filter_chain->filter_chain_data);
    }
  }
  if (size == 0 && default_filter_chain == nullptr) {
    return absl::InvalidArgumentError("No filter chain provided.");
  }
  return lds_update;
}

absl::StatusOr<XdsListenerResource> LdsResourceParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_listener_v3_Listener* listener, bool is_v2) {
  // Check whether it's a client or server listener.
  const envoy_config_listener_v3_ApiListener* api_listener =
      envoy_config_listener_v3_Listener_api_listener(listener);
  const envoy_config_core_v3_Address* address =
      envoy_config_listener_v3_Listener_address(listener);
  // TODO(roth): Re-enable the following check once
  // github.com/istio/istio/issues/38914 is resolved.
  // if (api_listener != nullptr && address != nullptr) {
  //   return absl::InvalidArgumentError(
  //       "Listener has both address and ApiListener");
  // }
  if (api_listener == nullptr && address == nullptr) {
    return absl::InvalidArgumentError(
        "Listener has neither address nor ApiListener");
  }
  // If api_listener is present, it's for a client; otherwise, it's
  // for a server.
  if (api_listener != nullptr) {
    return LdsResourceParseClient(context, api_listener, is_v2);
  }
  return LdsResourceParseServer(context, listener, is_v2);
}

void MaybeLogListener(const XdsResourceType::DecodeContext& context,
                      const envoy_config_listener_v3_Listener* listener) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_MessageDef* msg_type =
        envoy_config_listener_v3_Listener_getmsgdef(context.symtab);
    char buf[10240];
    upb_TextEncode(listener, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] Listener: %s", context.client, buf);
  }
}

}  // namespace

XdsResourceType::DecodeResult XdsListenerResourceType::Decode(
    const XdsResourceType::DecodeContext& context,
    absl::string_view serialized_resource, bool is_v2) const {
  DecodeResult result;
  // Parse serialized proto.
  auto* resource = envoy_config_listener_v3_Listener_parse(
      serialized_resource.data(), serialized_resource.size(), context.arena);
  if (resource == nullptr) {
    result.resource =
        absl::InvalidArgumentError("Can't parse Listener resource.");
    return result;
  }
  MaybeLogListener(context, resource);
  // Validate resource.
  result.name =
      UpbStringToStdString(envoy_config_listener_v3_Listener_name(resource));
  auto listener = LdsResourceParse(context, resource, is_v2);
  if (!listener.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_ERROR, "[xds_client %p] invalid Listener %s: %s",
              context.client, result.name->c_str(),
              listener.status().ToString().c_str());
    }
    result.resource = listener.status();
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_INFO, "[xds_client %p] parsed Listener %s: %s",
              context.client, result.name->c_str(),
              listener->ToString().c_str());
    }
    auto resource = absl::make_unique<ResourceDataSubclass>();
    resource->resource = std::move(*listener);
    result.resource = std::move(resource);
  }
  return result;
}

}  // namespace grpc_core
