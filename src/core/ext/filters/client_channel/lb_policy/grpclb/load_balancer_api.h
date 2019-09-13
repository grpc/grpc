/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_LOAD_BALANCER_API_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_LOAD_BALANCER_API_H

#include <grpc/support/port_platform.h>

#include <grpc/slice_buffer.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/proto/grpc/lb/v1/load_balancer.upb.h"

#define GRPC_GRPCLB_SERVICE_NAME_MAX_LENGTH 128
#define GRPC_GRPCLB_SERVER_IP_ADDRESS_MAX_SIZE 16
#define GRPC_GRPCLB_SERVER_LOAD_BALANCE_TOKEN_MAX_SIZE 50

namespace grpc_core {

typedef grpc_lb_v1_LoadBalanceRequest grpc_grpclb_request;
typedef grpc_lb_v1_LoadBalanceResponse grpc_grpclb_response;
typedef grpc_lb_v1_InitialLoadBalanceResponse grpc_grpclb_initial_response;
typedef google_protobuf_Duration grpc_grpclb_duration;
typedef google_protobuf_Timestamp grpc_grpclb_timestamp;

typedef struct {
  int32_t size;
  char data[GRPC_GRPCLB_SERVER_IP_ADDRESS_MAX_SIZE];
} grpc_grpclb_server_ip_address;

// Contains server information. When the drop field is not true, use the other
// fields.
typedef struct {
  grpc_grpclb_server_ip_address ip_address;
  int32_t port;
  char load_balance_token[GRPC_GRPCLB_SERVER_LOAD_BALANCE_TOKEN_MAX_SIZE];
  bool drop;
} grpc_grpclb_server;

typedef struct {
  grpc_grpclb_server** servers;
  size_t num_servers;
} grpc_grpclb_serverlist;

/**
 * Create a request for a gRPC LB service under \a lb_service_name.
 * \a lb_service_name should be alive when returned request is being used.
 */
grpc_grpclb_request* grpc_grpclb_request_create(const char* lb_service_name,
                                                upb_arena* arena);
grpc_grpclb_request* grpc_grpclb_load_report_request_create(
    grpc_core::GrpcLbClientStats* client_stats, upb_arena* arena);

/** Protocol Buffers v3-encode \a request */
grpc_slice grpc_grpclb_request_encode(const grpc_grpclb_request* request,
                                      upb_arena* arena);

/** Parse (ie, decode) the bytes in \a encoded_grpc_grpclb_response as a \a
 * grpc_grpclb_initial_response */
const grpc_grpclb_initial_response* grpc_grpclb_initial_response_parse(
    const grpc_slice& encoded_grpc_grpclb_response, upb_arena* arena);

/** Parse the list of servers from an encoded \a grpc_grpclb_response */
grpc_grpclb_serverlist* grpc_grpclb_response_parse_serverlist(
    const grpc_slice& encoded_grpc_grpclb_response);

/** Return a copy of \a sl. The caller is responsible for calling \a
 * grpc_grpclb_destroy_serverlist on the returned copy. */
grpc_grpclb_serverlist* grpc_grpclb_serverlist_copy(
    const grpc_grpclb_serverlist* sl);

bool grpc_grpclb_serverlist_equals(const grpc_grpclb_serverlist* lhs,
                                   const grpc_grpclb_serverlist* rhs);

bool grpc_grpclb_server_equals(const grpc_grpclb_server* lhs,
                               const grpc_grpclb_server* rhs);

/** Destroy \a serverlist */
void grpc_grpclb_destroy_serverlist(grpc_grpclb_serverlist* serverlist);

grpc_millis grpc_grpclb_duration_to_millis(
    const grpc_grpclb_duration* duration_pb);

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_LOAD_BALANCER_API_H \
        */
