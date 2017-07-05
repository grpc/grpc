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

#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/support/string.h"

static void create_sockets(int sv[2]) {
  int flags;
  grpc_create_socketpair_if_unix(sv);
  flags = fcntl(sv[0], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
  flags = fcntl(sv[1], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[1], F_SETFL, flags | O_NONBLOCK) == 0);
  GPR_ASSERT(grpc_set_socket_no_sigpipe_if_possible(sv[0]) == GRPC_ERROR_NONE);
  GPR_ASSERT(grpc_set_socket_no_sigpipe_if_possible(sv[1]) == GRPC_ERROR_NONE);
}

grpc_endpoint_pair grpc_iomgr_create_endpoint_pair(const char *name,
                                                   grpc_channel_args *args) {
  int sv[2];
  grpc_endpoint_pair p;
  char *final_name;
  create_sockets(sv);

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  gpr_asprintf(&final_name, "%s:client", name);
  p.client = grpc_tcp_create(&exec_ctx, grpc_fd_create(sv[1], final_name), args,
                             "socketpair-server");
  gpr_free(final_name);
  gpr_asprintf(&final_name, "%s:server", name);
  p.server = grpc_tcp_create(&exec_ctx, grpc_fd_create(sv[0], final_name), args,
                             "socketpair-client");
  gpr_free(final_name);

  grpc_exec_ctx_finish(&exec_ctx);
  return p;
}

#endif
