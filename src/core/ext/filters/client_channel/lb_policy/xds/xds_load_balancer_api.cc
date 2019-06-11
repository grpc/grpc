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

#include <grpc/impl/codegen/log.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "pb_decode.h"
#include "pb_encode.h"

#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_load_balancer_api.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

namespace grpc_core {

namespace {

constexpr char kEdsTypeUrl[] =
    "type.googleapis.com/grpc.lb.v2.ClusterLoadAssignment";
constexpr char kEndpointRequired[] = "endpointRequired";

}  // namespace

grpc_slice XdsEdsRequestCreateAndEncode(const char* service_name) {
  upb::Arena arena;
  // Create a request.
  XdsDiscoveryRequest* request = envoy_api_v2_DiscoveryRequest_new(arena.ptr());
  XdsNode* node =
      envoy_api_v2_DiscoveryRequest_mutable_node(request, arena.ptr());
  google_protobuf_Struct* metadata =
      envoy_api_v2_core_Node_mutable_metadata(node, arena.ptr());
  google_protobuf_Struct_FieldsEntry* field =
      google_protobuf_Struct_add_fields(metadata, arena.ptr());
  google_protobuf_Struct_FieldsEntry_set_key(
      field,
      upb_strview_make(kEndpointRequired, sizeof(kEndpointRequired) - 1));
  google_protobuf_Value* value =
      google_protobuf_Struct_FieldsEntry_mutable_value(field, arena.ptr());
  google_protobuf_Value_set_bool_value(value, true);
  envoy_api_v2_DiscoveryRequest_add_resource_names(
      request, upb_strview_makez(service_name), arena.ptr());
  envoy_api_v2_DiscoveryRequest_set_type_url(
      request, upb_strview_make(kEdsTypeUrl, sizeof(kEdsTypeUrl) - 1));
  // Encode the request.
  size_t output_length;
  char* output = envoy_api_v2_DiscoveryRequest_serialize(request, arena.ptr(),
                                                         &output_length);
  return grpc_slice_from_copied_buffer(output, output_length);
}

namespace {

grpc_error* ServerAddressParseAndAppend(const XdsLbEndpoint* lb_endpoint,
                                        ServerAddressList* list) {
  // Find the ip:port.
  const XdsEndpoint* endpoint =
      envoy_api_v2_endpoint_LbEndpoint_endpoint(lb_endpoint);
  const XdsAddress* address = envoy_api_v2_endpoint_Endpoint_address(endpoint);
  const XdsSocketAddress* socket_address =
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

char* StringCopy(const upb_strview& strview) {
  char* str = static_cast<char*>(gpr_malloc(strview.size + 1));
  memcpy(str, strview.data, strview.size);
  str[strview.size] = '\0';
  return str;
}

}  // namespace

grpc_error* LocalityParse(const XdsLocalityLbEndpoints* locality_lb_endpoints,
                          XdsLocalityInfo* locality_info) {
  // Parse locality name.
  const XdsLocality* locality =
      envoy_api_v2_endpoint_LocalityLbEndpoints_locality(locality_lb_endpoints);
  locality_info->locality_name = MakeRefCounted<XdsLocalityName>(
      UniquePtr<char>(StringCopy(envoy_api_v2_core_Locality_region(locality))),
      UniquePtr<char>(StringCopy(envoy_api_v2_core_Locality_zone(locality))),
      UniquePtr<char>(
          StringCopy(envoy_api_v2_core_Locality_sub_zone(locality))));
  // Parse the addresses.
  size_t size;
  const XdsLbEndpoint* const* lb_endpoints =
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
  const XdsDiscoveryResponse* response = envoy_api_v2_DiscoveryResponse_parse(
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
  XdsClusterLoadAssignment* cluster_load_assignment =
      envoy_api_v2_ClusterLoadAssignment_parse(
          encoded_cluster_load_assignment.data,
          encoded_cluster_load_assignment.size, arena.ptr());
  const XdsLocalityLbEndpoints* const* endpoints =
      envoy_api_v2_ClusterLoadAssignment_endpoints(cluster_load_assignment,
                                                   &size);
  for (size_t i = 0; i < size; ++i) {
    XdsLocalityInfo locality_info;
    grpc_error* error = LocalityParse(endpoints[i], &locality_info);
    if (error != GRPC_ERROR_NONE) return error;
    update->locality_list.push_back(std::move(locality_info));
  }
  return GRPC_ERROR_NONE;
}

static void populate_timestamp(gpr_timespec timestamp,
                               xds_grpclb_timestamp* timestamp_pb) {
  timestamp_pb->has_seconds = true;
  timestamp_pb->seconds = timestamp.tv_sec;
  timestamp_pb->has_nanos = true;
  timestamp_pb->nanos = timestamp.tv_nsec;
}

static bool encode_string(pb_ostream_t* stream, const pb_field_t* field,
                          void* const* arg) {
  char* str = static_cast<char*>(*arg);
  if (!pb_encode_tag_for_field(stream, field)) return false;
  return pb_encode_string(stream, reinterpret_cast<uint8_t*>(str), strlen(str));
}

static bool encode_drops(pb_ostream_t* stream, const pb_field_t* field,
                         void* const* arg) {
  grpc_core::XdsLbClientStats::DroppedCallCounts* drop_entries =
      static_cast<grpc_core::XdsLbClientStats::DroppedCallCounts*>(*arg);
  if (drop_entries == nullptr) return true;
  for (size_t i = 0; i < drop_entries->size(); ++i) {
    if (!pb_encode_tag_for_field(stream, field)) return false;
    grpc_lb_v1_ClientStatsPerToken drop_message;
    drop_message.load_balance_token.funcs.encode = encode_string;
    drop_message.load_balance_token.arg = (*drop_entries)[i].token.get();
    drop_message.has_num_calls = true;
    drop_message.num_calls = (*drop_entries)[i].count;
    if (!pb_encode_submessage(stream, grpc_lb_v1_ClientStatsPerToken_fields,
                              &drop_message)) {
      return false;
    }
  }
  return true;
}

xds_grpclb_request* xds_grpclb_load_report_request_create_locked(
    grpc_core::XdsLbClientStats* client_stats) {
  xds_grpclb_request* req =
      static_cast<xds_grpclb_request*>(gpr_zalloc(sizeof(xds_grpclb_request)));
  req->has_client_stats = true;
  req->client_stats.has_timestamp = true;
  populate_timestamp(gpr_now(GPR_CLOCK_REALTIME), &req->client_stats.timestamp);
  req->client_stats.has_num_calls_started = true;
  req->client_stats.has_num_calls_finished = true;
  req->client_stats.has_num_calls_finished_with_client_failed_to_send = true;
  req->client_stats.has_num_calls_finished_with_client_failed_to_send = true;
  req->client_stats.has_num_calls_finished_known_received = true;
  req->client_stats.calls_finished_with_drop.funcs.encode = encode_drops;
  grpc_core::UniquePtr<grpc_core::XdsLbClientStats::DroppedCallCounts>
      drop_counts;
  client_stats->GetLocked(
      &req->client_stats.num_calls_started,
      &req->client_stats.num_calls_finished,
      &req->client_stats.num_calls_finished_with_client_failed_to_send,
      &req->client_stats.num_calls_finished_known_received, &drop_counts);
  // Will be deleted in xds_grpclb_request_destroy().
  req->client_stats.calls_finished_with_drop.arg = drop_counts.release();
  return req;
}

void xds_grpclb_request_destroy(xds_grpclb_request* request) {
  if (request->has_client_stats) {
    grpc_core::XdsLbClientStats::DroppedCallCounts* drop_entries =
        static_cast<grpc_core::XdsLbClientStats::DroppedCallCounts*>(
            request->client_stats.calls_finished_with_drop.arg);
    grpc_core::Delete(drop_entries);
  }
  gpr_free(request);
}

}  // namespace grpc_core
