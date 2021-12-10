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

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "envoy/config/cluster/v3/circuit_breaker.upb.h"
#include "envoy/config/cluster/v3/cluster.upb.h"
#include "envoy/config/cluster/v3/cluster.upbdefs.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/config_source.upb.h"
#include "envoy/config/endpoint/v3/endpoint.upb.h"
#include "envoy/config/endpoint/v3/endpoint_components.upb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/wrappers.upb.h"

#include <grpc/support/alloc.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"

namespace grpc_core {

//
// XdsClusterResource
//

std::string XdsClusterResource::ToString() const {
  absl::InlinedVector<std::string, 8> contents;
  switch (cluster_type) {
    case EDS:
      contents.push_back("cluster_type=EDS");
      if (!eds_service_name.empty()) {
        contents.push_back(
            absl::StrFormat("eds_service_name=%s", eds_service_name));
      }
      break;
    case LOGICAL_DNS:
      contents.push_back("cluster_type=LOGICAL_DNS");
      contents.push_back(absl::StrFormat("dns_hostname=%s", dns_hostname));
      break;
    case AGGREGATE:
      contents.push_back("cluster_type=AGGREGATE");
      contents.push_back(
          absl::StrFormat("prioritized_cluster_names=[%s]",
                          absl::StrJoin(prioritized_cluster_names, ", ")));
  }
  if (!common_tls_context.Empty()) {
    contents.push_back(absl::StrFormat("common_tls_context=%s",
                                       common_tls_context.ToString()));
  }
  if (lrs_load_reporting_server_name.has_value()) {
    contents.push_back(absl::StrFormat("lrs_load_reporting_server_name=%s",
                                       lrs_load_reporting_server_name.value()));
  }
  contents.push_back(absl::StrCat("lb_policy=", lb_policy));
  if (lb_policy == "RING_HASH") {
    contents.push_back(absl::StrCat("min_ring_size=", min_ring_size));
    contents.push_back(absl::StrCat("max_ring_size=", max_ring_size));
  }
  contents.push_back(
      absl::StrFormat("max_concurrent_requests=%d", max_concurrent_requests));
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

//
// XdsClusterResourceType
//

namespace {

grpc_error_handle UpstreamTlsContextParse(
    const XdsEncodingContext& context,
    const envoy_config_core_v3_TransportSocket* transport_socket,
    CommonTlsContext* common_tls_context) {
  // Record Upstream tls context
  absl::string_view name = UpbStringToAbsl(
      envoy_config_core_v3_TransportSocket_name(transport_socket));
  if (name != "envoy.transport_sockets.tls") {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("Unrecognized transport socket: ", name));
  }
  auto* typed_config =
      envoy_config_core_v3_TransportSocket_typed_config(transport_socket);
  if (typed_config != nullptr) {
    const upb_strview encoded_upstream_tls_context =
        google_protobuf_Any_value(typed_config);
    auto* upstream_tls_context =
        envoy_extensions_transport_sockets_tls_v3_UpstreamTlsContext_parse(
            encoded_upstream_tls_context.data,
            encoded_upstream_tls_context.size, context.arena);
    if (upstream_tls_context == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Can't decode upstream tls context.");
    }
    auto* common_tls_context_proto =
        envoy_extensions_transport_sockets_tls_v3_UpstreamTlsContext_common_tls_context(
            upstream_tls_context);
    if (common_tls_context_proto != nullptr) {
      grpc_error_handle error = CommonTlsContext::Parse(
          context, common_tls_context_proto, common_tls_context);
      if (error != GRPC_ERROR_NONE) {
        return grpc_error_add_child(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                        "Error parsing UpstreamTlsContext"),
                                    error);
      }
    }
  }
  if (common_tls_context->certificate_validation_context
          .ca_certificate_provider_instance.instance_name.empty()) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "UpstreamTlsContext: TLS configuration provided but no "
        "ca_certificate_provider_instance found.");
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle CdsLogicalDnsParse(
    const envoy_config_cluster_v3_Cluster* cluster,
    XdsClusterResource* cds_update) {
  const auto* load_assignment =
      envoy_config_cluster_v3_Cluster_load_assignment(cluster);
  if (load_assignment == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "load_assignment not present for LOGICAL_DNS cluster");
  }
  size_t num_localities;
  const auto* const* localities =
      envoy_config_endpoint_v3_ClusterLoadAssignment_endpoints(load_assignment,
                                                               &num_localities);
  if (num_localities != 1) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("load_assignment for LOGICAL_DNS cluster must have "
                     "exactly one locality, found ",
                     num_localities));
  }
  size_t num_endpoints;
  const auto* const* endpoints =
      envoy_config_endpoint_v3_LocalityLbEndpoints_lb_endpoints(localities[0],
                                                                &num_endpoints);
  if (num_endpoints != 1) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("locality for LOGICAL_DNS cluster must have "
                     "exactly one endpoint, found ",
                     num_endpoints));
  }
  const auto* endpoint =
      envoy_config_endpoint_v3_LbEndpoint_endpoint(endpoints[0]);
  if (endpoint == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "LbEndpoint endpoint field not set");
  }
  const auto* address = envoy_config_endpoint_v3_Endpoint_address(endpoint);
  if (address == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Endpoint address field not set");
  }
  const auto* socket_address =
      envoy_config_core_v3_Address_socket_address(address);
  if (socket_address == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Address socket_address field not set");
  }
  if (envoy_config_core_v3_SocketAddress_resolver_name(socket_address).size !=
      0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "LOGICAL_DNS clusters must NOT have a custom resolver name set");
  }
  absl::string_view address_str = UpbStringToAbsl(
      envoy_config_core_v3_SocketAddress_address(socket_address));
  if (address_str.empty()) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "SocketAddress address field not set");
  }
  if (!envoy_config_core_v3_SocketAddress_has_port_value(socket_address)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "SocketAddress port_value field not set");
  }
  cds_update->dns_hostname = JoinHostPort(
      address_str,
      envoy_config_core_v3_SocketAddress_port_value(socket_address));
  return GRPC_ERROR_NONE;
}

// TODO(donnadionne): Check to see if cluster types aggregate_cluster and
// logical_dns are enabled, this will be
// removed once the cluster types are fully integration-tested and enabled by
// default.
bool XdsAggregateAndLogicalDnsClusterEnabled() {
  char* value = gpr_getenv(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value, &parsed_value);
  gpr_free(value);
  return parse_succeeded && parsed_value;
}

grpc_error_handle CdsResourceParse(
    const XdsEncodingContext& context,
    const envoy_config_cluster_v3_Cluster* cluster, bool /*is_v2*/,
    XdsClusterResource* cds_update) {
  std::vector<grpc_error_handle> errors;
  // Check the cluster_discovery_type.
  if (!envoy_config_cluster_v3_Cluster_has_type(cluster) &&
      !envoy_config_cluster_v3_Cluster_has_cluster_type(cluster)) {
    errors.push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("DiscoveryType not found."));
  } else if (envoy_config_cluster_v3_Cluster_type(cluster) ==
             envoy_config_cluster_v3_Cluster_EDS) {
    cds_update->cluster_type = XdsClusterResource::ClusterType::EDS;
    // Check the EDS config source.
    const envoy_config_cluster_v3_Cluster_EdsClusterConfig* eds_cluster_config =
        envoy_config_cluster_v3_Cluster_eds_cluster_config(cluster);
    const envoy_config_core_v3_ConfigSource* eds_config =
        envoy_config_cluster_v3_Cluster_EdsClusterConfig_eds_config(
            eds_cluster_config);
    if (!envoy_config_core_v3_ConfigSource_has_ads(eds_config)) {
      errors.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("EDS ConfigSource is not ADS."));
    }
    // Record EDS service_name (if any).
    upb_strview service_name =
        envoy_config_cluster_v3_Cluster_EdsClusterConfig_service_name(
            eds_cluster_config);
    if (service_name.size != 0) {
      cds_update->eds_service_name = UpbStringToStdString(service_name);
    }
  } else if (!XdsAggregateAndLogicalDnsClusterEnabled()) {
    errors.push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("DiscoveryType is not valid."));
  } else if (envoy_config_cluster_v3_Cluster_type(cluster) ==
             envoy_config_cluster_v3_Cluster_LOGICAL_DNS) {
    cds_update->cluster_type = XdsClusterResource::ClusterType::LOGICAL_DNS;
    grpc_error_handle error = CdsLogicalDnsParse(cluster, cds_update);
    if (error != GRPC_ERROR_NONE) errors.push_back(error);
  } else {
    if (!envoy_config_cluster_v3_Cluster_has_cluster_type(cluster)) {
      errors.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("DiscoveryType is not valid."));
    } else {
      const envoy_config_cluster_v3_Cluster_CustomClusterType*
          custom_cluster_type =
              envoy_config_cluster_v3_Cluster_cluster_type(cluster);
      upb_strview type_name =
          envoy_config_cluster_v3_Cluster_CustomClusterType_name(
              custom_cluster_type);
      if (UpbStringToAbsl(type_name) != "envoy.clusters.aggregate") {
        errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "DiscoveryType is not valid."));
      } else {
        cds_update->cluster_type = XdsClusterResource::ClusterType::AGGREGATE;
        // Retrieve aggregate clusters.
        const google_protobuf_Any* typed_config =
            envoy_config_cluster_v3_Cluster_CustomClusterType_typed_config(
                custom_cluster_type);
        const upb_strview aggregate_cluster_config_upb_strview =
            google_protobuf_Any_value(typed_config);
        const envoy_extensions_clusters_aggregate_v3_ClusterConfig*
            aggregate_cluster_config =
                envoy_extensions_clusters_aggregate_v3_ClusterConfig_parse(
                    aggregate_cluster_config_upb_strview.data,
                    aggregate_cluster_config_upb_strview.size, context.arena);
        if (aggregate_cluster_config == nullptr) {
          errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "Can't parse aggregate cluster."));
        } else {
          size_t size;
          const upb_strview* clusters =
              envoy_extensions_clusters_aggregate_v3_ClusterConfig_clusters(
                  aggregate_cluster_config, &size);
          for (size_t i = 0; i < size; ++i) {
            const upb_strview cluster = clusters[i];
            cds_update->prioritized_cluster_names.emplace_back(
                UpbStringToStdString(cluster));
          }
        }
      }
    }
  }
  // Check the LB policy.
  if (envoy_config_cluster_v3_Cluster_lb_policy(cluster) ==
      envoy_config_cluster_v3_Cluster_ROUND_ROBIN) {
    cds_update->lb_policy = "ROUND_ROBIN";
  } else if (envoy_config_cluster_v3_Cluster_lb_policy(cluster) ==
             envoy_config_cluster_v3_Cluster_RING_HASH) {
    cds_update->lb_policy = "RING_HASH";
    // Record ring hash lb config
    auto* ring_hash_config =
        envoy_config_cluster_v3_Cluster_ring_hash_lb_config(cluster);
    if (ring_hash_config != nullptr) {
      const google_protobuf_UInt64Value* max_ring_size =
          envoy_config_cluster_v3_Cluster_RingHashLbConfig_maximum_ring_size(
              ring_hash_config);
      if (max_ring_size != nullptr) {
        cds_update->max_ring_size =
            google_protobuf_UInt64Value_value(max_ring_size);
        if (cds_update->max_ring_size > 8388608 ||
            cds_update->max_ring_size == 0) {
          errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "max_ring_size is not in the range of 1 to 8388608."));
        }
      }
      const google_protobuf_UInt64Value* min_ring_size =
          envoy_config_cluster_v3_Cluster_RingHashLbConfig_minimum_ring_size(
              ring_hash_config);
      if (min_ring_size != nullptr) {
        cds_update->min_ring_size =
            google_protobuf_UInt64Value_value(min_ring_size);
        if (cds_update->min_ring_size > 8388608 ||
            cds_update->min_ring_size == 0) {
          errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "min_ring_size is not in the range of 1 to 8388608."));
        }
        if (cds_update->min_ring_size > cds_update->max_ring_size) {
          errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "min_ring_size cannot be greater than max_ring_size."));
        }
      }
      if (envoy_config_cluster_v3_Cluster_RingHashLbConfig_hash_function(
              ring_hash_config) !=
          envoy_config_cluster_v3_Cluster_RingHashLbConfig_XX_HASH) {
        errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "ring hash lb config has invalid hash function."));
      }
    }
  } else {
    errors.push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("LB policy is not supported."));
  }
  auto* transport_socket =
      envoy_config_cluster_v3_Cluster_transport_socket(cluster);
  if (transport_socket != nullptr) {
    grpc_error_handle error = UpstreamTlsContextParse(
        context, transport_socket, &cds_update->common_tls_context);
    if (error != GRPC_ERROR_NONE) {
      errors.push_back(
          grpc_error_add_child(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                   "Error parsing security configuration"),
                               error));
    }
  }
  // Record LRS server name (if any).
  const envoy_config_core_v3_ConfigSource* lrs_server =
      envoy_config_cluster_v3_Cluster_lrs_server(cluster);
  if (lrs_server != nullptr) {
    if (!envoy_config_core_v3_ConfigSource_has_self(lrs_server)) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          ": LRS ConfigSource is not self."));
    }
    cds_update->lrs_load_reporting_server_name.emplace("");
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
          cds_update->max_concurrent_requests =
              google_protobuf_UInt32Value_value(max_requests);
        }
        break;
      }
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing CDS resource", &errors);
}

void MaybeLogCluster(const XdsEncodingContext& context,
                     const envoy_config_cluster_v3_Cluster* cluster) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_config_cluster_v3_Cluster_getmsgdef(context.symtab);
    char buf[10240];
    upb_text_encode(cluster, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] Cluster: %s", context.client, buf);
  }
}

}  // namespace

absl::StatusOr<XdsResourceType::DecodeResult> XdsClusterResourceType::Decode(
    const XdsEncodingContext& context, absl::string_view serialized_resource,
    bool is_v2) const {
  // Parse serialized proto.
  auto* resource = envoy_config_cluster_v3_Cluster_parse(
      serialized_resource.data(), serialized_resource.size(), context.arena);
  if (resource == nullptr) {
    return absl::InvalidArgumentError("Can't parse Cluster resource.");
  }
  MaybeLogCluster(context, resource);
  // Validate resource.
  DecodeResult result;
  result.name =
      UpbStringToStdString(envoy_config_cluster_v3_Cluster_name(resource));
  auto cluster_data = absl::make_unique<ResourceDataSubclass>();
  grpc_error_handle error =
      CdsResourceParse(context, resource, is_v2, &cluster_data->resource);
  if (error != GRPC_ERROR_NONE) {
    std::string error_str = grpc_error_std_string(error);
    GRPC_ERROR_UNREF(error);
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_ERROR, "[xds_client %p] invalid Cluster %s: %s",
              context.client, result.name.c_str(), error_str.c_str());
    }
    result.resource = absl::InvalidArgumentError(error_str);
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_INFO, "[xds_client %p] parsed Cluster %s: %s", context.client,
              result.name.c_str(), cluster_data->resource.ToString().c_str());
    }
    result.resource = std::move(cluster_data);
  }
  return std::move(result);
}

}  // namespace grpc_core
