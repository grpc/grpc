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
#include "src/core/ext/upb-generated/envoy/service/load_stats/v2/lrs.upb.h"
#include "src/core/ext/upb-generated/google/protobuf/any.upb.h"
#include "src/core/ext/upb-generated/google/protobuf/struct.upb.h"
#include "src/core/ext/upb-generated/google/protobuf/wrappers.upb.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "upb/upb.h"

namespace grpc_core {

typedef grpc_lb_v1_LoadBalanceRequest xds_grpclb_request;
typedef google_protobuf_Timestamp xds_grpclb_timestamp;

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
using XdsLoadStatsRequest = envoy_service_load_stats_v2_LoadStatsRequest;
using XdsLoadStatsResponse = envoy_service_load_stats_v2_LoadStatsResponse;

struct XdsLocalityInfo {
  bool operator==(const XdsLocalityInfo& other) const {
    return *locality_name == *other.locality_name &&
           serverlist == other.serverlist && lb_weight == other.lb_weight &&
           priority == other.priority;
  }

  RefCountedPtr<XdsLocalityName> locality_name;
  ServerAddressList serverlist;
  uint32_t lb_weight;
  uint32_t priority;
};

using XdsLocalityList = InlinedVector<XdsLocalityInfo, 1>;

struct XdsUpdate {
  XdsLocalityList locality_list;
  // TODO(juanlishen): Pass drop_per_million when adding drop support.
};

struct XdsLoadReportingConfig {
  bool operator==(const XdsLoadReportingConfig& other) {
    return interval == other.interval &&
           report_endpoint_granularity == other.report_endpoint_granularity;
  }

  grpc_millis interval = 0;
  // TODO(juanlishen): Use this when we add per-backend load reporting.
  bool report_endpoint_granularity = false;
};

// Creates an EDS request querying \a service_name.
grpc_slice XdsEdsRequestCreateAndEncode(const char* service_name);

// Parses the EDS response and returns the args to update locality map. If there
// is any error, the output update is invalid.
grpc_error* XdsEdsResponseDecodeAndParse(const grpc_slice& encoded_response,
                                         XdsUpdate* update);

grpc_slice XdsLrsRequestCreateAndEncode(const char* server_name);

// If all the counters are zero, return empty slice.
grpc_slice XdsLrsRequestCreateAndEncode(XdsLbClientStats* client_stats);

grpc_error* XdsLrsResponseDecodeAndParse(const grpc_slice& encoded_response,
                                         XdsLoadReportingConfig* response,
                                         const char* expected_server_name);

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_XDS_XDS_LOAD_BALANCER_API_H \
        */
