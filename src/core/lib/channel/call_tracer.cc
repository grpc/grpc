//
//
// Copyright 2023 gRPC authors.
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
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/call_tracer.h"

namespace grpc_core {

//
// ServerCallTracerFactory
//

namespace {
ServerCallTracerFactory* g_server_call_tracer_factory_ = nullptr;

const char* kServerCallTracerFactoryChannelArgName =
    "grpc.experimental.server_call_tracer_factory";
}  // namespace

ServerCallTracerFactory* ServerCallTracerFactory::Get(
    const ChannelArgs& channel_args) {
  ServerCallTracerFactory* factory =
      channel_args.GetObject<ServerCallTracerFactory>();
  return factory != nullptr ? factory : g_server_call_tracer_factory_;
}

void ServerCallTracerFactory::RegisterGlobal(ServerCallTracerFactory* factory) {
  g_server_call_tracer_factory_ = factory;
}

absl::string_view ServerCallTracerFactory::ChannelArgName() {
  return kServerCallTracerFactoryChannelArgName;
}

}  // namespace grpc_core
