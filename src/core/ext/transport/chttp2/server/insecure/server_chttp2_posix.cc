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

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/support/log.h>

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/server.h"

void grpc_server_add_insecure_channel_from_fd(grpc_server* server,
                                              void* reserved, int fd) {
  GPR_ASSERT(reserved == nullptr);

  grpc_core::ExecCtx exec_ctx;
  char* name;
  gpr_asprintf(&name, "fd:%d", fd);

  grpc_endpoint* server_endpoint =
      grpc_tcp_create(grpc_fd_create(fd, name, false),
                      grpc_server_get_channel_args(server), name);

  gpr_free(name);

  const grpc_channel_args* server_args = grpc_server_get_channel_args(server);
  grpc_transport* transport = grpc_create_chttp2_transport(
      server_args, server_endpoint, false /* is_client */);

  grpc_pollset** pollsets;
  size_t num_pollsets = 0;
  grpc_server_get_pollsets(server, &pollsets, &num_pollsets);

  for (size_t i = 0; i < num_pollsets; i++) {
    grpc_endpoint_add_to_pollset(server_endpoint, pollsets[i]);
  }

  grpc_server_setup_transport(server, transport, nullptr, server_args);
  grpc_chttp2_transport_start_reading(transport, nullptr, nullptr);
}

#else  // !GPR_SUPPORT_CHANNELS_FROM_FD

void grpc_server_add_insecure_channel_from_fd(grpc_server* server,
                                              void* reserved, int fd) {
  GPR_ASSERT(0);
}

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD
