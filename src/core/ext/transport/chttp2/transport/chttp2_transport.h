/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CHTTP2_TRANSPORT_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CHTTP2_TRANSPORT_H

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/transport/transport.h"

extern grpc_core::TraceFlag grpc_http_trace;
extern grpc_core::TraceFlag grpc_trace_http2_stream_state;
extern grpc_core::DebugOnlyTraceFlag grpc_trace_chttp2_refcount;

#ifdef __cplusplus
extern "C" {
#endif

grpc_transport* grpc_create_chttp2_transport(
    grpc_exec_ctx* exec_ctx, const grpc_channel_args* channel_args,
    grpc_endpoint* ep, int is_client);

/// Takes ownership of \a read_buffer, which (if non-NULL) contains
/// leftover bytes previously read from the endpoint (e.g., by handshakers).
void grpc_chttp2_transport_start_reading(grpc_exec_ctx* exec_ctx,
                                         grpc_transport* transport,
                                         grpc_slice_buffer* read_buffer);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_CHTTP2_TRANSPORT_H */
