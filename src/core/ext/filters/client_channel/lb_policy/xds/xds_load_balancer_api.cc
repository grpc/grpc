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

#include "pb_decode.h"
#include "pb_encode.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_load_balancer_api.h"

#include <grpc/support/alloc.h>

/* invoked once for every Server in ServerList */
static bool count_serverlist(pb_istream_t* stream, const pb_field_t* field,
                             void** arg) {
  xds_grpclb_serverlist* sl = static_cast<xds_grpclb_serverlist*>(*arg);
  xds_grpclb_server server;
  if (GPR_UNLIKELY(!pb_decode(stream, grpc_lb_v1_Server_fields, &server))) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(stream));
    return false;
  }
  ++sl->num_servers;
  return true;
}

typedef struct decode_serverlist_arg {
  /* The decoding callback is invoked once per server in serverlist. Remember
   * which index of the serverlist are we currently decoding */
  size_t decoding_idx;
  /* The decoded serverlist */
  xds_grpclb_serverlist* serverlist;
} decode_serverlist_arg;

/* invoked once for every Server in ServerList */
static bool decode_serverlist(pb_istream_t* stream, const pb_field_t* field,
                              void** arg) {
  decode_serverlist_arg* dec_arg = static_cast<decode_serverlist_arg*>(*arg);
  GPR_ASSERT(dec_arg->serverlist->num_servers >= dec_arg->decoding_idx);
  xds_grpclb_server* server =
      static_cast<xds_grpclb_server*>(gpr_zalloc(sizeof(xds_grpclb_server)));
  if (GPR_UNLIKELY(!pb_decode(stream, grpc_lb_v1_Server_fields, server))) {
    gpr_free(server);
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(stream));
    return false;
  }
  dec_arg->serverlist->servers[dec_arg->decoding_idx++] = server;
  return true;
}

xds_discovery_request*
xds_discovery_request_create(
    const char* lb_service_name,
    upb_arena *arena) {
  xds_discovery_request* request = envoy_api_v2_DiscoveryRequest_new(arena);
  // Set the URL for eds request
  static const char url[] = "type.googleapis.com/envoy.api.v2.Cluster";
  envoy_api_v2_DiscoveryRequest_set_type_url(request,
                                             upb_stringview_make(url, sizeof(url)));
  // Set the service name in resources
  upb_array* resources = upb_array_new(UPB_TYPE_STRING, arena);
//  upb_array_set(resources, 0, upb_stringview_make(lb_service_name, sizeof(lb_service_name)));
  envoy_api_v2_DiscoveryRequest_set_resource_names(request, resources);
  return request;
}

grpc_slice xds_request_encode(
    const xds_discovery_request* request,
    upb_arena *arena) {
  // seralize the request
  size_t request_len;
  char *serialize_request = envoy_api_v2_DiscoveryRequest_serialize(
      request, arena, &request_len);

  return grpc_slice_from_copied_buffer(serialize_request, request_len);
}

void xds_request_destroy(xds_discovery_request* request) {
  gpr_free(request);
}

typedef grpc_lb_v1_LoadBalanceResponse xds_grpclb_response;
xds_grpclb_initial_response* xds_grpclb_initial_response_parse(
    grpc_slice encoded_xds_grpclb_response) {
  pb_istream_t stream =
      pb_istream_from_buffer(GRPC_SLICE_START_PTR(encoded_xds_grpclb_response),
                             GRPC_SLICE_LENGTH(encoded_xds_grpclb_response));
  xds_grpclb_response res;
  memset(&res, 0, sizeof(xds_grpclb_response));
  if (GPR_UNLIKELY(
          !pb_decode(&stream, grpc_lb_v1_LoadBalanceResponse_fields, &res))) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&stream));
    return nullptr;
  }

  if (!res.has_initial_response) return nullptr;

  xds_grpclb_initial_response* initial_res =
      static_cast<xds_grpclb_initial_response*>(
          gpr_malloc(sizeof(xds_grpclb_initial_response)));
  memcpy(initial_res, &res.initial_response,
         sizeof(xds_grpclb_initial_response));

  return initial_res;
}

