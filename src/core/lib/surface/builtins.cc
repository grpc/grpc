// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/builtins.h"

#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/lame_client.h"
#include "src/core/lib/surface/server.h"

namespace grpc_core {

void RegisterBuiltins(CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterStage(GRPC_CLIENT_SUBCHANNEL,
                                         GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                         grpc_add_connected_filter);
  builder->channel_init()->RegisterStage(GRPC_CLIENT_DIRECT_CHANNEL,
                                         GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                         grpc_add_connected_filter);
  builder->channel_init()->RegisterStage(GRPC_SERVER_CHANNEL,
                                         GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                         grpc_add_connected_filter);
  builder->channel_init()->RegisterStage(
      GRPC_CLIENT_LAME_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      [](grpc_channel_stack_builder* builder) {
        return grpc_channel_stack_builder_append_filter(
            builder, &grpc_lame_filter, nullptr, nullptr);
      });
  builder->channel_init()->RegisterStage(
      GRPC_SERVER_CHANNEL, INT_MAX, [](grpc_channel_stack_builder* builder) {
        return grpc_channel_stack_builder_prepend_filter(
            builder, &Server::kServerTopFilter, nullptr, nullptr);
      });
}

}  // namespace grpc_core
