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

#include "src/core/ext/xds/xds_cluster.h"

#include <stddef.h>

#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/strip.h"
#include "envoy/config/cluster/v3/circuit_breaker.upb.h"
#include "envoy/config/cluster/v3/cluster.upb.h"
#include "envoy/config/cluster/v3/cluster.upbdefs.h"
#include "envoy/config/cluster/v3/outlier_detection.upb.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/config_source.upb.h"
#include "envoy/config/endpoint/v3/endpoint.upb.h"
#include "envoy/config/endpoint/v3/endpoint_components.upb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.upb.h"
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
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_core {

//
// XdsClusterResource
//

std::string XdsClusterResource::ToString() const {
  std::vector<std::string> contents;
  switch (cluster_type) {
    case EDS:
      contents.push_back("cluster_type=EDS");
      if (!eds_service_name.empty()) {
        contents.push_back(absl::StrCat("eds_service_name=", eds_service_name));
      }
      break;
    case LOGICAL_DNS:
      contents.push_back("cluster_type=LOGICAL_DNS");
      contents.push_back(absl::StrCat("dns_hostname=", dns_hostname));
      break;
    case AGGREGATE:
      contents.push_back("cluster_type=AGGREGATE");
      contents.push_back(
          absl::StrCat("prioritized_cluster_names=[",
                       absl::StrJoin(prioritized_cluster_names, ", "), "]"));
  }
  if (!common_tls_context.Empty()) {
    contents.push_back(
        absl::StrCat("common_tls_context=", common_tls_context.ToString()));
  }
  if (lrs_load_reporting_server.has_value()) {
    contents.push_back(absl::StrCat("lrs_load_reporting_server_name=",
                                    lrs_load_reporting_server->server_uri()));
  }
  contents.push_back(absl::StrCat("lb_policy=", lb_policy));
  if (lb_policy == "RING_HASH") {
    contents.push_back(absl::StrCat("min_ring_size=", min_ring_size));
    contents.push_back(absl::StrCat("max_ring_size=", max_ring_size));
  }
  contents.push_back(
      absl::StrCat("max_concurrent_requests=", max_concurrent_requests));
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsClusterResourceType
//

namespace {

absl::StatusOr<CommonTlsContext> UpstreamTlsContextParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_core_v3_TransportSocket* transport_socket) {
  auto* typed_config =
      envoy_config_core_v3_TransportSocket_typed_config(transport_socket);
  if (typed_config == nullptr) {
    return absl::InvalidArgumentError("transport_socket.typed_config not set");
  }
  absl::string_view type_url = absl::StripPrefix(
      UpbStringToAbsl(google_protobuf_Any_type_url(typed_config)),
      "type.googleapis.com/");
  if (type_url !=
      "envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext") {
    return absl::InvalidArgumentError(
        absl::StrCat("Unrecognized transport socket type: ", type_url));
  }
  const upb_StringView encoded_upstream_tls_context =
      google_protobuf_Any_value(typed_config);
  auto* upstream_tls_context =
      envoy_extensions_transport_sockets_tls_v3_UpstreamTlsContext_parse(
          encoded_upstream_tls_context.data, encoded_upstream_tls_context.size,
          context.arena);
  if (upstream_tls_context == nullptr) {
    return absl::InvalidArgumentError("Can't decode upstream tls context.");
  }
  auto* common_tls_context_proto =
      envoy_extensions_transport_sockets_tls_v3_UpstreamTlsContext_common_tls_context(
          upstream_tls_context);
  CommonTlsContext common_tls_context;
  if (common_tls_context_proto != nullptr) {
    auto common_context =
        CommonTlsContext::Parse(context, common_tls_context_proto);
    if (!common_context.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Error parsing UpstreamTlsContext: ",
                       common_context.status().message()));
    }
    common_tls_context = std::move(*common_context);
  }
  if (common_tls_context.certificate_validation_context
          .ca_certificate_provider_instance.instance_name.empty()) {
    return absl::InvalidArgumentError(
        "UpstreamTlsContext: TLS configuration provided but no "
        "ca_certificate_provider_instance found.");
  }
  return common_tls_context;
}