xds_grpclb_serverlist* xds_grpclb_response_parse_serverlist(
    grpc_slice encoded_xds_grpclb_response) {
  pb_istream_t stream =
      pb_istream_from_buffer(GRPC_SLICE_START_PTR(encoded_xds_grpclb_response),
                             GRPC_SLICE_LENGTH(encoded_xds_grpclb_response));
  pb_istream_t stream_at_start = stream;
  xds_grpclb_serverlist* sl = static_cast<xds_grpclb_serverlist*>(
      gpr_zalloc(sizeof(xds_grpclb_serverlist)));
  xds_grpclb_response res;
  memset(&res, 0, sizeof(xds_grpclb_response));
  // First pass: count number of servers.
  res.server_list.servers.funcs.decode = count_serverlist;
  res.server_list.servers.arg = sl;
  bool status = pb_decode(&stream, grpc_lb_v1_LoadBalanceResponse_fields, &res);
  if (GPR_UNLIKELY(!status)) {
    gpr_free(sl);
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&stream));
    return nullptr;
  }
  // Second pass: populate servers.
  if (sl->num_servers > 0) {
    sl->servers = static_cast<xds_grpclb_server**>(
        gpr_zalloc(sizeof(xds_grpclb_server*) * sl->num_servers));
    decode_serverlist_arg decode_arg;
    memset(&decode_arg, 0, sizeof(decode_arg));
    decode_arg.serverlist = sl;
    res.server_list.servers.funcs.decode = decode_serverlist;
    res.server_list.servers.arg = &decode_arg;
    status = pb_decode(&stream_at_start, grpc_lb_v1_LoadBalanceResponse_fields,
                       &res);
    if (GPR_UNLIKELY(!status)) {
      xds_grpclb_destroy_serverlist(sl);
      gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&stream));
      return nullptr;
    }
  }
  return sl;
}

void xds_grpclb_destroy_serverlist(xds_grpclb_serverlist* serverlist) {
  if (serverlist == nullptr) {
    return;
  }
  for (size_t i = 0; i < serverlist->num_servers; i++) {
    gpr_free(serverlist->servers[i]);
  }
  gpr_free(serverlist->servers);
  gpr_free(serverlist);
}

xds_grpclb_serverlist* xds_grpclb_serverlist_copy(
    const xds_grpclb_serverlist* sl) {
  xds_grpclb_serverlist* copy = static_cast<xds_grpclb_serverlist*>(
      gpr_zalloc(sizeof(xds_grpclb_serverlist)));
  copy->num_servers = sl->num_servers;
  copy->servers = static_cast<xds_grpclb_server**>(
      gpr_malloc(sizeof(xds_grpclb_server*) * sl->num_servers));
  for (size_t i = 0; i < sl->num_servers; i++) {
    copy->servers[i] =
        static_cast<xds_grpclb_server*>(gpr_malloc(sizeof(xds_grpclb_server)));
    memcpy(copy->servers[i], sl->servers[i], sizeof(xds_grpclb_server));
  }
  return copy;
}

bool xds_grpclb_serverlist_equals(const xds_grpclb_serverlist* lhs,
                                  const xds_grpclb_serverlist* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  if (lhs->num_servers != rhs->num_servers) {
    return false;
  }
  for (size_t i = 0; i < lhs->num_servers; i++) {
    if (!xds_grpclb_server_equals(lhs->servers[i], rhs->servers[i])) {
      return false;
    }
  }
  return true;
}

bool xds_grpclb_server_equals(const xds_grpclb_server* lhs,
                              const xds_grpclb_server* rhs) {
  return memcmp(lhs, rhs, sizeof(xds_grpclb_server)) == 0;
}

void xds_grpclb_initial_response_destroy(
    xds_grpclb_initial_response* response) {
  gpr_free(response);
}
