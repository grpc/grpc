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

#include "src/core/xds/grpc/xds_listener_parser.h"

#include <grpc/support/port_platform.h>
#include <stdint.h>

#include <set>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
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
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/util/host_port.h"
#include "src/core/util/match.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_route_config_parser.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "upb/text/encode.h"

namespace grpc_core {

namespace {

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
    prefix_ranges_content.reserve(prefix_ranges.size());
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
    source_prefix_ranges_content.reserve(source_prefix_ranges.size());
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

void MaybeLogHttpConnectionManager(
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager*
        http_connection_manager_config) {
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*context.tracer) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_getmsgdef(
            context.symtab);
    char buf[10240];
    upb_TextEncode(
        reinterpret_cast<const upb_Message*>(http_connection_manager_config),
        msg_type, nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << context.client
            << "] HttpConnectionManager: " << buf;
  }
}

XdsListenerResource::HttpConnectionManager HttpConnectionManagerParse(
    bool is_client, const XdsResourceType::DecodeContext& context,
    XdsExtension extension, ValidationErrors* errors) {
  if (extension.type !=
      "envoy.extensions.filters.network.http_connection_manager.v3"
      ".HttpConnectionManager") {
    errors->AddError("unsupported filter type");
    return {};
  }
  auto* serialized_hcm_config =
      absl::get_if<absl::string_view>(&extension.value);
  if (serialized_hcm_config == nullptr) {
    errors->AddError("could not parse HttpConnectionManager config");
    return {};
  }
  const auto* http_connection_manager_proto =
      envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_parse(
          serialized_hcm_config->data(), serialized_hcm_config->size(),
          context.arena);
  if (http_connection_manager_proto == nullptr) {
    errors->AddError("could not parse HttpConnectionManager config");
    return {};
  }
  MaybeLogHttpConnectionManager(context, http_connection_manager_proto);
  XdsListenerResource::HttpConnectionManager http_connection_manager;
  // xff_num_trusted_hops -- must be zero as per
  // https://github.com/grpc/proposal/blob/master/A41-xds-rbac.md
  if (envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_xff_num_trusted_hops(
          http_connection_manager_proto) != 0) {
    ValidationErrors::ScopedField field(errors, ".xff_num_trusted_hops");
    errors->AddError("must be zero");
  }
  // original_ip_detection_extensions -- must be empty as per
  // https://github.com/grpc/proposal/blob/master/A41-xds-rbac.md
  {
    size_t size;
    envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_original_ip_detection_extensions(
        http_connection_manager_proto, &size);
    if (size != 0) {
      ValidationErrors::ScopedField field(errors,
                                          ".original_ip_detection_extensions");
      errors->AddError("must be empty");
    }
  }
  // common_http_protocol_options
  const envoy_config_core_v3_HttpProtocolOptions* options =
      envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_common_http_protocol_options(
          http_connection_manager_proto);
  if (options != nullptr) {
    // max_stream_duration
    const google_protobuf_Duration* duration =
        envoy_config_core_v3_HttpProtocolOptions_max_stream_duration(options);
    if (duration != nullptr) {
      ValidationErrors::ScopedField field(
          errors, ".common_http_protocol_options.max_stream_duration");
      http_connection_manager.http_max_stream_duration =
          ParseDuration(duration, errors);
    }
  }
  // http_filters
  {
    ValidationErrors::ScopedField field(errors, ".http_filters");
    const auto& http_filter_registry =
        static_cast<const GrpcXdsBootstrap&>(context.client->bootstrap())
            .http_filter_registry();
    size_t num_filters = 0;
    const auto* http_filters =
        envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_http_filters(
            http_connection_manager_proto, &num_filters);
    std::set<absl::string_view> names_seen;
    const size_t original_error_size = errors->size();
    for (size_t i = 0; i < num_filters; ++i) {
      ValidationErrors::ScopedField field(errors, absl::StrCat("[", i, "]"));
      const auto* http_filter = http_filters[i];
      // name
      absl::string_view name = UpbStringToAbsl(
          envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_name(
              http_filter));
      {
        ValidationErrors::ScopedField field(errors, ".name");
        if (name.empty()) {
          errors->AddError("empty filter name");
          continue;
        }
        if (names_seen.find(name) != names_seen.end()) {
          errors->AddError(absl::StrCat("duplicate HTTP filter name: ", name));
          continue;
        }
      }
      names_seen.insert(name);
      // is_optional
      const bool is_optional =
          envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_is_optional(
              http_filter);
      // typed_config
      {
        ValidationErrors::ScopedField field(errors, ".typed_config");
        const google_protobuf_Any* typed_config =
            envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_typed_config(
                http_filter);
        auto extension = ExtractXdsExtension(context, typed_config, errors);
        if (!extension.has_value()) continue;
        const XdsHttpFilterImpl* filter_impl =
            http_filter_registry.GetFilterForType(extension->type);
        if (filter_impl == nullptr) {
          if (!is_optional) errors->AddError("unsupported filter type");
          continue;
        }
        if ((is_client && !filter_impl->IsSupportedOnClients()) ||
            (!is_client && !filter_impl->IsSupportedOnServers())) {
          if (!is_optional) {
            errors->AddError(absl::StrCat("filter is not supported on ",
                                          is_client ? "clients" : "servers"));
          }
          continue;
        }
        absl::optional<XdsHttpFilterImpl::FilterConfig> filter_config =
            filter_impl->GenerateFilterConfig(name, context,
                                              std::move(*extension), errors);
        if (filter_config.has_value()) {
          http_connection_manager.http_filters.emplace_back(
              XdsListenerResource::HttpConnectionManager::HttpFilter{
                  std::string(name), std::move(*filter_config)});
        }
      }
    }
    if (errors->size() == original_error_size &&
        http_connection_manager.http_filters.empty()) {
      errors->AddError("expected at least one HTTP filter");
    }
    // Make sure that the last filter is terminal and non-last filters are
    // non-terminal. Note that this check is being performed in a separate loop
    // to take care of the case where there are two terminal filters in the list
    // out of which only one gets added in the final list.
    for (const auto& http_filter : http_connection_manager.http_filters) {
      const XdsHttpFilterImpl* filter_impl =
          http_filter_registry.GetFilterForType(
              http_filter.config.config_proto_type_name);
      if (&http_filter != &http_connection_manager.http_filters.back()) {
        // Filters before the last filter must not be terminal.
        if (filter_impl->IsTerminalFilter()) {
          errors->AddError(
              absl::StrCat("terminal filter for config type ",
                           http_filter.config.config_proto_type_name,
                           " must be the last filter in the chain"));
        }
      } else {
        // The last filter must be terminal.
        if (!filter_impl->IsTerminalFilter()) {
          errors->AddError(
              absl::StrCat("non-terminal filter for config type ",
                           http_filter.config.config_proto_type_name,
                           " is the last filter in the chain"));
        }
      }
    }
  }
  // Found inlined route_config. Parse it to find the cluster_name.
  if (envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_has_route_config(
          http_connection_manager_proto)) {
    const envoy_config_route_v3_RouteConfiguration* route_config =
        envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_route_config(
            http_connection_manager_proto);
    ValidationErrors::ScopedField field(errors, ".route_config");
    http_connection_manager.route_config =
        XdsRouteConfigResourceParse(context, route_config, errors);
  } else {
    // Validate that RDS must be used to get the route_config dynamically.
    const envoy_extensions_filters_network_http_connection_manager_v3_Rds* rds =
        envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_rds(
            http_connection_manager_proto);
    if (rds == nullptr) {
      errors->AddError("neither route_config nor rds fields are present");
    } else {
      // Get the route_config_name.
      http_connection_manager.route_config = UpbStringToStdString(
          envoy_extensions_filters_network_http_connection_manager_v3_Rds_route_config_name(
              rds));
      // Check that the ConfigSource specifies ADS.
      const envoy_config_core_v3_ConfigSource* config_source =
          envoy_extensions_filters_network_http_connection_manager_v3_Rds_config_source(
              rds);
      ValidationErrors::ScopedField field(errors, ".rds.config_source");
      if (config_source == nullptr) {
        errors->AddError("field not present");
      } else if (!envoy_config_core_v3_ConfigSource_has_ads(config_source) &&
                 !envoy_config_core_v3_ConfigSource_has_self(config_source)) {
        errors->AddError("ConfigSource does not specify ADS or SELF");
      }
    }
  }
  return http_connection_manager;
}

absl::StatusOr<std::shared_ptr<const XdsListenerResource>>
LdsResourceParseClient(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_listener_v3_ApiListener* api_listener) {
  auto lds_update = std::make_shared<XdsListenerResource>();
  ValidationErrors errors;
  ValidationErrors::ScopedField field(&errors, "api_listener.api_listener");
  auto* api_listener_field =
      envoy_config_listener_v3_ApiListener_api_listener(api_listener);
  auto extension = ExtractXdsExtension(context, api_listener_field, &errors);
  if (extension.has_value()) {
    lds_update->listener = HttpConnectionManagerParse(
        /*is_client=*/true, context, std::move(*extension), &errors);
  }
  if (!errors.ok()) {
    return errors.status(absl::StatusCode::kInvalidArgument,
                         "errors validating ApiListener");
  }
  return std::move(lds_update);
}

XdsListenerResource::DownstreamTlsContext DownstreamTlsContextParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_core_v3_TransportSocket* transport_socket,
    ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".typed_config");
  const auto* typed_config =
      envoy_config_core_v3_TransportSocket_typed_config(transport_socket);
  auto extension = ExtractXdsExtension(context, typed_config, errors);
  if (!extension.has_value()) return {};
  if (extension->type !=
      "envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext") {
    ValidationErrors::ScopedField field(errors, ".type_url");
    errors->AddError("unsupported transport socket type");
    return {};
  }
  absl::string_view* serialized_downstream_tls_context =
      absl::get_if<absl::string_view>(&extension->value);
  if (serialized_downstream_tls_context == nullptr) {
    errors->AddError("can't decode DownstreamTlsContext");
    return {};
  }
  const auto* downstream_tls_context_proto =
      envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_parse(
          serialized_downstream_tls_context->data(),
          serialized_downstream_tls_context->size(), context.arena);
  if (downstream_tls_context_proto == nullptr) {
    errors->AddError("can't decode DownstreamTlsContext");
    return {};
  }
  XdsListenerResource::DownstreamTlsContext downstream_tls_context;
  auto* common_tls_context =
      envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_common_tls_context(
          downstream_tls_context_proto);
  if (common_tls_context != nullptr) {
    ValidationErrors::ScopedField field(errors, ".common_tls_context");
    downstream_tls_context.common_tls_context =
        CommonTlsContextParse(context, common_tls_context, errors);
    // Note: We can't be more specific about the field names for these
    // errors, because we don't know which fields they were found in
    // inside of CommonTlsContext, so we make the error message a bit
    // more verbose to compensate.
    if (absl::holds_alternative<
            CommonTlsContext::CertificateValidationContext::SystemRootCerts>(
            downstream_tls_context.common_tls_context
                .certificate_validation_context.ca_certs)) {
      errors->AddError("system_root_certs not supported");
    }
    if (!downstream_tls_context.common_tls_context
             .certificate_validation_context.match_subject_alt_names.empty()) {
      errors->AddError("match_subject_alt_names not supported on servers");
    }
  }
  // Note: We can't be more specific about the field name for this
  // error, because we don't know which fields they were found in
  // inside of CommonTlsContext, so we make the error message a bit
  // more verbose to compensate.
  if (downstream_tls_context.common_tls_context
          .tls_certificate_provider_instance.instance_name.empty()) {
    errors->AddError(
        "TLS configuration provided but no "
        "tls_certificate_provider_instance found");
  }
  auto* require_client_certificate =
      envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_require_client_certificate(
          downstream_tls_context_proto);
  if (require_client_certificate != nullptr) {
    downstream_tls_context.require_client_certificate =
        google_protobuf_BoolValue_value(require_client_certificate);
    if (downstream_tls_context.require_client_certificate) {
      auto* ca_cert_provider =
          absl::get_if<CommonTlsContext::CertificateProviderPluginInstance>(
              &downstream_tls_context.common_tls_context
                   .certificate_validation_context.ca_certs);
      if (ca_cert_provider == nullptr ||
          ca_cert_provider->instance_name.empty()) {
        ValidationErrors::ScopedField field(errors,
                                            ".require_client_certificate");
        errors->AddError(
            "client certificate required but no certificate provider "
            "instance specified for validation");
      }
    }
  }
  if (ParseBoolValue(
          envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_require_sni(
              downstream_tls_context_proto))) {
    ValidationErrors::ScopedField field(errors, ".require_sni");
    errors->AddError("field unsupported");
  }
  if (envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_ocsp_staple_policy(
          downstream_tls_context_proto) !=
      envoy_extensions_transport_sockets_tls_v3_DownstreamTlsContext_LENIENT_STAPLING) {
    ValidationErrors::ScopedField field(errors, ".ocsp_staple_policy");
    errors->AddError("value must be LENIENT_STAPLING");
  }
  return downstream_tls_context;
}

absl::optional<XdsListenerResource::FilterChainMap::CidrRange> CidrRangeParse(
    const envoy_config_core_v3_CidrRange* cidr_range_proto,
    ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".address_prefix");
  XdsListenerResource::FilterChainMap::CidrRange cidr_range;
  std::string address_prefix = UpbStringToStdString(
      envoy_config_core_v3_CidrRange_address_prefix(cidr_range_proto));
  auto address = StringToSockaddr(address_prefix, /*port=*/0);
  if (!address.ok()) {
    errors->AddError(address.status().message());
    return absl::nullopt;
  }
  cidr_range.address = *address;
  cidr_range.prefix_len = 0;
  auto value = ParseUInt32Value(
      envoy_config_core_v3_CidrRange_prefix_len(cidr_range_proto));
  if (value.has_value()) {
    cidr_range.prefix_len = std::min(
        *value,
        (reinterpret_cast<const grpc_sockaddr*>(cidr_range.address.addr))
                    ->sa_family == GRPC_AF_INET
            ? uint32_t{32}
            : uint32_t{128});
  }
  // Normalize the network address by masking it with prefix_len
  grpc_sockaddr_mask_bits(&cidr_range.address, cidr_range.prefix_len);
  return cidr_range;
}

absl::optional<FilterChain::FilterChainMatch> FilterChainMatchParse(
    const envoy_config_listener_v3_FilterChainMatch* filter_chain_match_proto,
    ValidationErrors* errors) {
  FilterChain::FilterChainMatch filter_chain_match;
  const size_t original_error_size = errors->size();
  // destination_port
  auto destination_port = ParseUInt32Value(
      envoy_config_listener_v3_FilterChainMatch_destination_port(
          filter_chain_match_proto));
  if (destination_port.has_value()) {
    filter_chain_match.destination_port = *destination_port;
  }
  // prefix_ranges
  size_t size = 0;
  auto* prefix_ranges = envoy_config_listener_v3_FilterChainMatch_prefix_ranges(
      filter_chain_match_proto, &size);
  filter_chain_match.prefix_ranges.reserve(size);
  for (size_t i = 0; i < size; i++) {
    ValidationErrors::ScopedField field(
        errors, absl::StrCat(".prefix_ranges[", i, "]"));
    auto cidr_range = CidrRangeParse(prefix_ranges[i], errors);
    if (cidr_range.has_value()) {
      filter_chain_match.prefix_ranges.push_back(*cidr_range);
    }
  }
  // source_type
  filter_chain_match.source_type =
      static_cast<XdsListenerResource::FilterChainMap::ConnectionSourceType>(
          envoy_config_listener_v3_FilterChainMatch_source_type(
              filter_chain_match_proto));
  // source_prefix_ranges
  auto* source_prefix_ranges =
      envoy_config_listener_v3_FilterChainMatch_source_prefix_ranges(
          filter_chain_match_proto, &size);
  filter_chain_match.source_prefix_ranges.reserve(size);
  for (size_t i = 0; i < size; i++) {
    ValidationErrors::ScopedField field(
        errors, absl::StrCat(".source_prefix_ranges[", i, "]"));
    auto cidr_range = CidrRangeParse(source_prefix_ranges[i], errors);
    if (cidr_range.has_value()) {
      filter_chain_match.source_prefix_ranges.push_back(*cidr_range);
    }
  }
  // source_ports
  auto* source_ports = envoy_config_listener_v3_FilterChainMatch_source_ports(
      filter_chain_match_proto, &size);
  filter_chain_match.source_ports.reserve(size);
  for (size_t i = 0; i < size; i++) {
    filter_chain_match.source_ports.push_back(source_ports[i]);
  }
  // server_names
  auto* server_names = envoy_config_listener_v3_FilterChainMatch_server_names(
      filter_chain_match_proto, &size);
  for (size_t i = 0; i < size; i++) {
    filter_chain_match.server_names.push_back(
        UpbStringToStdString(server_names[i]));
  }
  // transport_protocol
  filter_chain_match.transport_protocol = UpbStringToStdString(
      envoy_config_listener_v3_FilterChainMatch_transport_protocol(
          filter_chain_match_proto));
  // application_protocols
  auto* application_protocols =
      envoy_config_listener_v3_FilterChainMatch_application_protocols(
          filter_chain_match_proto, &size);
  for (size_t i = 0; i < size; i++) {
    filter_chain_match.application_protocols.push_back(
        UpbStringToStdString(application_protocols[i]));
  }
  // Return result.
  if (errors->size() != original_error_size) return absl::nullopt;
  return filter_chain_match;
}

