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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/server/chttp2_server.h"

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

int grpc_server_add_secure_http2_port(grpc_server *server, const char *addr,
                                      grpc_server_credentials *creds) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_error *err = GRPC_ERROR_NONE;
  grpc_server_security_connector *sc = NULL;
  int port_num = 0;
  GRPC_API_TRACE(
      "grpc_server_add_secure_http2_port("
      "server=%p, addr=%s, creds=%p)",
      3, (server, addr, creds));
  // Create security context.
  if (creds == NULL) {
    err = GRPC_ERROR_CREATE(
        "No credentials specified for secure server port (creds==NULL)");
    goto done;
  }
  grpc_security_status status =
      grpc_server_credentials_create_security_connector(&exec_ctx, creds, &sc);
  if (status != GRPC_SECURITY_OK) {
    char *msg;
    gpr_asprintf(&msg,
                 "Unable to create secure server with credentials of type %s.",
                 creds->type);
    err = grpc_error_set_int(GRPC_ERROR_CREATE(msg),
                             GRPC_ERROR_INT_SECURITY_STATUS, status);
    gpr_free(msg);
    goto done;
  }
  // Create channel args.
  grpc_arg args_to_add[2];
  args_to_add[0] = grpc_server_credentials_to_arg(creds);
  args_to_add[1] = grpc_security_connector_to_arg(&sc->base);
  grpc_channel_args *args =
      grpc_channel_args_copy_and_add(grpc_server_get_channel_args(server),
                                     args_to_add, GPR_ARRAY_SIZE(args_to_add));
  // Add server port.
  err = grpc_chttp2_server_add_port(&exec_ctx, server, addr, args, &port_num);
done:
  if (sc != NULL) {
    GRPC_SECURITY_CONNECTOR_UNREF(&exec_ctx, &sc->base, "server");
  }
  grpc_exec_ctx_finish(&exec_ctx);
  if (err != GRPC_ERROR_NONE) {
    const char *msg = grpc_error_string(err);
    gpr_log(GPR_ERROR, "%s", msg);

    GRPC_ERROR_UNREF(err);
  }
  return port_num;
}
