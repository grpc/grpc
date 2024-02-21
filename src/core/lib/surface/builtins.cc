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

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/lame_client.h"
#include "src/core/lib/surface/server.h"

namespace grpc_core {

void RegisterBuiltins(CoreConfiguration::Builder* builder) {
  RegisterServerCallTracerFilter(builder);
  builder->channel_init()
      ->RegisterFilter<LameClientFilter>(GRPC_CLIENT_LAME_CHANNEL)
      .Terminal();
  builder->channel_init()
      ->RegisterFilter(GRPC_SERVER_CHANNEL, &Server::kServerTopFilter)
      .BeforeAll();
}

}  // namespace grpc_core
