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

#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_load_balancer_api.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/gpr/murmur_hash.h"

namespace grpc_core {

namespace {

constexpr char kEdsTypeUrl[] =
    "type.googleapis.com/envoy.api.v2.ClusterLoadAssignment";
constexpr char kEndpointRequired[] = "endpointRequired";

}  // namespace

grpc_slice XdsRequestCreateAndEncode(const char* service_name) {
  upb::Arena arena;
  // Create a request.
  XdsDiscoveryRequest* request = envoy_api_v2_DiscoveryRequest_new(arena.ptr());
  XdsNode* node =
      envoy_api_v2_DiscoveryRequest_mutable_node(request, arena.ptr());
  XdsStruct* metadata =
      envoy_api_v2_core_Node_mutable_metadata(node, arena.ptr());
  XdsFieldsEntry* field =
      google_protobuf_Struct_add_fields(metadata, arena.ptr());
  google_protobuf_Struct_FieldsEntry_set_key(
      field, upb_strview_make(kEndpointRequired, strlen(kEndpointRequired)));
  XdsValue* value =
      google_protobuf_Struct_FieldsEntry_mutable_value(field, arena.ptr());
  google_protobuf_Value_set_bool_value(value, true);
  envoy_api_v2_DiscoveryRequest_add_resource_names(request, upb_strview_makez(service_name),
                                                   arena.ptr());
  envoy_api_v2_DiscoveryRequest_set_type_url(
      request, upb_strview_make(kEdsTypeUrl, strlen(kEdsTypeUrl)));
  // Encode the request.
  size_t output_length;
  char* output = envoy_api_v2_DiscoveryRequest_serialize(request, arena.ptr(),
                                                         &output_length);
  return grpc_slice_from_copied_buffer(output, output_length);
}

namespace {

void ServerAddressParseAndAppend(const XdsLbEndpoint* lb_endpoint,
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
    gpr_log(GPR_ERROR, "Invalid port '%d'. Ignoring.", port);
    return;
  }
  // Populate grpc_resolved_address.
  grpc_resolved_address addr;
  char* address_str = static_cast<char*>(gpr_malloc(address_strview.size));
  memcpy(address_str, address_strview.data, address_strview.size);
  address_str[address_strview.size] = '\0';
  grpc_string_to_sockaddr(&addr, address_str, port);
  gpr_free(address_str);
  // Append the address to the list.
  list->emplace_back(addr, nullptr);
}

XdsLocalityUpdateArgs LocalityParse(
    const XdsLocalityLbEndpoints* locality_lb_endpoints) {
  upb::Arena arena;
  XdsLocalityUpdateArgs update_args;
  update_args.serverlist = MakeUnique<ServerAddressList>();
  // Parse locality_name.
  const XdsLocality* locality =
      envoy_api_v2_endpoint_LocalityLbEndpoints_locality(locality_lb_endpoints);
  size_t size;
  char* serialized_locality = envoy_api_v2_core_Locality_serialize(locality, arena.ptr(), &size);
  grpc_slice serialized_locality_slice = grpc_slice_from_copied_buffer(serialized_locality, size);
  update_args.locality_name = serialized_locality_slice;
  // Parse the addresses.
  const XdsLbEndpoint* const * lb_endpoints =
      envoy_api_v2_endpoint_LocalityLbEndpoints_lb_endpoints(
          locality_lb_endpoints, &size);
  for (size_t i = 0; i < size; ++i) {
    ServerAddressParseAndAppend(lb_endpoints[i], update_args.serverlist.get());
  }
  // Parse the lb_weight and priority.
  const google_protobuf_UInt32Value* lb_weight =
      envoy_api_v2_endpoint_LocalityLbEndpoints_load_balancing_weight(
          locality_lb_endpoints);
  update_args.lb_weight = google_protobuf_UInt32Value_value(lb_weight);
  update_args.priority =
      envoy_api_v2_endpoint_LocalityLbEndpoints_priority(locality_lb_endpoints);
  return update_args;
}

}  // namespace

UniquePtr<XdsLoadUpdateArgs> XdsResponseDecodeAndParse(
    const grpc_slice& encoded_response) {
  upb::Arena arena;
  // Decode the response.
  const XdsDiscoveryResponse* response =
      envoy_api_v2_DiscoveryResponse_parse(
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(encoded_response)),
                           GRPC_SLICE_LENGTH(encoded_response),
          arena.ptr());
  // Parse the response.
  if (response == nullptr) return nullptr;
  // Check the type_url of the response.
  upb_strview type_url = envoy_api_v2_DiscoveryResponse_type_url(response);
  upb_strview expected_type_url = upb_strview_makez(kEdsTypeUrl);
  if (!upb_strview_eql(type_url, expected_type_url)) {
    gpr_log(GPR_ERROR, "Resource is not EDS");
    return nullptr;
  }
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const * resources =
      envoy_api_v2_DiscoveryResponse_resources(response, &size);
  if (size < 1) {
    gpr_log(GPR_ERROR, "EDS response contains 0 resource.");
    return nullptr;
  }
  // Check the type_url of the resource.
  type_url = google_protobuf_Any_type_url(resources[0]);
  if (!upb_strview_eql(type_url, expected_type_url)) {
    gpr_log(GPR_ERROR, "Resource is not EDS");
    return nullptr;
  }
  // Get the cluster_load_assignment.
  auto update_args = MakeUnique<XdsLoadUpdateArgs>();
  upb_strview encoded_cluster_load_assignment =
      google_protobuf_Any_value(resources[0]);
  XdsClusterLoadAssignment* cluster_load_assignment =
      envoy_api_v2_ClusterLoadAssignment_parse(
          encoded_cluster_load_assignment.data, encoded_cluster_load_assignment.size, arena.ptr());
  const XdsLocalityLbEndpoints* const * endpoints =
      envoy_api_v2_ClusterLoadAssignment_endpoints(cluster_load_assignment,
                                                   &size);
  for (size_t i = 0; i < size; ++i) {
    update_args->localities.push_back(LocalityParse(endpoints[i]));
  }
  return update_args;
}

xds_grpclb_request* xds_grpclb_load_report_request_create_locked(
    grpc_core::XdsLbClientStats* client_stats) {
  xds_grpclb_request* req =
      static_cast<xds_grpclb_request*>(gpr_zalloc(sizeof(xds_grpclb_request)));
  req->has_client_stats = true;
  req->client_stats.has_timestamp = true;
  req->client_stats.has_num_calls_started = true;
  req->client_stats.has_num_calls_finished = true;
  req->client_stats.has_num_calls_finished_with_client_failed_to_send = true;
  req->client_stats.has_num_calls_finished_with_client_failed_to_send = true;
  req->client_stats.has_num_calls_finished_known_received = true;
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