absl::Status CdsLogicalDnsParse(const envoy_config_cluster_v3_Cluster* cluster,
                                XdsClusterResource* cds_update) {
  const auto* load_assignment =
      envoy_config_cluster_v3_Cluster_load_assignment(cluster);
  if (load_assignment == nullptr) {
    return absl::InvalidArgumentError(
        "load_assignment not present for LOGICAL_DNS cluster");
  }
  size_t num_localities;
  const auto* const* localities =
      envoy_config_endpoint_v3_ClusterLoadAssignment_endpoints(load_assignment,
                                                               &num_localities);
  if (num_localities != 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("load_assignment for LOGICAL_DNS cluster must have "
                     "exactly one locality, found ",
                     num_localities));
  }
  size_t num_endpoints;
  const auto* const* endpoints =
      envoy_config_endpoint_v3_LocalityLbEndpoints_lb_endpoints(localities[0],
                                                                &num_endpoints);
  if (num_endpoints != 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("locality for LOGICAL_DNS cluster must have "
                     "exactly one endpoint, found ",
                     num_endpoints));
  }
  const auto* endpoint =
      envoy_config_endpoint_v3_LbEndpoint_endpoint(endpoints[0]);
  if (endpoint == nullptr) {
    return absl::InvalidArgumentError("LbEndpoint endpoint field not set");
  }
  const auto* address = envoy_config_endpoint_v3_Endpoint_address(endpoint);
  if (address == nullptr) {
    return absl::InvalidArgumentError("Endpoint address field not set");
  }
  const auto* socket_address =
      envoy_config_core_v3_Address_socket_address(address);
  if (socket_address == nullptr) {
    return absl::InvalidArgumentError("Address socket_address field not set");
  }
  if (envoy_config_core_v3_SocketAddress_resolver_name(socket_address).size !=
      0) {
    return absl::InvalidArgumentError(
        "LOGICAL_DNS clusters must NOT have a custom resolver name set");
  }
  absl::string_view address_str = UpbStringToAbsl(
      envoy_config_core_v3_SocketAddress_address(socket_address));
  if (address_str.empty()) {
    return absl::InvalidArgumentError("SocketAddress address field not set");
  }
  if (!envoy_config_core_v3_SocketAddress_has_port_value(socket_address)) {
    return absl::InvalidArgumentError("SocketAddress port_value field not set");
  }
  cds_update->dns_hostname = JoinHostPort(
      address_str,
      envoy_config_core_v3_SocketAddress_port_value(socket_address));
  return absl::OkStatus();
}

