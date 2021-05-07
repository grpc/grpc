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

#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/server/chttp2_server.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

namespace {

grpc_channel_args* ModifyArgsForConnection(grpc_channel_args* args,
                                           grpc_error_handle* /*error*/) {
  return args;
}

}  // namespace

int grpc_server_add_insecure_http2_port(grpc_server* server, const char* addr) {
  grpc_core::ExecCtx exec_ctx;
  int port_num = 0;
  GRPC_API_TRACE("grpc_server_add_insecure_http2_port(server=%p, addr=%s)", 2,
                 (server, addr));
  grpc_error_handle err = grpc_core::Chttp2ServerAddPort(
      server->core_server.get(), addr,
      grpc_channel_args_copy(server->core_server->channel_args()),
      ModifyArgsForConnection, &port_num);
  if (err != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "%s", grpc_error_std_string(err).c_str());

    GRPC_ERROR_UNREF(err);
  }
  return port_num;
}
