/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

  grpc_arg default_authority_arg;
  default_authority_arg.type = GRPC_ARG_STRING;
  default_authority_arg.key = GRPC_ARG_DEFAULT_AUTHORITY;
  default_authority_arg.value.string = "test.authority";
  grpc_channel_args *final_args =
      grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);

  int flags = fcntl(fd, F_GETFL, 0);
  GPR_ASSERT(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);

  grpc_endpoint *client = grpc_tcp_client_create_from_fd(
      &exec_ctx, grpc_fd_create(fd, "client"), args, "fd-client");

  grpc_transport *transport =
      grpc_create_chttp2_transport(&exec_ctx, final_args, client, 1);
  GPR_ASSERT(transport);
  grpc_channel *channel = grpc_channel_create(
      &exec_ctx, target, final_args, GRPC_CLIENT_DIRECT_CHANNEL, transport);
  grpc_channel_args_destroy(&exec_ctx, final_args);
  grpc_chttp2_transport_start_reading(&exec_ctx, transport, NULL);

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
