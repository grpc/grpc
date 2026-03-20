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

#include "src/core/xds/grpc/xds_listener.h"

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/util/match.h"
#include "src/core/util/string.h"

namespace grpc_core {

//
// XdsListenerResource::HttpConnectionManager
//

std::string XdsListenerResource::HttpConnectionManager::ToString() const {
  std::string result = "{";
  Match(
      route_config,
      [&](const std::string& rds_name) {
        StrAppend(result, "rds_name=");
        StrAppend(result, rds_name);
      },
      [&](const std::shared_ptr<const XdsRouteConfigResource>& route_config) {
        StrAppend(result, "route_config=");
        StrAppend(result, route_config->ToString());
      });
  StrAppend(result, ", http_max_stream_duration=");
  StrAppend(result, http_max_stream_duration.ToString());
  if (!http_filters.empty()) {
    StrAppend(result, ", http_filters=[");
    bool is_first = true;
    for (const auto& http_filter : http_filters) {
      if (!is_first) StrAppend(result, ", ");
      StrAppend(result, http_filter.ToString());
      is_first = false;
    }
    StrAppend(result, "]");
  }
  StrAppend(result, "}");
  return result;
}

//
// XdsListenerResource::HttpConnectionManager::HttpFilter
//

std::string XdsListenerResource::HttpConnectionManager::HttpFilter::ToString()
    const {
  std::string result = "{name=";
  StrAppend(result, name);
  StrAppend(result, ", config_proto_type=");
  StrAppend(result, config_proto_type);
  StrAppend(result, ", config=");
  StrAppend(result, JsonDump(config));
  StrAppend(result, ", filter_config=");
  StrAppend(result, filter_config == nullptr ? "null" : filter_config->ToString());
  StrAppend(result, "}");
  return result;
}

//
// XdsListenerResource::DownstreamTlsContext
//

std::string XdsListenerResource::DownstreamTlsContext::ToString() const {
  std::string result = "common_tls_context=";
  StrAppend(result, common_tls_context.ToString());
  StrAppend(result, ", require_client_certificate=");
  StrAppend(result, require_client_certificate ? "true" : "false");
  return result;
}

bool XdsListenerResource::DownstreamTlsContext::Empty() const {
  return common_tls_context.Empty();
}

//
// XdsListenerResource::FilterChainData
//

std::string XdsListenerResource::FilterChainData::ToString() const {
  std::string result = "{downstream_tls_context=";
  StrAppend(result, downstream_tls_context.ToString());
  StrAppend(result, " http_connection_manager=");
  StrAppend(result, http_connection_manager.ToString());
  StrAppend(result, "}");
  return result;
}

//
// XdsListenerResource::FilterChainMap::CidrRange
//

std::string XdsListenerResource::FilterChainMap::CidrRange::ToString() const {
  std::string result = "{address_prefix=";
  auto addr_str = grpc_sockaddr_to_string(&address, false);
  StrAppend(result,
            addr_str.ok() ? addr_str.value() : addr_str.status().ToString());
  StrAppend(result, ", prefix_len=");
  StrAppend(result, std::to_string(prefix_len));
  StrAppend(result, "}");
  return result;
}

//
// XdsListenerResource::FilterChainMap
//

std::string XdsListenerResource::FilterChainMap::ToString() const {
  std::string result = "{";
  bool is_first = true;
  for (const auto& destination_ip : destination_ip_vector) {
    for (int source_type = 0; source_type < 3; ++source_type) {
      for (const auto& source_ip :
           destination_ip.source_types_array[source_type]) {
        for (const auto& [port, filter_chain] : source_ip.ports_map) {
          if (!is_first) StrAppend(result, ", ");
          StrAppend(result, "{filter_chain_match={");
          bool match_is_first = true;
          if (destination_ip.prefix_range.has_value()) {
            StrAppend(result, "prefix_ranges={");
            StrAppend(result, destination_ip.prefix_range->ToString());
            StrAppend(result, "}");
            match_is_first = false;
          }
          if (static_cast<ConnectionSourceType>(source_type) ==
              ConnectionSourceType::kSameIpOrLoopback) {
            if (!match_is_first) StrAppend(result, ", ");
            StrAppend(result, "source_type=SAME_IP_OR_LOOPBACK");
            match_is_first = false;
          } else if (static_cast<ConnectionSourceType>(source_type) ==
                     ConnectionSourceType::kExternal) {
            if (!match_is_first) StrAppend(result, ", ");
            StrAppend(result, "source_type=EXTERNAL");
            match_is_first = false;
          }
          if (source_ip.prefix_range.has_value()) {
            if (!match_is_first) StrAppend(result, ", ");
            StrAppend(result, "source_prefix_ranges={");
            StrAppend(result, source_ip.prefix_range->ToString());
            StrAppend(result, "}");
            match_is_first = false;
          }
          if (port != 0) {
            if (!match_is_first) StrAppend(result, ", ");
            StrAppend(result, "source_ports={");
            StrAppend(result, std::to_string(port));
            StrAppend(result, "}");
            match_is_first = false;
          }
          StrAppend(result, "}, filter_chain=");
          StrAppend(result, filter_chain.data->ToString());
          StrAppend(result, "}");
          is_first = false;
        }
      }
    }
  }
  StrAppend(result, "}");
  return result;
}

//
// XdsListenerResource::TcpListener
//

std::string XdsListenerResource::TcpListener::ToString() const {
  std::string result = "{address=";
  StrAppend(result, address);
  StrAppend(result, ", filter_chain_map=");
  StrAppend(result, filter_chain_map.ToString());
  if (default_filter_chain.has_value()) {
    StrAppend(result, ", default_filter_chain=");
    StrAppend(result, default_filter_chain->ToString());
  }
  StrAppend(result, "}");
  return result;
}

//
// XdsListenerResource
//

std::string XdsListenerResource::ToString() const {
  std::string result = "{";
  Match(
      listener,
      [&](const HttpConnectionManager& hcm) {
        StrAppend(result, "http_connection_manager=");
        StrAppend(result, hcm.ToString());
      },
      [&](const TcpListener& tcp) {
        StrAppend(result, "tcp_listener=");
        StrAppend(result, tcp.ToString());
      });
  StrAppend(result, "}");
  return result;
}

}  // namespace grpc_core
