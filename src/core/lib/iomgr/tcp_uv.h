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

#ifndef GRPC_CORE_LIB_IOMGR_TCP_UV_H
#define GRPC_CORE_LIB_IOMGR_TCP_UV_H
/*
   Low level TCP "bottom half" implementation, for use by transports built on
   top of a TCP connection.

   Note that this file does not (yet) include APIs for creating the socket in
   the first place.

   All calls passing slice transfer ownership of a slice refcount unless
   otherwise specified.
*/

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/endpoint.h"

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_UV

#include <uv.h>

extern grpc_core::TraceFlag grpc_tcp_trace;

#define GRPC_TCP_DEFAULT_READ_SLICE_SIZE 8192

#ifdef __cplusplus
extern "C" {
#endif

grpc_endpoint* grpc_tcp_create(uv_tcp_t* handle,
                               grpc_resource_quota* resource_quota,
                               char* peer_string);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_UV */

#endif /* GRPC_CORE_LIB_IOMGR_TCP_UV_H */
