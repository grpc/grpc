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
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/channel.h"

grpc_channel* grpc_tunnel_client_from_call(const char* target, grpc_call* call, const grpc_channel_args* args) {
  grpc_core::ExecCtx exec_ctx;
  grpc_endpoint* ep = grpc_tunnel_endpoint(call);
  grpc_arg default_authority_arg = grpc_channel_arg_string_create(
      (char*)GRPC_ARG_DEFAULT_AUTHORITY, (char*)"test.authority");
  grpc_channel_args* final_args =
      grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);
  grpc_transport* transport =
      grpc_create_chttp2_transport(final_args, ep, true);
  grpc_channel* channel = grpc_channel_create(
      target, final_args, GRPC_CLIENT_DIRECT_CHANNEL, transport);
  grpc_channel_args_destroy(final_args);
  grpc_chttp2_transport_start_reading(transport, nullptr, nullptr);
  grpc_core::ExecCtx::Get()->Flush();
  return channel != nullptr ? channel
                            : grpc_lame_client_channel_create(
                                  target, GRPC_STATUS_INTERNAL,
                                  "Failed to create client channel");
}

