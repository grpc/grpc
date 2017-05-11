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

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/server.h"

void grpc_server_add_insecure_channel_from_fd(grpc_server *server,
                                              void *reserved, int fd) {
  GPR_ASSERT(reserved == NULL);

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  char *name;
  gpr_asprintf(&name, "fd:%d", fd);

  grpc_endpoint *server_endpoint =
      grpc_tcp_create(&exec_ctx, grpc_fd_create(fd, name),
                      grpc_server_get_channel_args(server), name);

  gpr_free(name);

  const grpc_channel_args *server_args = grpc_server_get_channel_args(server);
  grpc_transport *transport = grpc_create_chttp2_transport(
      &exec_ctx, server_args, server_endpoint, 0 /* is_client */);

  grpc_pollset **pollsets;
  size_t num_pollsets = 0;
  grpc_server_get_pollsets(server, &pollsets, &num_pollsets);

  for (size_t i = 0; i < num_pollsets; i++) {
    grpc_endpoint_add_to_pollset(&exec_ctx, server_endpoint, pollsets[i]);
  }

  grpc_server_setup_transport(&exec_ctx, server, transport, NULL, server_args);
  grpc_chttp2_transport_start_reading(&exec_ctx, transport, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
}

#else  // !GPR_SUPPORT_CHANNELS_FROM_FD

void grpc_server_add_insecure_channel_from_fd(grpc_server *server,
                                              void *reserved, int fd) {
  GPR_ASSERT(0);
}

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD
