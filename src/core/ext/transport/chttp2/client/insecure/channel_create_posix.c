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

#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD

#include <fcntl.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/tcp_client_posix.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport.h"

grpc_channel *grpc_insecure_channel_create_from_fd(
    const char *target, int fd, const grpc_channel_args *args) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_API_TRACE("grpc_insecure_channel_create(target=%p, fd=%d, args=%p)", 3,
                 (target, fd, args));

  grpc_arg default_authority_arg = grpc_channel_arg_string_create(
      (char *)GRPC_ARG_DEFAULT_AUTHORITY, (char *)"test.authority");
  grpc_channel_args *final_args =
      grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);

  int flags = fcntl(fd, F_GETFL, 0);
  GPR_ASSERT(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);

  grpc_endpoint *client = grpc_tcp_client_create_from_fd(
      &exec_ctx, grpc_fd_create(fd, "client"), args, "fd-client");

  grpc_transport *transport =
      grpc_create_chttp2_transport(&exec_ctx, final_args, client, true);
  GPR_ASSERT(transport);
  grpc_channel *channel = grpc_channel_create(
      &exec_ctx, target, final_args, GRPC_CLIENT_DIRECT_CHANNEL, transport);
  grpc_channel_args_destroy(&exec_ctx, final_args);
  grpc_chttp2_transport_start_reading(&exec_ctx, transport, NULL, NULL);

  grpc_exec_ctx_finish(&exec_ctx);

  return channel != NULL ? channel : grpc_lame_client_channel_create(
                                         target, GRPC_STATUS_INTERNAL,
                                         "Failed to create client channel");
}

#else  // !GPR_SUPPORT_CHANNELS_FROM_FD

grpc_channel *grpc_insecure_channel_create_from_fd(
    const char *target, int fd, const grpc_channel_args *args) {
  GPR_ASSERT(0);
  return NULL;
}

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD
