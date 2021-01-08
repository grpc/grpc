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

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>

#include <string.h>

#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/server/chttp2_server.h"

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

namespace {

grpc_channel_args* connection_args_modifier(grpc_channel_args* args,
                                            grpc_error** error) {
  grpc_server_credentials* server_credentials =
      grpc_find_server_credentials_in_args(args);
  if (server_credentials == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Could not find server credentials");
    return args;
  }
  auto security_connector = server_credentials->create_security_connector(args);
  if (security_connector == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Unable to create secure server with credentials of type ",
                     server_credentials->type())
            .c_str());
    return args;
  }
  grpc_arg arg_to_add =
      grpc_security_connector_to_arg(security_connector.get());
  grpc_channel_args* new_args =
      grpc_channel_args_copy_and_add(args, &arg_to_add, 1);
  grpc_channel_args_destroy(args);
  return new_args;
}

}  // namespace

int grpc_server_add_secure_http2_port(grpc_server* server, const char* addr,
                                      grpc_server_credentials* creds) {
  grpc_core::ExecCtx exec_ctx;
  grpc_error* err = GRPC_ERROR_NONE;
  int port_num = 0;
  grpc_channel_args* args = nullptr;
  grpc_arg arg_to_add;
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
  // Create channel args.
  arg_to_add = grpc_server_credentials_to_arg(creds);
  args = grpc_channel_args_copy_and_add(server->core_server->channel_args(),
                                        &arg_to_add, 1);
  // Add server port.
  err = grpc_core::Chttp2ServerAddPort(server->core_server.get(), addr, args,
                                       connection_args_modifier, &port_num);
done:
  if (err != GRPC_ERROR_NONE) {
    const char* msg = grpc_error_string(err);
    gpr_log(GPR_ERROR, "%s", msg);

    GRPC_ERROR_UNREF(err);
  }
  return port_num;
}
