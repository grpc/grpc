/*
 *
 * Copyright 2019 gRPC authors.
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

#ifndef GRPC_CORE_LIB_IOMGR_VSOCK_SOCKETS_POSIX_H
#define GRPC_CORE_LIB_IOMGR_VSOCK_SOCKETS_POSIX_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#include <grpc/support/string_util.h>

#include "src/core/lib/iomgr/resolve_address.h"

int grpc_is_vsock_socket(const grpc_resolved_address* resolved_addr);



grpc_error* grpc_resolve_vsock_domain_address(const char* cid, const char* port,
                                             grpc_resolved_addresses** addrs);

char* grpc_sockaddr_to_uri_vsock_if_possible(const grpc_resolved_address* resolved_addr);



#endif /* GRPC_CORE_LIB_IOMGR_VSOCK_SOCKETS_POSIX_H */
