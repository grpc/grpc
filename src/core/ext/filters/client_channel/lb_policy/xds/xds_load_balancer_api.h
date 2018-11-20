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
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "envoy/api/v2/discovery.upb.h"

#define XDS_SERVICE_NAME_MAX_LENGTH 128

typedef grpc_lb_v1_Server_ip_address_t xds_grpclb_ip_address;
typedef grpc_lb_v1_LoadBalanceRequest xds_grpclb_request;
typedef grpc_lb_v1_InitialLoadBalanceResponse xds_grpclb_initial_response;
typedef grpc_lb_v1_Server xds_grpclb_server;
typedef google_protobuf_Duration xds_grpclb_duration;
typedef google_protobuf_Timestamp xds_grpclb_timestamp;
// xDS API typedef
typedef envoy_api_v2_DiscoveryRequest xds_discovery_request;
typedef envoy_api_v2_DiscoveryResponse xds_discovery_response;

typedef struct {
  xds_grpclb_server** servers;
  size_t num_servers;
} xds_grpclb_serverlist;

/** Create a request for a xDS service under \a lb_service_name */
xds_discovery_request* xds_request_create(const char* lb_service_name,
                                       upb_arena *arena);

/** Protocol Buffers v3-encode \a request */
grpc_slice xds_request_encode(const xds_discovery_request* request,
                              upb_arena *arena);

/** Destroy \a request */
void xds_request_destroy(xds_discovery_request* request);

/** Parse (ie, decode) the bytes in \a encoded_xds_grpclb_response as a \a
 * xds_grpclb_initial_response */
xds_grpclb_initial_response* xds_grpclb_initial_response_parse(
    grpc_slice encoded_xds_grpclb_response);

/** Parse the list of servers from an encoded \a xds_grpclb_response */
xds_grpclb_serverlist* xds_grpclb_response_parse_serverlist(
    grpc_slice encoded_xds_grpclb_response);

/** Destroy \a initial_response */
void xds_grpclb_initial_response_destroy(xds_grpclb_initial_response* response);

// **********
// NO-CHANGE
// **********
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

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOAD_BALANCER_API_H \
        */
