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

#ifndef GRPC_CORE_LIB_IOMGR_TCP_CLIENT_POSIX_H
#define GRPC_CORE_LIB_IOMGR_TCP_CLIENT_POSIX_H

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/tcp_client.h"

/* Create an endpoint from a connected grpc_fd. */
grpc_endpoint* grpc_tcp_client_create_from_fd(
    grpc_fd* fd, const grpc_channel_args* channel_args, const char* addr_str);

/* Return a configured, unbound, unconnected TCP client grpc_fd. Sets
   *mapped_addr based on the socket type created. */
grpc_error* grpc_tcp_client_prepare_fd(const grpc_channel_args* channel_args,
                                       const grpc_resolved_address* addr,
                                       grpc_resolved_address* mapped_addr,
                                       grpc_fd** fdobj);

/* Connect a configured TCP client grpc_fd. Takes ownership of fdobj. */
void grpc_tcp_client_create_from_prepared_fd(
    grpc_pollset_set* interested_parties, grpc_closure* closure, grpc_fd* fdobj,
    const grpc_channel_args* channel_args, const grpc_resolved_address* addr,
    grpc_millis deadline, grpc_endpoint** ep);

#endif /* GRPC_CORE_LIB_IOMGR_TCP_CLIENT_POSIX_H */