absl::StatusOr<XdsClusterResource> CdsResourceParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_cluster_v3_Cluster* cluster, bool /*is_v2*/) {
  XdsClusterResource cds_update;
  std::vector<std::string> errors;
  // Check the cluster_discovery_type.
  if (!envoy_config_cluster_v3_Cluster_has_type(cluster) &&
      !envoy_config_cluster_v3_Cluster_has_cluster_type(cluster)) {
    errors.emplace_back("DiscoveryType not found.");
  } else if (envoy_config_cluster_v3_Cluster_type(cluster) ==
             envoy_config_cluster_v3_Cluster_EDS) {
    cds_update.cluster_type = XdsClusterResource::ClusterType::EDS;
    // Check the EDS config source.
    const envoy_config_cluster_v3_Cluster_EdsClusterConfig* eds_cluster_config =
        envoy_config_cluster_v3_Cluster_eds_cluster_config(cluster);
    const envoy_config_core_v3_ConfigSource* eds_config =
        envoy_config_cluster_v3_Cluster_EdsClusterConfig_eds_config(
            eds_cluster_config);
    if (!envoy_config_core_v3_ConfigSource_has_ads(eds_config) &&
        !envoy_config_core_v3_ConfigSource_has_self(eds_config)) {
      errors.emplace_back("EDS ConfigSource is not ADS or SELF.");
    }
    // Record EDS service_name (if any).
    upb_StringView service_name =
        envoy_config_cluster_v3_Cluster_EdsClusterConfig_service_name(
            eds_cluster_config);
    if (service_name.size != 0) {
      cds_update.eds_service_name = UpbStringToStdString(service_name);
    }
  } else if (envoy_config_cluster_v3_Cluster_type(cluster) ==
             envoy_config_cluster_v3_Cluster_LOGICAL_DNS) {
    cds_update.cluster_type = XdsClusterResource::ClusterType::LOGICAL_DNS;
    absl::Status status = CdsLogicalDnsParse(cluster, &cds_update);
    if (!status.ok()) errors.emplace_back(status.message());
  } else {
    const auto* custom_cluster_type =
        envoy_config_cluster_v3_Cluster_cluster_type(cluster);
    if (custom_cluster_type == nullptr) {
      errors.push_back("DiscoveryType is not valid.");
    } else {
      const auto* typed_config =
          envoy_config_cluster_v3_Cluster_CustomClusterType_typed_config(
              custom_cluster_type);
      if (typed_config == nullptr) {
        errors.push_back("cluster_type.typed_config not set");
      } else {
        absl::string_view type_url = absl::StripPrefix(
            UpbStringToAbsl(google_protobuf_Any_type_url(typed_config)),
            "type.googleapis.com/");
        if (type_url !=
            "envoy.extensions.clusters.aggregate.v3.ClusterConfig") {
          errors.push_back(
              absl::StrCat("unknown cluster_type extension: ", type_url));
        } else {
          cds_update.cluster_type = XdsClusterResource::ClusterType::AGGREGATE;
          // Retrieve aggregate clusters.
          const upb_StringView aggregate_cluster_config_upb_stringview =
              google_protobuf_Any_value(typed_config);
          const auto* aggregate_cluster_config =
              envoy_extensions_clusters_aggregate_v3_ClusterConfig_parse(
                  aggregate_cluster_config_upb_stringview.data,
                  aggregate_cluster_config_upb_stringview.size, context.arena);
          if (aggregate_cluster_config == nullptr) {
            errors.emplace_back("Can't parse aggregate cluster.");
          } else {
            size_t size;
            const upb_StringView* clusters =
                envoy_extensions_clusters_aggregate_v3_ClusterConfig_clusters(
                    aggregate_cluster_config, &size);
            for (size_t i = 0; i < size; ++i) {
              const upb_StringView cluster = clusters[i];
              cds_update.prioritized_cluster_names.emplace_back(
                  UpbStringToStdString(cluster));
            }
          }
        }
      }
    }
  }
  // Check the LB policy.
  if (envoy_config_cluster_v3_Cluster_lb_policy(cluster) ==
      envoy_config_cluster_v3_Cluster_ROUND_ROBIN) {
    cds_update.lb_policy = "ROUND_ROBIN";
  } else if (envoy_config_cluster_v3_Cluster_lb_policy(cluster) ==
             envoy_config_cluster_v3_Cluster_RING_HASH) {
    cds_update.lb_policy = "RING_HASH";
    // Record ring hash lb config
    auto* ring_hash_config =
        envoy_config_cluster_v3_Cluster_ring_hash_lb_config(cluster);
    if (ring_hash_config != nullptr) {
      const google_protobuf_UInt64Value* max_ring_size =
          envoy_config_cluster_v3_Cluster_RingHashLbConfig_maximum_ring_size(
              ring_hash_config);
      if (max_ring_size != nullptr) {
        cds_update.max_ring_size =
            google_protobuf_UInt64Value_value(max_ring_size);
        if (cds_update.max_ring_size > 8388608 ||
            cds_update.max_ring_size == 0) {
          errors.emplace_back(
              "max_ring_size is not in the range of 1 to 8388608.");
        }
      }
      const google_protobuf_UInt64Value* min_ring_size =
          envoy_config_cluster_v3_Cluster_RingHashLbConfig_minimum_ring_size(
              ring_hash_config);
      if (min_ring_size != nullptr) {
        cds_update.min_ring_size =
            google_protobuf_UInt64Value_value(min_ring_size);
        if (cds_update.min_ring_size > 8388608 ||
            cds_update.min_ring_size == 0) {
          errors.emplace_back(
              "min_ring_size is not in the range of 1 to 8388608.");
        }
        if (cds_update.min_ring_size > cds_update.max_ring_size) {
          errors.emplace_back(
              "min_ring_size cannot be greater than max_ring_size.");
        }
      }
      if (envoy_config_cluster_v3_Cluster_RingHashLbConfig_hash_function(
              ring_hash_config) !=
          envoy_config_cluster_v3_Cluster_RingHashLbConfig_XX_HASH) {
        errors.emplace_back("ring hash lb config has invalid hash function.");
      }
    }
  } else {
    errors.emplace_back("LB policy is not supported.");
  }
  auto* transport_socket =
      envoy_config_cluster_v3_Cluster_transport_socket(cluster);
  if (transport_socket != nullptr) {
    auto common_tls_context =
        UpstreamTlsContextParse(context, transport_socket);
    if (!common_tls_context.ok()) {
      errors.emplace_back(absl::StrCat("Error parsing security configuration: ",
                                       common_tls_context.status().message()));
    } else {
      cds_update.common_tls_context = std::move(*common_tls_context);
    }
  }
  // Record LRS server name (if any).
  const envoy_config_core_v3_ConfigSource* lrs_server =
      envoy_config_cluster_v3_Cluster_lrs_server(cluster);
  if (lrs_server != nullptr) {
    if (!envoy_config_core_v3_ConfigSource_has_self(lrs_server)) {
      errors.emplace_back("LRS ConfigSource is not self.");
    }
    cds_update.lrs_load_reporting_server.emplace(
        static_cast<const GrpcXdsBootstrap::GrpcXdsServer&>(context.server));
  }
  // The Cluster resource encodes the circuit breaking parameters in a list of
  // Thresholds messages, where each message specifies the parameters for a
  // particular RoutingPriority. we will look only at the first entry in the
  // list for priority DEFAULT and default to 1024 if not found.
  if (envoy_config_cluster_v3_Cluster_has_circuit_breakers(cluster)) {
    const envoy_config_cluster_v3_CircuitBreakers* circuit_breakers =
        envoy_config_cluster_v3_Cluster_circuit_breakers(cluster);
    size_t num_thresholds;
    const envoy_config_cluster_v3_CircuitBreakers_Thresholds* const*
        thresholds = envoy_config_cluster_v3_CircuitBreakers_thresholds(
            circuit_breakers, &num_thresholds);
    for (size_t i = 0; i < num_thresholds; ++i) {
      const auto* threshold = thresholds[i];
      if (envoy_config_cluster_v3_CircuitBreakers_Thresholds_priority(
              threshold) == envoy_config_core_v3_DEFAULT) {
        const google_protobuf_UInt32Value* max_requests =
            envoy_config_cluster_v3_CircuitBreakers_Thresholds_max_requests(
                threshold);
        if (max_requests != nullptr) {
          cds_update.max_concurrent_requests =
              google_protobuf_UInt32Value_value(max_requests);
        }
        break;
      }
    }
  }
  // As long as outlier detection field is present in the cluster update,
  // we will end up with a outlier detection in the cluster resource which will
  // lead to the creation of outlier detection in discovery mechanism.  Values
  // for outlier detection will be based on fields received and
  // default values.
  if (XdsOutlierDetectionEnabled() &&
      envoy_config_cluster_v3_Cluster_has_outlier_detection(cluster)) {
    OutlierDetectionConfig outlier_detection_update;
    const envoy_config_cluster_v3_OutlierDetection* outlier_detection =
        envoy_config_cluster_v3_Cluster_outlier_detection(cluster);
    const google_protobuf_Duration* duration =
        envoy_config_cluster_v3_OutlierDetection_interval(outlier_detection);
    if (duration != nullptr) {
      outlier_detection_update.interval = ParseDuration(duration);
    }
    duration = envoy_config_cluster_v3_OutlierDetection_base_ejection_time(
        outlier_detection);
    if (duration != nullptr) {
      outlier_detection_update.base_ejection_time = ParseDuration(duration);
    }
    duration = envoy_config_cluster_v3_OutlierDetection_max_ejection_time(
        outlier_detection);
    if (duration != nullptr) {
      outlier_detection_update.max_ejection_time = ParseDuration(duration);
    }
    const google_protobuf_UInt32Value* max_ejection_percent =
        envoy_config_cluster_v3_OutlierDetection_max_ejection_percent(
            outlier_detection);
    if (max_ejection_percent != nullptr) {
      outlier_detection_update.max_ejection_percent =
          google_protobuf_UInt32Value_value(max_ejection_percent);
    }
    const google_protobuf_UInt32Value* enforcing_success_rate =
        envoy_config_cluster_v3_OutlierDetection_enforcing_success_rate(
            outlier_detection);
    if (enforcing_success_rate != nullptr) {
      uint32_t enforcement_percentage =
          google_protobuf_UInt32Value_value(enforcing_success_rate);
      if (enforcement_percentage != 0) {
        OutlierDetectionConfig::SuccessRateEjection success_rate_ejection;
        success_rate_ejection.enforcement_percentage = enforcement_percentage;
        const google_protobuf_UInt32Value* minimum_hosts =
            envoy_config_cluster_v3_OutlierDetection_success_rate_minimum_hosts(
                outlier_detection);
        if (minimum_hosts != nullptr) {
          success_rate_ejection.minimum_hosts =
              google_protobuf_UInt32Value_value(minimum_hosts);
        }
        const google_protobuf_UInt32Value* request_volume =
            envoy_config_cluster_v3_OutlierDetection_success_rate_request_volume(
                outlier_detection);
        if (request_volume != nullptr) {
          success_rate_ejection.request_volume =
              google_protobuf_UInt32Value_value(request_volume);
        }
        const google_protobuf_UInt32Value* stdev_factor =
            envoy_config_cluster_v3_OutlierDetection_success_rate_stdev_factor(
                outlier_detection);
        if (stdev_factor != nullptr) {
          success_rate_ejection.stdev_factor =
              google_protobuf_UInt32Value_value(stdev_factor);
        }
        outlier_detection_update.success_rate_ejection = success_rate_ejection;
      }
    }
    const google_protobuf_UInt32Value* enforcing_failure_percentage =
        envoy_config_cluster_v3_OutlierDetection_enforcing_failure_percentage(
            outlier_detection);
    if (enforcing_failure_percentage != nullptr) {
      uint32_t enforcement_percentage =
          google_protobuf_UInt32Value_value(enforcing_failure_percentage);
      if (enforcement_percentage != 0) {
        OutlierDetectionConfig::FailurePercentageEjection
            failure_percentage_ejection;
        failure_percentage_ejection.enforcement_percentage =
            enforcement_percentage;
        const google_protobuf_UInt32Value* minimum_hosts =
            envoy_config_cluster_v3_OutlierDetection_failure_percentage_minimum_hosts(
                outlier_detection);
        if (minimum_hosts != nullptr) {
          failure_percentage_ejection.minimum_hosts =
              google_protobuf_UInt32Value_value(minimum_hosts);
        }
        const google_protobuf_UInt32Value* request_volume =
            envoy_config_cluster_v3_OutlierDetection_failure_percentage_request_volume(
                outlier_detection);
        if (request_volume != nullptr) {
          failure_percentage_ejection.request_volume =
              google_protobuf_UInt32Value_value(request_volume);
        }
        const google_protobuf_UInt32Value* threshold =
            envoy_config_cluster_v3_OutlierDetection_failure_percentage_threshold(
                outlier_detection);
        if (threshold != nullptr) {
          failure_percentage_ejection.threshold =
              google_protobuf_UInt32Value_value(threshold);
        }
        outlier_detection_update.failure_percentage_ejection =
            failure_percentage_ejection;
      }
    }
    cds_update.outlier_detection = outlier_detection_update;
  }
  // Return result.
  if (!errors.empty()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "errors parsing CDS resource: [", absl::StrJoin(errors, "; "), "]"));
  }
  return cds_update;
}

