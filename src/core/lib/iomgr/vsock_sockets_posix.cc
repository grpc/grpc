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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#include "src/core/lib/iomgr/vsock_sockets_posix.h"

#ifdef GRPC_HAVE_UNIX_SOCKET

#include "src/core/lib/iomgr/sockaddr.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"

#include <sys/socket.h>
#include <linux/vm_sockets.h>

int grpc_is_vsock_socket(const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  return addr->sa_family == AF_VSOCK;
}

grpc_error* grpc_resolve_vsock_domain_address(const char* cid, const char* port,
                                             grpc_resolved_addresses** addrs) {
  struct sockaddr_vm* vm;

  *addrs = static_cast<grpc_resolved_addresses*>(
      gpr_malloc(sizeof(grpc_resolved_addresses)));
  (*addrs)->naddrs = 1;
  (*addrs)->addrs = static_cast<grpc_resolved_address*>(
      gpr_malloc(sizeof(grpc_resolved_address)));
  vm = reinterpret_cast<struct sockaddr_vm*>((*addrs)->addrs->addr);
  memset(vm, 0, sizeof(struct sockaddr_vm));
  vm->svm_family = AF_VSOCK;
  vm->svm_port = atoi(port);
  vm->svm_cid = atoi(cid);
  (*addrs)->addrs->len =
      static_cast<socklen_t>(sizeof(struct sockaddr_vm));

  return GRPC_ERROR_NONE;
}

char* grpc_sockaddr_to_uri_vsock_if_possible(
    const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  if (addr->sa_family != AF_VSOCK) {
    return nullptr;
  }

  char* result;
  gpr_asprintf(&result, "vsock://%d:%d", ((struct sockaddr_vm*)addr)->svm_cid,
      ((struct sockaddr_vm*)addr)->svm_port);
  return result;
}

#else

int grpc_is_vsock_socket(const grpc_resolved_address* resolved_addr) {
  return false;
}

#endif