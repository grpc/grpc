/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/grpc_tunnel.h>
#include "src/core/ext/tunnel/tunnel_endpoint.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"

void grpc_tunnel_server_from_call(grpc_call* call, grpc_server* server) {
  grpc_core::ExecCtx exec_ctx;
  grpc_endpoint* ep = grpc_tunnel_endpoint(call);
  const grpc_channel_args* server_args = grpc_server_get_channel_args(server);
  grpc_transport* transport =
      grpc_create_chttp2_transport(server_args, ep, true);
  grpc_channel* channel = grpc_channel_create(
      target, server_args, GRPC_CLIENT_DIRECT_CHANNEL, transport);
  if (channel == nullptr) return;

  grpc_pollset** pollsets;
  size_t num_pollsets = 0;
  grpc_server_get_pollsets(server, &pollsets, &num_pollsets);

  for (size_t i = 0; i < num_pollsets; i++) {
    grpc_endpoint_add_to_pollset(server_endpoint, pollsets[i]);
  }

  grpc_server_setup_transport(server, transport, nullptr, server_args);

  grpc_chttp2_transport_start_reading(transport, nullptr, nullptr);
}
