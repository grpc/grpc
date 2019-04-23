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

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_client_stats.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/upb-generated/envoy/api/v2/core/address.upb.h"
#include "src/core/ext/upb-generated/envoy/api/v2/core/base.upb.h"
#include "src/core/ext/upb-generated/envoy/api/v2/discovery.upb.h"
#include "src/core/ext/upb-generated/envoy/api/v2/eds.upb.h"
#include "src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint.upb.h"
#include "src/core/ext/upb-generated/google/protobuf/struct.upb.h"
#include "src/core/ext/upb-generated/google/protobuf/wrappers.upb.h"
#include "src/core/ext/upb-generated/google/protobuf/any.upb.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "upb/upb.h"

#define XDS_SERVICE_NAME_MAX_LENGTH 128

namespace grpc_core {

typedef grpc_lb_v1_LoadBalanceRequest xds_grpclb_request;

using XdsDiscoveryRequest = envoy_api_v2_DiscoveryRequest;
using XdsDiscoveryResponse = envoy_api_v2_DiscoveryResponse;
using XdsClusterLoadAssignment = envoy_api_v2_ClusterLoadAssignment;
using XdsLocalityLbEndpoints = envoy_api_v2_endpoint_LocalityLbEndpoints;
using XdsLocality = envoy_api_v2_core_Locality;
using XdsLbEndpoint = envoy_api_v2_endpoint_LbEndpoint;
using XdsEndpoint = envoy_api_v2_endpoint_Endpoint;
using XdsAddress = envoy_api_v2_core_Address;
using XdsSocketAddress = envoy_api_v2_core_SocketAddress;
using XdsNode = envoy_api_v2_core_Node;
using XdsStruct = google_protobuf_Struct;
using XdsFieldsEntry = google_protobuf_Struct_FieldsEntry;
using XdsValue = google_protobuf_Value;

struct XdsLocalityUpdateArgs {
  grpc_slice locality_name;
  UniquePtr<ServerAddressList> serverlist;
  uint32_t lb_weight;
  uint32_t priority;
};

struct XdsLoadUpdateArgs {
  InlinedVector<XdsLocalityUpdateArgs, 1> localities;
  // TODO(juanlishen): Pass drop_per_million when adding drop support.
};

// Creates a request to gRPC LB querying \a service_name.
grpc_slice XdsRequestCreateAndEncode(const char* service_name);

// Parses the response and returns the args to update locality map.
UniquePtr<XdsLoadUpdateArgs> XdsResponseDecodeAndParse(
    const grpc_slice& encoded_response);

// TODO(juanlishen): Delete these when LRS is added.
xds_grpclb_request* xds_grpclb_load_report_request_create_locked(
    grpc_core::XdsLbClientStats* client_stats);
void xds_grpclb_request_destroy(xds_grpclb_request* request);

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOAD_BALANCER_API_H \
        */
