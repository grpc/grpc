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
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_HAVE_UNIX_SOCKET

#include "src/core/lib/iomgr/sockaddr.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include "src/core/lib/iomgr/unix_sockets_posix.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

void grpc_create_socketpair_if_unix(int sv[2]) {
  GPR_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
}

grpc_error *grpc_resolve_unix_domain_address(const char *name,
                                             grpc_resolved_addresses **addrs) {
  struct sockaddr_un *un;
  if (strlen(name) > GPR_ARRAY_SIZE(((struct sockaddr_un *)0)->sun_path) - 1) {
    char *err_msg;
    grpc_error *err;
    gpr_asprintf(&err_msg,
                 "Path name should not have more than %" PRIuPTR " characters.",
                 GPR_ARRAY_SIZE(un->sun_path) - 1);
    err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(err_msg);
    gpr_free(err_msg);
    return err;
  }
  *addrs = gpr_malloc(sizeof(grpc_resolved_addresses));
  (*addrs)->naddrs = 1;
  (*addrs)->addrs = gpr_malloc(sizeof(grpc_resolved_address));
  un = (struct sockaddr_un *)(*addrs)->addrs->addr;
  un->sun_family = AF_UNIX;
  strcpy(un->sun_path, name);
  (*addrs)->addrs->len = strlen(un->sun_path) + sizeof(un->sun_family) + 1;
  return GRPC_ERROR_NONE;
}

int grpc_is_unix_socket(const grpc_resolved_address *resolved_addr) {
  const struct sockaddr *addr = (const struct sockaddr *)resolved_addr->addr;
  return addr->sa_family == AF_UNIX;
}

void grpc_unlink_if_unix_domain_socket(
    const grpc_resolved_address *resolved_addr) {
  const struct sockaddr *addr = (const struct sockaddr *)resolved_addr->addr;
  if (addr->sa_family != AF_UNIX) {
    return;
  }
  struct sockaddr_un *un = (struct sockaddr_un *)resolved_addr->addr;
  struct stat st;

  if (stat(un->sun_path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFSOCK) {
    unlink(un->sun_path);
  }
}

char *grpc_sockaddr_to_uri_unix_if_possible(
    const grpc_resolved_address *resolved_addr) {
  const struct sockaddr *addr = (const struct sockaddr *)resolved_addr->addr;
  if (addr->sa_family != AF_UNIX) {
    return NULL;
  }

  char *result;
  gpr_asprintf(&result, "unix:%s", ((struct sockaddr_un *)addr)->sun_path);
  return result;
}

#endif
