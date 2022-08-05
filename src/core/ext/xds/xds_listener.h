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

#ifndef GRPC_CORE_EXT_XDS_XDS_LISTENER_H
#define GRPC_CORE_EXT_XDS_XDS_LISTENER_H

#include <grpc/support/port_platform.h>

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "envoy/config/listener/v3/listener.upbdefs.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upbdefs.h"
#include "upb/def.h"

#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_http_filters.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/ext/xds/xds_resource_type_impl.h"
#include "src/core/ext/xds/xds_route_config.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/resolved_address.h"

namespace grpc_core {

// TODO(roth): When we can use absl::variant<>, consider using that
// here, to enforce the fact that only one of the two fields can be set.
struct XdsListenerResource {
  struct DownstreamTlsContext {
    CommonTlsContext common_tls_context;
    bool require_client_certificate = false;

    bool operator==(const DownstreamTlsContext& other) const {
      return common_tls_context == other.common_tls_context &&
             require_client_certificate == other.require_client_certificate;
    }

    std::string ToString() const;
    bool Empty() const;
  };

  enum class ListenerType {
    kTcpListener = 0,
    kHttpApiListener,
  } type;

  struct HttpConnectionManager {
    // The name to use in the RDS request.
    std::string route_config_name;
    // Storing the Http Connection Manager Common Http Protocol Option
    // max_stream_duration
    Duration http_max_stream_duration;
    // The RouteConfiguration to use for this listener.
    // Present only if it is inlined in the LDS response.
    absl::optional<XdsRouteConfigResource> rds_update;

    struct HttpFilter {
      std::string name;
      XdsHttpFilterImpl::FilterConfig config;

      bool operator==(const HttpFilter& other) const {
        return name == other.name && config == other.config;
      }

      std::string ToString() const;
    };
    std::vector<HttpFilter> http_filters;

    bool operator==(const HttpConnectionManager& other) const {
      return route_config_name == other.route_config_name &&
             http_max_stream_duration == other.http_max_stream_duration &&
             rds_update == other.rds_update &&
             http_filters == other.http_filters;
    }

    std::string ToString() const;
  };

  // Populated for type=kHttpApiListener.
  HttpConnectionManager http_connection_manager;

  // Populated for type=kTcpListener.
  // host:port listening_address set when type is kTcpListener
  std::string address;

  struct FilterChainData {
    DownstreamTlsContext downstream_tls_context;
    // This is in principle the filter list.
    // We currently require exactly one filter, which is the HCM.
    HttpConnectionManager http_connection_manager;

    bool operator==(const FilterChainData& other) const {
      return downstream_tls_context == other.downstream_tls_context &&
             http_connection_manager == other.http_connection_manager;
    }

    std::string ToString() const;
  };

  // A multi-level map used to determine which filter chain to use for a given
  // incoming connection. Determining the right filter chain for a given
  // connection checks the following properties, in order:
  // - destination port (never matched, so not present in map)
  // - destination IP address
  // - server name (never matched, so not present in map)
  // - transport protocol (allows only "raw_buffer" or unset, prefers the
  //   former, so only one of those two types is present in map)
  // - application protocol (never matched, so not present in map)
  // - connection source type (any, local or external)
  // - source IP address
  // - source port
  // https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/listener/v3/listener_components.proto#config-listener-v3-filterchainmatch
  // for more details
  struct FilterChainMap {
    struct FilterChainDataSharedPtr {
      std::shared_ptr<FilterChainData> data;
      bool operator==(const FilterChainDataSharedPtr& other) const {
        return *data == *other.data;
      }
    };
    struct CidrRange {
      grpc_resolved_address address;
      uint32_t prefix_len;

      bool operator==(const CidrRange& other) const {
        return memcmp(&address, &other.address, sizeof(address)) == 0 &&
               prefix_len == other.prefix_len;
      }

      std::string ToString() const;
    };
    using SourcePortsMap = std::map<uint16_t, FilterChainDataSharedPtr>;
    struct SourceIp {
      absl::optional<CidrRange> prefix_range;
      SourcePortsMap ports_map;

      bool operator==(const SourceIp& other) const {
        return prefix_range == other.prefix_range &&
               ports_map == other.ports_map;
      }
    };
    using SourceIpVector = std::vector<SourceIp>;
    enum class ConnectionSourceType { kAny = 0, kSameIpOrLoopback, kExternal };
    using ConnectionSourceTypesArray = std::array<SourceIpVector, 3>;
    struct DestinationIp {
      absl::optional<CidrRange> prefix_range;
      // We always fail match on server name, so those filter chains are not
      // included here.
      ConnectionSourceTypesArray source_types_array;

      bool operator==(const DestinationIp& other) const {
        return prefix_range == other.prefix_range &&
               source_types_array == other.source_types_array;
      }
    };
    // We always fail match on destination ports map
    using DestinationIpVector = std::vector<DestinationIp>;
    DestinationIpVector destination_ip_vector;

    bool operator==(const FilterChainMap& other) const {
      return destination_ip_vector == other.destination_ip_vector;
    }

    std::string ToString() const;
  } filter_chain_map;

  absl::optional<FilterChainData> default_filter_chain;

  bool operator==(const XdsListenerResource& other) const {
    return http_connection_manager == other.http_connection_manager &&
           address == other.address &&
           filter_chain_map == other.filter_chain_map &&
           default_filter_chain == other.default_filter_chain;
  }

  std::string ToString() const;
};

class XdsListenerResourceType
    : public XdsResourceTypeImpl<XdsListenerResourceType, XdsListenerResource> {
 public:
  absl::string_view type_url() const override {
    return "envoy.config.listener.v3.Listener";
  }
  absl::string_view v2_type_url() const override {
    return "envoy.api.v2.Listener";
  }

  absl::StatusOr<DecodeResult> Decode(
      const XdsResourceType::DecodeContext& context,
      absl::string_view serialized_resource, bool is_v2) const override;

  bool AllResourcesRequiredInSotW() const override { return true; }

  void InitUpbSymtab(upb_DefPool* symtab) const override {
    envoy_config_listener_v3_Listener_getmsgdef(symtab);
    envoy_extensions_filters_network_http_connection_manager_v3_HttpConnectionManager_getmsgdef(
        symtab);
    XdsHttpFilterRegistry::PopulateSymtab(symtab);
  }
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_LISTENER_H
