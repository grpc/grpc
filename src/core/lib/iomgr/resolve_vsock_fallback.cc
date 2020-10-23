/*
 *
 * Copyright 2020 gRPC authors.
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

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/resolve_address.h"

#ifndef GRPC_HAVE_LINUX_VSOCK

grpc_error* grpc_resolve_vsock_address(const char* name,
                                       grpc_resolved_addresses** addrs) {
  return GRPC_ERROR_CREATE_FROM_STATIC_STRING("vsock not supported");
}

int grpc_is_vsock(const grpc_resolved_address* resolved_addr) {
  return 0;
}

char* grpc_sockaddr_to_uri_vsock_if_possible(
    const grpc_resolved_address* resolved_addr) {
  return nullptr;
}

#endif /* GRPC_HAVE_LINUX_VSOCK */
