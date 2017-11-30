/*
 *
 * Copyright 2015 gRPC authors.
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

int grpc_server_add_secure_http2_port(grpc_server* server, const char* addr,
                                      grpc_server_credentials* creds) {
  grpc_core::ExecCtx exec_ctx;
  grpc_error* err = GRPC_ERROR_NONE;
  grpc_server_security_connector* sc = nullptr;
  int port_num = 0;
  grpc_security_status status;
  grpc_channel_args* args = nullptr;
  GRPC_API_TRACE(
      "grpc_server_add_secure_http2_port("
      "server=%p, addr=%s, creds=%p)",
      3, (server, addr, creds));
  // Create security context.
  if (creds == nullptr) {
    err = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "No credentials specified for secure server port (creds==NULL)");
    goto done;
  }
  status = grpc_server_credentials_create_security_connector(creds, &sc);
  if (status != GRPC_SECURITY_OK) {
    char* msg;
    gpr_asprintf(&msg,
                 "Unable to create secure server with credentials of type %s.",
                 creds->type);
    err = grpc_error_set_int(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg),
                             GRPC_ERROR_INT_SECURITY_STATUS, status);
    gpr_free(msg);
    goto done;
  }
  // Create channel args.
  grpc_arg args_to_add[2];
  args_to_add[0] = grpc_server_credentials_to_arg(creds);
  args_to_add[1] = grpc_security_connector_to_arg(&sc->base);
  args =
      grpc_channel_args_copy_and_add(grpc_server_get_channel_args(server),
                                     args_to_add, GPR_ARRAY_SIZE(args_to_add));
  // Add server port.
  err = grpc_chttp2_server_add_port(server, addr, args, &port_num);
done:
  if (sc != nullptr) {
    GRPC_SECURITY_CONNECTOR_UNREF(&sc->base, "server");
  }

  if (err != GRPC_ERROR_NONE) {
    const char* msg = grpc_error_string(err);
    gpr_log(GPR_ERROR, "%s", msg);

    GRPC_ERROR_UNREF(err);
  }
  return port_num;
}
