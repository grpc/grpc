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

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gprpp/match.h"

namespace grpc_core {

//
// XdsListenerResource::HttpConnectionManager
//

std::string XdsListenerResource::HttpConnectionManager::ToString() const {
  std::vector<std::string> contents;
  contents.push_back(Match(
      route_config,
      [](const std::string& rds_name) {
        return absl::StrCat("rds_name=", rds_name);
      },
      [](const std::shared_ptr<const XdsRouteConfigResource>& route_config) {
        return absl::StrCat("route_config=", route_config->ToString());
      }));
  contents.push_back(absl::StrCat("http_max_stream_duration=",
                                  http_max_stream_duration.ToString()));
  if (!http_filters.empty()) {
    std::vector<std::string> filter_strings;
    filter_strings.reserve(http_filters.size());
    for (const auto& http_filter : http_filters) {
      filter_strings.push_back(http_filter.ToString());
    }
    contents.push_back(absl::StrCat("http_filters=[",
                                    absl::StrJoin(filter_strings, ", "), "]"));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsListenerResource::HttpConnectionManager::HttpFilter
//

std::string XdsListenerResource::HttpConnectionManager::HttpFilter::ToString()
    const {
  return absl::StrCat("{name=", name, ", config=", config.ToString(), "}");
}

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
// XdsListenerResource::FilterChainMap
//

std::string XdsListenerResource::FilterChainMap::ToString() const {
  std::vector<std::string> contents;
  for (const auto& destination_ip : destination_ip_vector) {
    for (int source_type = 0; source_type < 3; ++source_type) {
      for (const auto& source_ip :
           destination_ip.source_types_array[source_type]) {
        for (const auto& source_port_pair : source_ip.ports_map) {
          std::vector<std::string> match_contents;
          if (destination_ip.prefix_range.has_value()) {
            match_contents.push_back(
                absl::StrCat("prefix_ranges={",
                             destination_ip.prefix_range->ToString(), "}"));
          }
          if (static_cast<ConnectionSourceType>(source_type) ==
              ConnectionSourceType::kSameIpOrLoopback) {
            match_contents.push_back("source_type=SAME_IP_OR_LOOPBACK");
          } else if (static_cast<ConnectionSourceType>(source_type) ==
                     ConnectionSourceType::kExternal) {
            match_contents.push_back("source_type=EXTERNAL");
          }
          if (source_ip.prefix_range.has_value()) {
            match_contents.push_back(
                absl::StrCat("source_prefix_ranges={",
                             source_ip.prefix_range->ToString(), "}"));
          }
          if (source_port_pair.first != 0) {
            match_contents.push_back(
                absl::StrCat("source_ports={", source_port_pair.first, "}"));
          }
          contents.push_back(absl::StrCat(
              "{filter_chain_match={", absl::StrJoin(match_contents, ", "),
              "}, filter_chain=", source_port_pair.second.data->ToString(),
              "}"));
        }
      }
    }
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsListenerResource::TcpListener
//

std::string XdsListenerResource::TcpListener::ToString() const {
  std::vector<std::string> contents;
  contents.push_back(absl::StrCat("address=", address));
  contents.push_back(
      absl::StrCat("filter_chain_map=", filter_chain_map.ToString()));
  if (default_filter_chain.has_value()) {
    contents.push_back(absl::StrCat("default_filter_chain=",
                                    default_filter_chain->ToString()));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsListenerResource
//

std::string XdsListenerResource::ToString() const {
  return Match(
      listener,
      [](const HttpConnectionManager& hcm) {
        return absl::StrCat("{http_connection_manager=", hcm.ToString(), "}");
      },
      [](const TcpListener& tcp) {
        return absl::StrCat("{tcp_listener=", tcp.ToString(), "}");
      });
}

}  // namespace grpc_core
