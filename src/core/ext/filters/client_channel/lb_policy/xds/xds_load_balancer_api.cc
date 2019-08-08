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

#include <grpc/support/port_platform.h>

#include <algorithm>

#include <grpc/impl/codegen/log.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_load_balancer_api.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

#include "envoy/api/v2/core/address.upb.h"
#include "envoy/api/v2/core/base.upb.h"
#include "envoy/api/v2/discovery.upb.h"
#include "envoy/api/v2/eds.upb.h"
#include "envoy/api/v2/endpoint/endpoint.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "upb/upb.h"

namespace grpc_core {

namespace {

constexpr char kEdsTypeUrl[] =
    "type.googleapis.com/envoy.api.v2.ClusterLoadAssignment";
constexpr char kEndpointRequired[] = "endpointRequired";

}  // namespace

grpc_slice XdsEdsRequestCreateAndEncode(const char* service_name) {
  upb::Arena arena;
  // Create a request.
  envoy_api_v2_DiscoveryRequest* request =
      envoy_api_v2_DiscoveryRequest_new(arena.ptr());
  envoy_api_v2_core_Node* node =
      envoy_api_v2_DiscoveryRequest_mutable_node(request, arena.ptr());
  google_protobuf_Struct* metadata =
      envoy_api_v2_core_Node_mutable_metadata(node, arena.ptr());
  google_protobuf_Struct_FieldsEntry* field =
      google_protobuf_Struct_add_fields(metadata, arena.ptr());
  google_protobuf_Struct_FieldsEntry_set_key(
      field, upb_strview_makez(kEndpointRequired));
  google_protobuf_Value* value =
      google_protobuf_Struct_FieldsEntry_mutable_value(field, arena.ptr());
  google_protobuf_Value_set_bool_value(value, true);
  envoy_api_v2_DiscoveryRequest_add_resource_names(
      request, upb_strview_makez(service_name), arena.ptr());
  envoy_api_v2_DiscoveryRequest_set_type_url(request,
                                             upb_strview_makez(kEdsTypeUrl));
  // Encode the request.
  size_t output_length;
  char* output = envoy_api_v2_DiscoveryRequest_serialize(request, arena.ptr(),
                                                         &output_length);
  return grpc_slice_from_copied_buffer(output, output_length);
}

namespace {

grpc_error* ServerAddressParseAndAppend(
    const envoy_api_v2_endpoint_LbEndpoint* lb_endpoint,
    ServerAddressList* list) {
  // Find the ip:port.
  const envoy_api_v2_endpoint_Endpoint* endpoint =
      envoy_api_v2_endpoint_LbEndpoint_endpoint(lb_endpoint);
  const envoy_api_v2_core_Address* address =
      envoy_api_v2_endpoint_Endpoint_address(endpoint);
  const envoy_api_v2_core_SocketAddress* socket_address =
      envoy_api_v2_core_Address_socket_address(address);
  upb_strview address_strview =
      envoy_api_v2_core_SocketAddress_address(socket_address);
  uint32_t port = envoy_api_v2_core_SocketAddress_port_value(socket_address);
  if (GPR_UNLIKELY(port >> 16) != 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Invalid port.");
  }
  // Populate grpc_resolved_address.
  grpc_resolved_address addr;
  char* address_str = static_cast<char*>(gpr_malloc(address_strview.size + 1));
  memcpy(address_str, address_strview.data, address_strview.size);
  address_str[address_strview.size] = '\0';
  grpc_string_to_sockaddr(&addr, address_str, port);
  gpr_free(address_str);
  // Append the address to the list.
  list->emplace_back(addr, nullptr);
  return GRPC_ERROR_NONE;
}

namespace {

UniquePtr<char> StringCopy(const upb_strview& strview) {
  char* str = static_cast<char*>(gpr_malloc(strview.size + 1));
  memcpy(str, strview.data, strview.size);
  str[strview.size] = '\0';
  return UniquePtr<char>(str);
}

}  // namespace

grpc_error* LocalityParse(
    const envoy_api_v2_endpoint_LocalityLbEndpoints* locality_lb_endpoints,
    XdsLocalityInfo* locality_info) {
  // Parse locality name.
  const envoy_api_v2_core_Locality* locality =
      envoy_api_v2_endpoint_LocalityLbEndpoints_locality(locality_lb_endpoints);
  locality_info->locality_name = MakeRefCounted<XdsLocalityName>(
      StringCopy(envoy_api_v2_core_Locality_region(locality)),
      StringCopy(envoy_api_v2_core_Locality_zone(locality)),
      StringCopy(envoy_api_v2_core_Locality_sub_zone(locality)));
  // Parse the addresses.
  size_t size;
  const envoy_api_v2_endpoint_LbEndpoint* const* lb_endpoints =
      envoy_api_v2_endpoint_LocalityLbEndpoints_lb_endpoints(
          locality_lb_endpoints, &size);
  for (size_t i = 0; i < size; ++i) {
    grpc_error* error = ServerAddressParseAndAppend(lb_endpoints[i],
                                                    &locality_info->serverlist);
    if (error != GRPC_ERROR_NONE) return error;
  }
  // Parse the lb_weight and priority.
  const google_protobuf_UInt32Value* lb_weight =
      envoy_api_v2_endpoint_LocalityLbEndpoints_load_balancing_weight(
          locality_lb_endpoints);
  // If LB weight is not specified, the default weight 0 is used, which means
  // this locality is assigned no load.
  locality_info->lb_weight =
      lb_weight != nullptr ? google_protobuf_UInt32Value_value(lb_weight) : 0;
  locality_info->priority =
      envoy_api_v2_endpoint_LocalityLbEndpoints_priority(locality_lb_endpoints);
  return GRPC_ERROR_NONE;
}

}  // namespace

grpc_error* XdsEdsResponseDecodeAndParse(const grpc_slice& encoded_response,
                                         XdsUpdate* update) {
  upb::Arena arena;
  // Decode the response.
  const envoy_api_v2_DiscoveryResponse* response =
      envoy_api_v2_DiscoveryResponse_parse(
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(encoded_response)),
          GRPC_SLICE_LENGTH(encoded_response), arena.ptr());
  // Parse the response.
  if (response == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No response found.");
  }
  // Check the type_url of the response.
  upb_strview type_url = envoy_api_v2_DiscoveryResponse_type_url(response);
  upb_strview expected_type_url = upb_strview_makez(kEdsTypeUrl);
  if (!upb_strview_eql(type_url, expected_type_url)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource is not EDS.");
  }
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_api_v2_DiscoveryResponse_resources(response, &size);
  if (size < 1) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "EDS response contains 0 resource.");
  }
  // Check the type_url of the resource.
  type_url = google_protobuf_Any_type_url(resources[0]);
  if (!upb_strview_eql(type_url, expected_type_url)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource is not EDS.");
  }
  // Get the cluster_load_assignment.
  upb_strview encoded_cluster_load_assignment =
      google_protobuf_Any_value(resources[0]);
  envoy_api_v2_ClusterLoadAssignment* cluster_load_assignment =
      envoy_api_v2_ClusterLoadAssignment_parse(
          encoded_cluster_load_assignment.data,
          encoded_cluster_load_assignment.size, arena.ptr());
  const envoy_api_v2_endpoint_LocalityLbEndpoints* const* endpoints =
      envoy_api_v2_ClusterLoadAssignment_endpoints(cluster_load_assignment,
                                                   &size);
  for (size_t i = 0; i < size; ++i) {
    XdsLocalityInfo locality_info;
    grpc_error* error = LocalityParse(endpoints[i], &locality_info);
    if (error != GRPC_ERROR_NONE) return error;
    update->locality_list.push_back(std::move(locality_info));
  }
  // The locality list is sorted here into deterministic order so that it's
  // easier to check if two locality lists contain the same set of localities.
  std::sort(update->locality_list.data(),
            update->locality_list.data() + update->locality_list.size(),
            XdsLocalityInfo::Less());
  return GRPC_ERROR_NONE;
}

