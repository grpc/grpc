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

#include <grpc/slice_buffer.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GRPC_GRPCLB_SERVICE_NAME_MAX_LENGTH 128

typedef grpc_lb_v1_Server_ip_address_t grpc_grpclb_ip_address;
typedef grpc_lb_v1_LoadBalanceRequest grpc_grpclb_request;
typedef grpc_lb_v1_InitialLoadBalanceResponse grpc_grpclb_initial_response;
typedef grpc_lb_v1_Server grpc_grpclb_server;
typedef grpc_lb_v1_Duration grpc_grpclb_duration;
typedef struct {
  grpc_grpclb_server **servers;
  size_t num_servers;
  grpc_grpclb_duration expiration_interval;
} grpc_grpclb_serverlist;

/** Create a request for a gRPC LB service under \a lb_service_name */
grpc_grpclb_request *grpc_grpclb_request_create(const char *lb_service_name);
grpc_grpclb_request *grpc_grpclb_load_report_request_create_locked(
    grpc_grpclb_client_stats *client_stats);

/** Protocol Buffers v3-encode \a request */
grpc_slice grpc_grpclb_request_encode(const grpc_grpclb_request *request);

/** Destroy \a request */
void grpc_grpclb_request_destroy(grpc_grpclb_request *request);

/** Parse (ie, decode) the bytes in \a encoded_grpc_grpclb_response as a \a
 * grpc_grpclb_initial_response */
grpc_grpclb_initial_response *grpc_grpclb_initial_response_parse(
    grpc_slice encoded_grpc_grpclb_response);

/** Parse the list of servers from an encoded \a grpc_grpclb_response */
grpc_grpclb_serverlist *grpc_grpclb_response_parse_serverlist(
    grpc_slice encoded_grpc_grpclb_response);

/** Return a copy of \a sl. The caller is responsible for calling \a
 * grpc_grpclb_destroy_serverlist on the returned copy. */
grpc_grpclb_serverlist *grpc_grpclb_serverlist_copy(
    const grpc_grpclb_serverlist *sl);

bool grpc_grpclb_serverlist_equals(const grpc_grpclb_serverlist *lhs,
                                   const grpc_grpclb_serverlist *rhs);

bool grpc_grpclb_server_equals(const grpc_grpclb_server *lhs,
                               const grpc_grpclb_server *rhs);

/** Destroy \a serverlist */
void grpc_grpclb_destroy_serverlist(grpc_grpclb_serverlist *serverlist);

/** Compare \a lhs against \a rhs and return 0 if \a lhs and \a rhs are equal,
 * < 0 if \a lhs represents a duration shorter than \a rhs and > 0 otherwise */
int grpc_grpclb_duration_compare(const grpc_grpclb_duration *lhs,
                                 const grpc_grpclb_duration *rhs);

gpr_timespec grpc_grpclb_duration_to_timespec(
    grpc_grpclb_duration *duration_pb);

/** Destroy \a initial_response */
void grpc_grpclb_initial_response_destroy(
    grpc_grpclb_initial_response *response);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_LOAD_BALANCER_API_H \
          */