void MaybeLogCluster(const XdsResourceType::DecodeContext& context,
                     const envoy_config_cluster_v3_Cluster* cluster) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_MessageDef* msg_type =
        envoy_config_cluster_v3_Cluster_getmsgdef(context.symtab);
    char buf[10240];
    upb_TextEncode(cluster, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] Cluster: %s", context.client, buf);
  }
}

}  // namespace

XdsResourceType::DecodeResult XdsClusterResourceType::Decode(
    const XdsResourceType::DecodeContext& context,
    absl::string_view serialized_resource, bool is_v2) const {
  DecodeResult result;
  // Parse serialized proto.
  auto* resource = envoy_config_cluster_v3_Cluster_parse(
      serialized_resource.data(), serialized_resource.size(), context.arena);
  if (resource == nullptr) {
    result.resource =
        absl::InvalidArgumentError("Can't parse Cluster resource.");
    return result;
  }
  MaybeLogCluster(context, resource);
  // Validate resource.
  result.name =
      UpbStringToStdString(envoy_config_cluster_v3_Cluster_name(resource));
  auto cds_resource = CdsResourceParse(context, resource, is_v2);
  if (!cds_resource.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_ERROR, "[xds_client %p] invalid Cluster %s: %s",
              context.client, result.name->c_str(),
              cds_resource.status().ToString().c_str());
    }
    result.resource = cds_resource.status();
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_INFO, "[xds_client %p] parsed Cluster %s: %s", context.client,
              result.name->c_str(), cds_resource->ToString().c_str());
    }
    auto resource = absl::make_unique<ResourceDataSubclass>();
    resource->resource = std::move(*cds_resource);
    result.resource = std::move(resource);
  }
  return result;
}

}  // namespace grpc_core
