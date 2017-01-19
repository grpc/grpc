/*
 *
 * Copyright 2015, Google Inc.
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

#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/server/chttp2_server.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

int grpc_server_add_insecure_http2_port(grpc_server *server, const char *addr) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  int port_num = 0;
  GRPC_API_TRACE("grpc_server_add_insecure_http2_port(server=%p, addr=%s)", 2,
                 (server, addr));
  grpc_error *err = grpc_chttp2_server_add_port(
      &exec_ctx, server, addr,
      grpc_channel_args_copy(grpc_server_get_channel_args(server)), &port_num);
  if (err != GRPC_ERROR_NONE) {
    const char *msg = grpc_error_string(err);
    gpr_log(GPR_ERROR, "%s", msg);
    grpc_error_free_string(msg);
    GRPC_ERROR_UNREF(err);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  return port_num;
}
