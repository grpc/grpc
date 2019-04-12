/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOAD_BALANCER_API_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOAD_BALANCER_API_H

#include <grpc/support/port_platform.h>

#include <grpc/slice_buffer.h>

#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_client_stats.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/upb-generated/envoy/api/v2/discovery.upb.h"
#include "src/core/ext/upb-generated/envoy/api/v2/eds.upb.h"
#include "src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint.upb.h"
#include "src/core/ext/upb-generated/google/protobuf/struct.upb.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "third_party/upb/upb/upb.h"

#define XDS_SERVICE_NAME_MAX_LENGTH 128

namespace grpc_core {

// typedef grpc_lb_v1_Server_ip_address_t xds_grpclb_ip_address;
// typedef grpc_lb_v1_LoadBalanceRequest xds_grpclb_request;
// typedef grpc_lb_v1_InitialLoadBalanceResponse xds_grpclb_initial_response;
// typedef grpc_lb_v1_Server xds_grpclb_server;
// typedef google_protobuf_Duration xds_grpclb_duration;
// typedef google_protobuf_Timestamp xds_grpclb_timestamp;

using XdsDiscoveryRequest = envoy_api_v2_DiscoveryRequest;
using XdsDiscoveryResponse = envoy_api_v2_DiscoveryResponse;
using XdsClusterLoadAssignment = envoy_api_v2_ClusterLoadAssignment;
using XdsLocalityLbEndpoints = envoy_api_v2_endpoint_LocalityLbEndpoints;
using XdsLocalityLbEndpointsList =
    InlinedVector<Pair<UniquePtr<char>, XdsLocalityLbEndpoints*>, 1>;
using XdsLocality = envoy_api_v2_core_Locality;
using XdsLbEndpoint = envoy_api_v2_endpoint_LbEndpoint;
using XdsEndpoint = envoy_api_v2_endpoint_Endpoint;
using XdsNode = envoy_api_v2_core_Node;
using XdsStruct = google_protobuf_Struct;
using XdsFieldsEntry = google_protobuf_Struct_FieldsEntry;
using XdsValue = google_protobuf_Value;

struct LocalityUpdateArgs {
  UniquePtr<char> locality_name;
  ServerAddressList addresses;
  uint32_t lb_weight;
  uint32_t priority;
};

struct LoadUpdateArgs {
  InlinedVector<LocalityUpdateArgs, 1> localities;
  uint32_t drop_per_million;
};

// Creates a request to gRPC LB querying \a service_name.
XdsDiscoveryRequest* XdsRequestCreate(const char* service_name);
XdsDiscoveryResponse* XdsResponseDecode(const grpc_slice& encoded_response);
LoadUpdateArgs XdsLocalitiesFromResponse(const XdsDiscoveryResponse* response);

typedef struct {
  xds_grpclb_server** servers;
  size_t num_servers;
} xds_grpclb_serverlist;

xds_grpclb_request* xds_grpclb_request_create(const char* lb_service_name);
xds_grpclb_request* xds_grpclb_load_report_request_create_locked(
    grpc_core::XdsLbClientStats* client_stats);

// Serializes the \a request into a slice.
grpc_slice XdsRequestEncode(const XdsDiscoveryRequest* request);

/** Destroy \a request */
void xds_grpclb_request_destroy(xds_grpclb_request* request);

/** Parse (ie, decode) the bytes in \a encoded_xds_grpclb_response as a \a
 * xds_grpclb_initial_response */
xds_grpclb_initial_response* xds_grpclb_initial_response_parse(
    const grpc_slice& encoded_xds_grpclb_response);

/** Parse the list of servers from an encoded \a xds_grpclb_response */
xds_grpclb_serverlist* xds_grpclb_response_parse_serverlist(
    const grpc_slice& encoded_xds_grpclb_response);

/** Return a copy of \a sl. The caller is responsible for calling \a
 * xds_grpclb_destroy_serverlist on the returned copy. */
xds_grpclb_serverlist* xds_grpclb_serverlist_copy(
    const xds_grpclb_serverlist* sl);

bool xds_grpclb_serverlist_equals(const xds_grpclb_serverlist* lhs,
                                  const xds_grpclb_serverlist* rhs);

bool xds_grpclb_server_equals(const xds_grpclb_server* lhs,
                              const xds_grpclb_server* rhs);

/** Destroy \a serverlist */
void xds_grpclb_destroy_serverlist(xds_grpclb_serverlist* serverlist);

/** Compare \a lhs against \a rhs and return 0 if \a lhs and \a rhs are equal,
 * < 0 if \a lhs represents a duration shorter than \a rhs and > 0 otherwise */
int xds_grpclb_duration_compare(const xds_grpclb_duration* lhs,
                                const xds_grpclb_duration* rhs);

grpc_millis xds_grpclb_duration_to_millis(xds_grpclb_duration* duration_pb);

/** Destroy \a initial_response */
void xds_grpclb_initial_response_destroy(xds_grpclb_initial_response* response);
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOAD_BALANCER_API_H \
        */
