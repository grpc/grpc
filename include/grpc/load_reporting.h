/*
 *
 * Copyright 2017, Google Inc.
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

#ifndef GRPC_LOAD_REPORTING_H
#define GRPC_LOAD_REPORTING_H

#include <grpc/impl/codegen/port_platform.h>
#include <grpc/slice.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Metadata key for the gRPC LB load balancer token.
 *
 * The value corresponding to this key is an opaque token that is given to the
 * frontend as part of each pick; the frontend sends this token to the backend
 * in each request it sends when using that pick. The token is used by the
 * backend to verify the request and to allow the backend to report load to the
 * gRPC LB system. */
#define GRPC_LB_TOKEN_MD_KEY "lb-token"

/** A sequence of values for load reporting purposes */
typedef struct grpc_load_reporting_cost_context {
  grpc_slice *values;
  size_t values_count;
} grpc_load_reporting_cost_context;

#ifdef __cplusplus
}
#endif

#endif /* GRPC_LOAD_REPORTING_H */
