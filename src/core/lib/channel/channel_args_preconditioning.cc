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

#include "src/core/lib/channel/channel_args_preconditioning.h"

#include <grpc/support/alloc.h>

namespace grpc_core {

void ChannelArgsPreconditioning::Builder::RegisterStage(Stage stage) {
  stages_.emplace_back(std::move(stage));
}

ChannelArgsPreconditioning ChannelArgsPreconditioning::Builder::Build() {
  // TODO(ctiller): should probably make this registered too.
  stages_.emplace_back(RemoveGrpcInternalArgs);

  ChannelArgsPreconditioning preconditioning;
  preconditioning.stages_ = std::move(stages_);
  return preconditioning;
}

const grpc_channel_args* ChannelArgsPreconditioning::PreconditionChannelArgs(
    const grpc_channel_args* args) const {
  const grpc_channel_args* owned_args = nullptr;
  for (auto& stage : stages_) {
    args = stage(args);
    grpc_channel_args_destroy(owned_args);
    owned_args = args;
  }
  return args;
}

}  // namespace grpc_core
