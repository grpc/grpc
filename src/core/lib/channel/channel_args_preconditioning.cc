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

#include "src/core/lib/channel/channel_args_preconditioning.h"

#include <utility>

#include <grpc/support/port_platform.h>

namespace grpc_core {

void ChannelArgsPreconditioning::Builder::RegisterStage(Stage stage) {
  stages_.emplace_back(std::move(stage));
}

ChannelArgsPreconditioning ChannelArgsPreconditioning::Builder::Build() {
  ChannelArgsPreconditioning preconditioning;
  preconditioning.stages_ = std::move(stages_);
  return preconditioning;
}

ChannelArgs ChannelArgsPreconditioning::PreconditionChannelArgs(
    const grpc_channel_args* args) const {
  ChannelArgs channel_args = ChannelArgsBuiltinPrecondition(args);
  for (auto& stage : stages_) {
    channel_args = stage(std::move(channel_args));
  }
  return channel_args;
}

}  // namespace grpc_core
