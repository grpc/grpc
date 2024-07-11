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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_CLUSTER_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_CLUSTER_H

#include <stdint.h>

#include <set>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "envoy/config/cluster/v3/cluster.upbdefs.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.upbdefs.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upbdefs.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.upbdefs.h"
#include "upb/reflection/def.h"

#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>

#include "src/core/load_balancing/outlier_detection/outlier_detection.h"
#include "src/core/util/json/json.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_health_status.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "src/core/xds/xds_client/xds_resource_type_impl.h"

namespace grpc_core {

struct XdsClusterResource : public XdsResourceType::ResourceData {
  struct Eds {
    // If empty, defaults to the cluster name.
    std::string eds_service_name;

    bool operator==(const Eds& other) const {
      return eds_service_name == other.eds_service_name;
    }
  };

  struct LogicalDns {
    // The hostname to lookup in DNS.
    std::string hostname;

    bool operator==(const LogicalDns& other) const {
      return hostname == other.hostname;
    }
  };

  struct Aggregate {
    // Prioritized list of cluster names.
    std::vector<std::string> prioritized_cluster_names;

    bool operator==(const Aggregate& other) const {
      return prioritized_cluster_names == other.prioritized_cluster_names;
    }
  };

  absl::variant<Eds, LogicalDns, Aggregate> type;

  // The LB policy to use for locality and endpoint picking.
  Json::Array lb_policy_config;

  // Note: Remaining fields are not used for aggregate clusters.

  // The LRS server to use for load reporting.
  // If not set, load reporting will be disabled.
  absl::optional<GrpcXdsBootstrap::GrpcXdsServer> lrs_load_reporting_server;

  // Tls Context used by clients
  CommonTlsContext common_tls_context;

  // Connection idle timeout.  Currently used only for SSA.
  Duration connection_idle_timeout = Duration::Hours(1);

  // Maximum number of outstanding requests can be made to the upstream
  // cluster.
  uint32_t max_concurrent_requests = 1024;

  absl::optional<OutlierDetectionConfig> outlier_detection;

  XdsHealthStatusSet override_host_statuses;

  RefCountedStringValue service_telemetry_label;
  RefCountedStringValue namespace_telemetry_label;

  bool operator==(const XdsClusterResource& other) const {
    return type == other.type && lb_policy_config == other.lb_policy_config &&
           lrs_load_reporting_server == other.lrs_load_reporting_server &&
           common_tls_context == other.common_tls_context &&
           connection_idle_timeout == other.connection_idle_timeout &&
           max_concurrent_requests == other.max_concurrent_requests &&
           outlier_detection == other.outlier_detection &&
           override_host_statuses == other.override_host_statuses &&
           service_telemetry_label == other.service_telemetry_label &&
           namespace_telemetry_label == other.namespace_telemetry_label;
  }

  std::string ToString() const;
};

class XdsClusterResourceType
    : public XdsResourceTypeImpl<XdsClusterResourceType, XdsClusterResource> {
 public:
  absl::string_view type_url() const override {
    return "envoy.config.cluster.v3.Cluster";
  }

  DecodeResult Decode(const XdsResourceType::DecodeContext& context,
                      absl::string_view serialized_resource) const override;

  bool AllResourcesRequiredInSotW() const override { return true; }

  void InitUpbSymtab(XdsClient*, upb_DefPool* symtab) const override {
    envoy_config_cluster_v3_Cluster_getmsgdef(symtab);
    envoy_extensions_clusters_aggregate_v3_ClusterConfig_getmsgdef(symtab);
    envoy_extensions_transport_sockets_tls_v3_UpstreamTlsContext_getmsgdef(
        symtab);
    envoy_extensions_upstreams_http_v3_HttpProtocolOptions_getmsgdef(symtab);
  }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_CLUSTER_H