namespace {

void google_protobuf_Timestamp_assign(google_protobuf_Timestamp* timestamp,
                                      const gpr_timespec& value) {
  google_protobuf_Timestamp_set_seconds(timestamp, value.tv_sec);
  google_protobuf_Timestamp_set_nanos(timestamp, value.tv_nsec);
}

}  // namespace

xds_grpclb_request* xds_grpclb_load_report_request_create_locked(
    grpc_core::XdsLbClientStats* client_stats, upb_arena* arena) {
  xds_grpclb_request* req = grpc_lb_v1_LoadBalanceRequest_new(arena);
  grpc_lb_v1_ClientStats* req_stats =
      grpc_lb_v1_LoadBalanceRequest_mutable_client_stats(req, arena);
  google_protobuf_Timestamp_assign(
      grpc_lb_v1_ClientStats_mutable_timestamp(req_stats, arena),
      gpr_now(GPR_CLOCK_REALTIME));

  int64_t num_calls_started;
  int64_t num_calls_finished;
  int64_t num_calls_finished_with_client_failed_to_send;
  int64_t num_calls_finished_known_received;
  UniquePtr<XdsLbClientStats::DroppedCallCounts> drop_token_counts;
  client_stats->GetLocked(&num_calls_started, &num_calls_finished,
                          &num_calls_finished_with_client_failed_to_send,
                          &num_calls_finished_known_received,
                          &drop_token_counts);
  grpc_lb_v1_ClientStats_set_num_calls_started(req_stats, num_calls_started);
  grpc_lb_v1_ClientStats_set_num_calls_finished(req_stats, num_calls_finished);
  grpc_lb_v1_ClientStats_set_num_calls_finished_with_client_failed_to_send(
      req_stats, num_calls_finished_with_client_failed_to_send);
  grpc_lb_v1_ClientStats_set_num_calls_finished_known_received(
      req_stats, num_calls_finished_known_received);
  if (drop_token_counts != nullptr) {
    for (size_t i = 0; i < drop_token_counts->size(); ++i) {
      XdsLbClientStats::DropTokenCount& cur = (*drop_token_counts)[i];
      grpc_lb_v1_ClientStatsPerToken* cur_msg =
          grpc_lb_v1_ClientStats_add_calls_finished_with_drop(req_stats, arena);

      const size_t token_len = strlen(cur.token.get());
      char* token = reinterpret_cast<char*>(upb_arena_malloc(arena, token_len));
      memcpy(token, cur.token.get(), token_len);

      grpc_lb_v1_ClientStatsPerToken_set_load_balance_token(
          cur_msg, upb_strview_make(token, token_len));
      grpc_lb_v1_ClientStatsPerToken_set_num_calls(cur_msg, cur.count);
    }
  }
  return req;
}

}  // namespace grpc_core