absl::optional<FilterChain> FilterChainParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_listener_v3_FilterChain* filter_chain_proto,
    ValidationErrors* errors) {
  FilterChain filter_chain;
  const size_t original_error_size = errors->size();
  // filter_chain_match
  auto* filter_chain_match =
      envoy_config_listener_v3_FilterChain_filter_chain_match(
          filter_chain_proto);
  if (filter_chain_match != nullptr) {
    ValidationErrors::ScopedField field(errors, ".filter_chain_match");
    auto match = FilterChainMatchParse(filter_chain_match, errors);
    if (match.has_value()) {
      filter_chain.filter_chain_match = std::move(*match);
    }
  }
  // filters
  {
    ValidationErrors::ScopedField field(errors, ".filters");
    filter_chain.filter_chain_data =
        std::make_shared<XdsListenerResource::FilterChainData>();
    size_t size = 0;
    auto* filters =
        envoy_config_listener_v3_FilterChain_filters(filter_chain_proto, &size);
    if (size != 1) {
      errors->AddError(
          "must have exactly one filter (HttpConnectionManager -- "
          "no other filter is supported at the moment)");
    }
    // entries in filters list
    for (size_t i = 0; i < size; ++i) {
      ValidationErrors::ScopedField field(
          errors, absl::StrCat("[", i, "].typed_config"));
      auto* typed_config =
          envoy_config_listener_v3_Filter_typed_config(filters[i]);
      auto extension = ExtractXdsExtension(context, typed_config, errors);
      if (extension.has_value()) {
        filter_chain.filter_chain_data->http_connection_manager =
            HttpConnectionManagerParse(/*is_client=*/false, context,
                                       std::move(*extension), errors);
      }
    }
  }
  // transport_socket
  auto* transport_socket =
      envoy_config_listener_v3_FilterChain_transport_socket(filter_chain_proto);
  if (transport_socket != nullptr) {
    ValidationErrors::ScopedField field(errors, ".transport_socket");
    filter_chain.filter_chain_data->downstream_tls_context =
        DownstreamTlsContextParse(context, transport_socket, errors);
  }
  // Return result.
  if (errors->size() != original_error_size) return absl::nullopt;
  return filter_chain;
}

