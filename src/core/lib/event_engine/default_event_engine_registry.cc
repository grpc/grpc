// Copyright 2024 The gRPC Authors
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

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/event_engine/default_event_engine.h"

namespace grpc_event_engine {
namespace experimental {

namespace {
grpc_core::ChannelArgs EnsureEventEngineInChannelArgs(
    grpc_core::ChannelArgs args) {
  if (args.ContainsObject<EventEngine>()) return args;
  return args.SetObject<EventEngine>(GetDefaultEventEngine());
}
}  // namespace

void RegisterEventEngineChannelArgPreconditioning(
    grpc_core::CoreConfiguration::Builder* builder) {
  builder->channel_args_preconditioning()->RegisterStage(
      grpc_event_engine::experimental::EnsureEventEngineInChannelArgs);
}

}  // namespace experimental
}  // namespace grpc_event_engine
