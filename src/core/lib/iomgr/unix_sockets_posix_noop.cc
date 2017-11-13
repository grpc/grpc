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

#include "src/core/lib/iomgr/unix_sockets_posix.h"

#ifndef GRPC_HAVE_UNIX_SOCKET

#include <grpc/support/log.h>

void grpc_create_socketpair_if_unix(int sv[2]) {
  // TODO: Either implement this for the non-Unix socket case or make
  // sure that it is never called in any such case. Until then, leave an
  // assertion to notify if this gets called inadvertently
  GPR_ASSERT(0);
}

grpc_error* grpc_resolve_unix_domain_address(
    const char* name, grpc_resolved_addresses** addresses) {
  *addresses = NULL;
  return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "Unix domain sockets are not supported on Windows");
}

int grpc_is_unix_socket(const grpc_resolved_address* addr) { return false; }

void grpc_unlink_if_unix_domain_socket(const grpc_resolved_address* addr) {}

char* grpc_sockaddr_to_uri_unix_if_possible(const grpc_resolved_address* addr) {
  return NULL;
}

#endif
