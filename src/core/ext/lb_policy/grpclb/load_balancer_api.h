/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_CORE_EXT_LB_POLICY_GRPCLB_LOAD_BALANCER_API_H
#define GRPC_CORE_EXT_LB_POLICY_GRPCLB_LOAD_BALANCER_API_H

#include <grpc/support/slice_buffer.h>

#include "src/core/ext/client_config/lb_policy_factory.h"
#include "src/core/ext/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GRPC_GRPCLB_SERVICE_NAME_MAX_LENGTH 128

typedef grpc_lb_v1_LoadBalanceRequest grpc_grpclb_request;
typedef grpc_lb_v1_LoadBalanceResponse grpc_grpclb_response;
typedef grpc_lb_v1_Server grpc_grpclb_server;
typedef grpc_lb_v1_Duration grpc_grpclb_duration;
typedef struct grpc_grpclb_serverlist {
  grpc_grpclb_server **servers;
  size_t num_servers;
  grpc_grpclb_duration expiration_interval;
} grpc_grpclb_serverlist;

/** Create a request for a gRPC LB service under \a lb_service_name */
grpc_grpclb_request *grpc_grpclb_request_create(const char *lb_service_name);

/** Protocol Buffers v3-encode \a request */
gpr_slice grpc_grpclb_request_encode(const grpc_grpclb_request *request);

/** Destroy \a request */
void grpc_grpclb_request_destroy(grpc_grpclb_request *request);

/** Parse (ie, decode) the bytes in \a encoded_response as a \a
 * grpc_grpclb_response */
grpc_grpclb_response *grpc_grpclb_response_parse(gpr_slice encoded_response);

/** Destroy \a serverlist */
void grpc_grpclb_destroy_serverlist(grpc_grpclb_serverlist *serverlist);

/** Parse the list of servers from an encoded \a grpc_grpclb_response */
grpc_grpclb_serverlist *grpc_grpclb_response_parse_serverlist(
    gpr_slice encoded_response);

/** Destroy \a response */
void grpc_grpclb_response_destroy(grpc_grpclb_response *response);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_LB_POLICY_GRPCLB_LOAD_BALANCER_API_H */
