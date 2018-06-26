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

#ifndef GRPC_SERVER_LOAD_REPORTING_H
#define GRPC_SERVER_LOAD_REPORTING_H

#include <grpc/support/port_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

// Metadata key for the gRPC LB load balancing token.
// The value corresponding to this key is an opaque token associated with each
// server in the serverlist, which is sent to the client from the balancer. The
// client must include the token of the picked server into the initial metadata
// when it starts a call to that server. The token is used by the server to
// verify the request and to allow the server to report load to the gRPC LB
// system. The token is also used in client stats for reporting dropped calls.
#define GRPC_LB_TOKEN_MD_KEY "lb-token"

// Metadata key for the gRPC LB cost reporting.
// The value corresponding to this key is an opaque binary blob reported by the
// server as part of its trailing metadata containing cost information for the
// call.
#define GRPC_LB_COST_MD_KEY "lb-cost-bin"

#ifdef __cplusplus
}
#endif

#endif /* GRPC_SERVER_LOAD_REPORTING_H */