absl::optional<std::string> AddressParse(
    const envoy_config_core_v3_Address* address_proto,
    ValidationErrors* errors) {
  if (address_proto == nullptr) {
    errors->AddError("field not present");
    return absl::nullopt;
  }
  ValidationErrors::ScopedField field(errors, ".socket_address");
  const auto* socket_address =
      envoy_config_core_v3_Address_socket_address(address_proto);
  if (socket_address == nullptr) {
    errors->AddError("field not present");
    return absl::nullopt;
  }
  {
    ValidationErrors::ScopedField field(errors, ".protocol");
    if (envoy_config_core_v3_SocketAddress_protocol(socket_address) !=
        envoy_config_core_v3_SocketAddress_TCP) {
      errors->AddError("value must be TCP");
    }
  }
  ValidationErrors::ScopedField field2(errors, ".port_value");
  uint32_t port = envoy_config_core_v3_SocketAddress_port_value(socket_address);
  if (port > 65535) {
    errors->AddError("invalid port");
    return absl::nullopt;
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

void AddFilterChainDataForSourcePort(
    const FilterChain& filter_chain, uint32_t port,
    XdsListenerResource::FilterChainMap::SourcePortsMap* ports_map,
    ValidationErrors* errors) {
  auto insert_result = ports_map->emplace(
      port, XdsListenerResource::FilterChainMap::FilterChainDataSharedPtr{
                filter_chain.filter_chain_data});
  if (!insert_result.second) {
    errors->AddError(absl::StrCat(
        "duplicate matching rules detected when adding filter chain: ",
        filter_chain.filter_chain_match.ToString()));
  }
}

void AddFilterChainDataForSourcePorts(
    const FilterChain& filter_chain,
    XdsListenerResource::FilterChainMap::SourcePortsMap* ports_map,
    ValidationErrors* errors) {
  if (filter_chain.filter_chain_match.source_ports.empty()) {
    AddFilterChainDataForSourcePort(filter_chain, 0, ports_map, errors);
  } else {
    for (uint32_t port : filter_chain.filter_chain_match.source_ports) {
      AddFilterChainDataForSourcePort(filter_chain, port, ports_map, errors);
    }
  }
}

void AddFilterChainDataForSourceIpRange(
    const FilterChain& filter_chain,
    InternalFilterChainMap::SourceIpMap* source_ip_map,
    ValidationErrors* errors) {
  if (filter_chain.filter_chain_match.source_prefix_ranges.empty()) {
    auto insert_result = source_ip_map->emplace(
        "", XdsListenerResource::FilterChainMap::SourceIp());
    AddFilterChainDataForSourcePorts(
        filter_chain, &insert_result.first->second.ports_map, errors);
  } else {
    for (const auto& prefix_range :
         filter_chain.filter_chain_match.source_prefix_ranges) {
      auto addr_str = grpc_sockaddr_to_string(&prefix_range.address, false);
      if (!addr_str.ok()) {
        errors->AddError(absl::StrCat(
            "error parsing source IP sockaddr (should not happen): ",
            addr_str.status().message()));
        continue;
      }
      auto insert_result = source_ip_map->emplace(
          absl::StrCat(*addr_str, "/", prefix_range.prefix_len),
          XdsListenerResource::FilterChainMap::SourceIp());
      if (insert_result.second) {
        insert_result.first->second.prefix_range.emplace(prefix_range);
      }
      AddFilterChainDataForSourcePorts(
          filter_chain, &insert_result.first->second.ports_map, errors);
    }
  }
}

void AddFilterChainDataForSourceType(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIp* destination_ip,
    ValidationErrors* errors) {
  CHECK(static_cast<unsigned int>(filter_chain.filter_chain_match.source_type) <
        3u);
  AddFilterChainDataForSourceIpRange(
      filter_chain,
      &destination_ip->source_types_array[static_cast<int>(
          filter_chain.filter_chain_match.source_type)],
      errors);
}

void AddFilterChainDataForApplicationProtocols(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIp* destination_ip,
    ValidationErrors* errors) {
  // Only allow filter chains that do not mention application protocols
  if (filter_chain.filter_chain_match.application_protocols.empty()) {
    AddFilterChainDataForSourceType(filter_chain, destination_ip, errors);
  }
}

void AddFilterChainDataForTransportProtocol(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIp* destination_ip,
    ValidationErrors* errors) {
  const std::string& transport_protocol =
      filter_chain.filter_chain_match.transport_protocol;
  // Only allow filter chains with no transport protocol or "raw_buffer"
  if (!transport_protocol.empty() && transport_protocol != "raw_buffer") {
    return;
  }
  // If for this configuration, we've already seen filter chains that mention
  // the transport protocol as "raw_buffer", we will never match filter chains
  // that do not mention it.
  if (destination_ip->transport_protocol_raw_buffer_provided &&
      transport_protocol.empty()) {
    return;
  }
  if (!transport_protocol.empty() &&
      !destination_ip->transport_protocol_raw_buffer_provided) {
    destination_ip->transport_protocol_raw_buffer_provided = true;
    // Clear out the previous entries if any since those entries did not mention
    // "raw_buffer"
    destination_ip->source_types_array =
        InternalFilterChainMap::ConnectionSourceTypesArray();
  }
  AddFilterChainDataForApplicationProtocols(filter_chain, destination_ip,
                                            errors);
}

void AddFilterChainDataForServerNames(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIp* destination_ip,
    ValidationErrors* errors) {
  // Don't continue adding filter chains with server names mentioned
  if (filter_chain.filter_chain_match.server_names.empty()) {
    AddFilterChainDataForTransportProtocol(filter_chain, destination_ip,
                                           errors);
  }
}

void AddFilterChainDataForDestinationIpRange(
    const FilterChain& filter_chain,
    InternalFilterChainMap::DestinationIpMap* destination_ip_map,
    ValidationErrors* errors) {
  if (filter_chain.filter_chain_match.prefix_ranges.empty()) {
    auto insert_result = destination_ip_map->emplace(
        "", InternalFilterChainMap::DestinationIp());
    AddFilterChainDataForServerNames(filter_chain, &insert_result.first->second,
                                     errors);
  } else {
    for (const auto& prefix_range :
         filter_chain.filter_chain_match.prefix_ranges) {
      auto addr_str = grpc_sockaddr_to_string(&prefix_range.address, false);
      if (!addr_str.ok()) {
        errors->AddError(absl::StrCat(
            "error parsing destination IP sockaddr (should not happen): ",
            addr_str.status().message()));
        continue;
      }
      auto insert_result = destination_ip_map->emplace(
          absl::StrCat(*addr_str, "/", prefix_range.prefix_len),
          InternalFilterChainMap::DestinationIp());
      if (insert_result.second) {
        insert_result.first->second.prefix_range.emplace(prefix_range);
      }
      AddFilterChainDataForServerNames(filter_chain,
                                       &insert_result.first->second, errors);
    }
  }
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

XdsListenerResource::FilterChainMap BuildFilterChainMap(
    const std::vector<FilterChain>& filter_chains, ValidationErrors* errors) {
  InternalFilterChainMap internal_filter_chain_map;
  for (const auto& filter_chain : filter_chains) {
    // Discard filter chain entries that specify destination port
    if (filter_chain.filter_chain_match.destination_port != 0) continue;
    AddFilterChainDataForDestinationIpRange(
        filter_chain, &internal_filter_chain_map.destination_ip_map, errors);
  }
  return BuildFromInternalFilterChainMap(&internal_filter_chain_map);
}

absl::StatusOr<std::shared_ptr<const XdsListenerResource>>
LdsResourceParseServer(const XdsResourceType::DecodeContext& context,
                       const envoy_config_listener_v3_Listener* listener) {
  ValidationErrors errors;
  XdsListenerResource::TcpListener tcp_listener;
  // address
  {
    ValidationErrors::ScopedField field(&errors, "address");
    auto address = AddressParse(
        envoy_config_listener_v3_Listener_address(listener), &errors);
    if (address.has_value()) tcp_listener.address = std::move(*address);
  }
  // use_original_dst
  if (ParseBoolValue(
          envoy_config_listener_v3_Listener_use_original_dst(listener))) {
    ValidationErrors::ScopedField field(&errors, "use_original_dst");
    errors.AddError("field not supported");
  }
  // filter_chains
  size_t num_filter_chains = 0;
  {
    ValidationErrors::ScopedField field(&errors, "filter_chains");
    auto* filter_chains = envoy_config_listener_v3_Listener_filter_chains(
        listener, &num_filter_chains);
    std::vector<FilterChain> parsed_filter_chains;
    parsed_filter_chains.reserve(num_filter_chains);
    for (size_t i = 0; i < num_filter_chains; i++) {
      ValidationErrors::ScopedField field(&errors, absl::StrCat("[", i, "]"));
      auto filter_chain = FilterChainParse(context, filter_chains[i], &errors);
      if (filter_chain.has_value()) {
        parsed_filter_chains.push_back(std::move(*filter_chain));
      }
    }
    tcp_listener.filter_chain_map =
        BuildFilterChainMap(parsed_filter_chains, &errors);
  }
  // default_filter_chain
  {
    ValidationErrors::ScopedField field(&errors, "default_filter_chain");
    auto* default_filter_chain =
        envoy_config_listener_v3_Listener_default_filter_chain(listener);
    if (default_filter_chain != nullptr) {
      auto filter_chain =
          FilterChainParse(context, default_filter_chain, &errors);
      if (filter_chain.has_value() &&
          filter_chain->filter_chain_data != nullptr) {
        tcp_listener.default_filter_chain =
            std::move(*filter_chain->filter_chain_data);
      }
    } else if (num_filter_chains == 0) {
      // Make sure that there is at least one filter chain to use.
      errors.AddError("must be set if filter_chains is unset");
    }
  }
  // Return result.
  if (!errors.ok()) {
    return errors.status(absl::StatusCode::kInvalidArgument,
                         "errors validating server Listener");
  }
  auto lds_update = std::make_shared<XdsListenerResource>();
  lds_update->listener = std::move(tcp_listener);
  return lds_update;
}

absl::StatusOr<std::shared_ptr<const XdsListenerResource>> LdsResourceParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_listener_v3_Listener* listener) {
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
    return LdsResourceParseClient(context, api_listener);
  }
  return LdsResourceParseServer(context, listener);
}

void MaybeLogListener(const XdsResourceType::DecodeContext& context,
                      const envoy_config_listener_v3_Listener* listener) {
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*context.tracer) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_config_listener_v3_Listener_getmsgdef(context.symtab);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(listener), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << context.client << "] Listener: " << buf;
  }
}

}  // namespace

XdsResourceType::DecodeResult XdsListenerResourceType::Decode(
    const XdsResourceType::DecodeContext& context,
    absl::string_view serialized_resource) const {
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
  auto listener = LdsResourceParse(context, resource);
  if (!listener.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED_OBJ(*context.tracer)) {
      LOG(ERROR) << "[xds_client " << context.client << "] invalid Listener "
                 << *result.name << ": " << listener.status();
    }
    result.resource = listener.status();
  } else {
    if (GRPC_TRACE_FLAG_ENABLED_OBJ(*context.tracer)) {
      LOG(INFO) << "[xds_client " << context.client << "] parsed Listener "
                << *result.name << ": " << (*listener)->ToString();
    }
    result.resource = std::move(*listener);
  }
  return result;
}

}  // namespace grpc_core
