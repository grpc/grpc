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

#include <string.h>

#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>
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

grpc_channel_args* ModifyArgsForConnection(grpc_channel_args* args,
                                           grpc_error_handle* error) {
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
  grpc_error_handle err = GRPC_ERROR_NONE;
  grpc_core::RefCountedPtr<grpc_server_security_connector> sc;
  int port_num = 0;
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
  // TODO(yashykt): Ideally, we would not want to have different behavior here
  // based on whether a config fetcher is configured or not. Currently, we have
  // a feature for SSL credentials reloading with an application callback that
  // assumes that there is a single security connector. If we delay the creation
  // of the security connector to after the creation of the listener(s), we
  // would have potentially multiple security connectors which breaks the
  // assumption for SSL creds reloading. When the API for SSL creds reloading is
  // rewritten, we would be able to make this workaround go away by removing
  // that assumption. As an immediate drawback of this workaround, config
  // fetchers need to be registered before adding ports to the server.
  if (server->core_server->config_fetcher() != nullptr) {
    // Create channel args.
    grpc_arg arg_to_add = grpc_server_credentials_to_arg(creds);
    args = grpc_channel_args_copy_and_add(server->core_server->channel_args(),
                                          &arg_to_add, 1);
  } else {
    sc = creds->create_security_connector(nullptr);
    if (sc == nullptr) {
      err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat(
              "Unable to create secure server with credentials of type ",
              creds->type())
              .c_str());
      goto done;
    }
    grpc_arg args_to_add[2];
    args_to_add[0] = grpc_server_credentials_to_arg(creds);
    args_to_add[1] = grpc_security_connector_to_arg(sc.get());
    args = grpc_channel_args_copy_and_add(server->core_server->channel_args(),
                                          args_to_add,
                                          GPR_ARRAY_SIZE(args_to_add));
  }
  // Add server port.
  err = grpc_core::Chttp2ServerAddPort(server->core_server.get(), addr, args,
                                       ModifyArgsForConnection, &port_num);
done:
  sc.reset(DEBUG_LOCATION, "server");
  if (err != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "%s", grpc_error_std_string(err).c_str());

    GRPC_ERROR_UNREF(err);
  }
  return port_num;
}
