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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_HAVE_LINUX_VSOCK

#include "src/core/lib/iomgr/sockaddr.h"

#include <linux/vm_sockets.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include "src/core/lib/iomgr/unix_sockets_posix.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"

grpc_error* grpc_resolve_vsock_address(const char* name,
                                       grpc_resolved_addresses** addrs) {
  struct sockaddr_vm *vm;
  unsigned int cid;
  unsigned int port;

  if (sscanf(name, "%u:%u", &cid, &port) != 2) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to parse cid:port pair");
  }

  *addrs = static_cast<grpc_resolved_addresses*>(
      gpr_malloc(sizeof(grpc_resolved_addresses)));
  (*addrs)->naddrs = 1;
  (*addrs)->addrs = static_cast<grpc_resolved_address*>(
      gpr_zalloc(sizeof(grpc_resolved_address)));
  vm = (struct sockaddr_vm *)(*addrs)->addrs->addr;
  vm->svm_family = AF_VSOCK;
  vm->svm_cid = cid;
  vm->svm_port = port;
  (*addrs)->addrs->len = sizeof(struct sockaddr_vm);
  return GRPC_ERROR_NONE;
}

int grpc_is_vsock(const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  return addr->sa_family == AF_VSOCK;
}

char* grpc_sockaddr_to_uri_vsock_if_possible(
    const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);

  if (addr->sa_family != AF_VSOCK) {
      return nullptr;
  }

  char *result;
  struct sockaddr_vm *vm = (struct sockaddr_vm*)addr;
  gpr_asprintf(&result, "vsock:%u:%u", vm->svm_cid, vm->svm_port);
  return result;
}

#endif /* GRPC_HAVE_LINUX_VSOCK */
